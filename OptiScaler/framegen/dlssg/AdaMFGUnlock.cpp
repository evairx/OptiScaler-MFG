#include "pch.h"
#include "AdaMFGUnlock.h"

#include <Logger.h>
#include <Config.h>
#include <misc/IdentifyGpu.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>
#include <tlhelp32.h>

namespace AdaMFGUnlock {

namespace internal {

constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kAdaArch = 89;
constexpr uint64_t kUncompressedFlags = 0x41;

struct TemporalProfile {
    size_t ptx_bytes;
    const char* entry_name;
    const char* descriptor_name;
    ptrdiff_t entry_name_offset;
    ptrdiff_t descriptor_name_offset;
};

constexpr TemporalProfile kTemporalProfiles[] = {
    {99362, "main_kernel", "dlfg_kernel", 0x10, 0x28},
    {99626, "Kernel_EstimateIntermMvecsScatter", "EstimateIntermMvecsScatter", 0x10, -0x08},
};

constexpr size_t kExpectedMidpoints = 104;
constexpr char kJoinLabel[] = "$L__BB0_3:";
constexpr char kMidpointBits[] = "0f3F000000";
constexpr char kMulPrefix[] = "mul.ftz.f32 ";
constexpr char kCurrToPrev[] = "%f136";
constexpr char kPrevToCurr[] = "%f134";

inline uint16_t ReadU16(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline uint32_t ReadU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline uint64_t ReadU64(const uint8_t* p) {
    uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline bool Lz4BlockDecompress(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size) {
    size_t in = 0;
    size_t out = 0;
    while (in < src_size) {
        const uint8_t token = src[in++];
        size_t literals = token >> 4;
        if (literals == 15) {
            uint8_t ext = 0;
            do {
                if (in >= src_size) return false;
                ext = src[in++];
                literals += ext;
            } while (ext == 0xFF);
        }
        if (literals > src_size - in || literals > dst_size - out) return false;
        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;
        if (in == src_size) break;
        if (src_size - in < 2) return false;
        const size_t back = static_cast<size_t>(src[in]) | (static_cast<size_t>(src[in + 1]) << 8);
        in += 2;
        if (back == 0 || back > out) return false;
        size_t match = 4 + (token & 0x0F);
        if ((token & 0x0F) == 15) {
            uint8_t ext = 0;
            do {
                if (in >= src_size) return false;
                ext = src[in++];
                match += ext;
            } while (ext == 0xFF);
        }
        if (match > dst_size - out) return false;
        for (size_t i = 0; i < match; ++i) dst[out + i] = dst[out + i - back];
        out += match;
    }
    return in == src_size && out == dst_size;
}

inline bool FindAdaPtxEntry(const uint8_t* fat, size_t fat_size, size_t& entry_offset) {
    if (fat_size < kOuterHeader || ReadU32(fat) != kFatbinMagic) return false;
    if (ReadU16(fat + 6) != kOuterHeader) return false;
    const uint64_t declared = ReadU64(fat + 8);
    if (declared + kOuterHeader != fat_size) return false;

    size_t p = kOuterHeader;
    while (p + 64 <= fat_size) {
        const uint32_t kind = ReadU16(fat + p);
        const uint32_t hdr = ReadU32(fat + p + 4);
        const uint64_t payload = ReadU64(fat + p + 8);
        if (hdr < 64 || payload == 0) return false;
        if (p + hdr + payload > fat_size) return false;
        if (kind == kPtxKind && ReadU32(fat + p + 28) == kAdaArch) {
            entry_offset = p;
            return true;
        }
        p += hdr + payload;
    }
    return false;
}

inline bool BuildTemporalFatbin(const uint8_t* fat, size_t fat_size,
                                const TemporalProfile& profile,
                                std::vector<uint8_t>& out, std::string& why) {
    size_t entry = 0;
    if (!FindAdaPtxEntry(fat, fat_size, entry)) {
        why = "no sm_89 PTX entry";
        return false;
    }

    const uint32_t hdr = ReadU32(fat + entry + 4);
    const uint32_t compressed = ReadU32(fat + entry + 16);
    const uint64_t raw = ReadU64(fat + entry + 56);
    if (compressed == 0 || raw == 0 || raw > (8u << 20)) {
        why = "PTX entry is not compressed as expected";
        return false;
    }
    if (raw != profile.ptx_bytes) {
        std::stringstream s;
        s << "PTX is " << raw << " bytes, expected " << profile.ptx_bytes;
        why = s.str();
        return false;
    }

    std::vector<uint8_t> ptx(static_cast<size_t>(raw));
    if (!Lz4BlockDecompress(fat + entry + hdr, compressed, ptx.data(), ptx.size())) {
        why = "LZ4 decompression failed";
        return false;
    }

    const std::string entry_signature = std::string(".entry ") + profile.entry_name + "(";
    const std::string parameter_name = std::string(profile.entry_name) + "_param_0";
    const std::string parameter_signature =
        std::string(".param .align 8 .b8 ") + parameter_name + "[144]";
    const std::string ptx_text(reinterpret_cast<const char*>(ptx.data()), ptx.size());
    if (ptx_text.find(entry_signature) == std::string::npos ||
        ptx_text.find(parameter_signature) == std::string::npos ||
        ptx_text.find(".reg .f32 %f<1362>;") == std::string::npos) {
        why = "temporal kernel signature changed";
        return false;
    }

    const char* begin = reinterpret_cast<const char*>(ptx.data());
    const size_t n = ptx.size();
    const size_t label_len = sizeof(kJoinLabel) - 1;
    size_t label = SIZE_MAX;
    for (size_t i = 0; i + label_len <= n; ++i) {
        if (std::memcmp(begin + i, kJoinLabel, label_len) != 0) continue;
        if (label != SIZE_MAX) {
            why = "join label is not unique";
            return false;
        }
        label = i;
    }
    if (label == SIZE_MAX) {
        why = "join label not found";
        return false;
    }
    size_t insertion = label + label_len;
    while (insertion < n && begin[insertion] != '\n') ++insertion;
    if (insertion >= n) {
        why = "join label has no line end";
        return false;
    }
    ++insertion;

    const size_t mid_len = sizeof(kMidpointBits) - 1;
    const size_t mul_len = sizeof(kMulPrefix) - 1;
    std::vector<size_t> marks;
    marks.reserve(kExpectedMidpoints);
    for (size_t i = 0; i + mid_len < n; ++i) {
        if (std::memcmp(begin + i, kMidpointBits, mid_len) != 0) continue;
        if (begin[i + mid_len] != ';') continue;
        size_t line = i;
        while (line > 0 && begin[line - 1] != '\n') --line;
        if (i - line < mul_len) continue;
        if (std::memcmp(begin + line, kMulPrefix, mul_len) != 0) continue;
        marks.push_back(i);
    }
    if (marks.size() != kExpectedMidpoints) {
        std::stringstream s;
        s << "found " << marks.size() << " midpoint multiplies, expected " << kExpectedMidpoints;
        why = s.str();
        return false;
    }
    if (marks.front() <= insertion) {
        why = "first midpoint precedes injection point";
        return false;
    }

    const std::string temporal_input =
        "ld.param.f32 %f134, [" + parameter_name + "+32];\r\n"
        "mov.f32 %f135, 0f3F800000;\r\n"
        "sub.ftz.f32 %f136, %f135, %f134;\r\n";

    std::vector<uint8_t> patched;
    patched.reserve(n + temporal_input.size());
    auto append = [&patched](const void* p, size_t bytes) {
        const auto* b = static_cast<const uint8_t*>(p);
        patched.insert(patched.end(), b, b + bytes);
    };
    append(ptx.data(), insertion);
    append(temporal_input.data(), temporal_input.size());
    size_t src = insertion;
    const size_t half = kExpectedMidpoints / 2;
    for (size_t i = 0; i < marks.size(); ++i) {
        append(ptx.data() + src, marks[i] - src);
        const char* scale = (i < half) ? kCurrToPrev : kPrevToCurr;
        append(scale, 5);
        src = marks[i] + mid_len;
    }
    append(ptx.data() + src, n - src);

    const size_t padded = (patched.size() + 7) & ~size_t{7};
    const size_t final_size = entry + hdr + padded;

    out.assign(fat, fat + entry + hdr);
    out.resize(final_size, 0);
    std::memcpy(out.data() + entry + hdr, patched.data(), patched.size());

    const uint64_t payload64 = padded;
    const uint32_t zero32 = 0;
    const uint64_t zero64 = 0;
    std::memcpy(out.data() + entry + 8, &payload64, sizeof(payload64));
    std::memcpy(out.data() + entry + 16, &zero32, sizeof(zero32));
    std::memcpy(out.data() + entry + 40, &kUncompressedFlags, sizeof(kUncompressedFlags));
    std::memcpy(out.data() + entry + 56, &zero64, sizeof(zero64));
    const uint64_t outer = final_size - kOuterHeader;
    std::memcpy(out.data() + 8, &outer, sizeof(outer));
    return true;
}

inline const TemporalProfile* FindTemporalProfile(const uint8_t* fat, size_t fat_size) {
    size_t entry = 0;
    if (!FindAdaPtxEntry(fat, fat_size, entry)) return nullptr;
    const uint64_t raw = ReadU64(fat + entry + 56);
    for (const auto& profile : kTemporalProfiles) {
        if (raw == profile.ptx_bytes) return &profile;
    }
    return nullptr;
}

inline bool PointsToCString(const uint8_t* base, size_t image_size, uint64_t value, const char* expected) {
    const auto start = reinterpret_cast<uintptr_t>(base);
    if (value < start || value >= start + image_size) return false;
    const char* s = reinterpret_cast<const char*>(value);
    const size_t len = std::strlen(expected);
    if (value + len + 1 > start + image_size) return false;
    return std::memcmp(s, expected, len + 1) == 0;
}

inline bool ReadRelativePointer(const uint8_t* base, size_t image_size, const uint8_t* slot,
                                ptrdiff_t displacement, uint64_t& value) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(base);
    const uintptr_t end = start + image_size;
    const uintptr_t address = reinterpret_cast<uintptr_t>(slot);
    uintptr_t field = address;
    if (displacement >= 0) {
        const auto distance = static_cast<uintptr_t>(displacement);
        if (distance > UINTPTR_MAX - address) return false;
        field = address + distance;
    } else {
        const auto distance = static_cast<uintptr_t>(-(displacement + 1)) + 1;
        if (distance > address) return false;
        field = address - distance;
    }
    if (field < start || field > end || sizeof(value) > end - field) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(field), sizeof(value));
    return true;
}

} // namespace internal

bool Manager::IsEnabled() {
    return s_enabled.load(std::memory_order_relaxed);
}

void Manager::SetEnabled(bool enabled) {
    s_enabled.store(enabled, std::memory_order_relaxed);
}

bool Manager::IsArchPatched() {
    return s_archPatched.load(std::memory_order_relaxed);
}

bool Manager::IsMidpointPatched() {
    return s_midpointPatched.load(std::memory_order_relaxed);
}

bool Manager::IsFlipMeteringPatched() {
    return s_flipMeteringPatched.load(std::memory_order_relaxed);
}

bool Manager::IsCeilingPatched() {
    return s_ceilingPatched.load(std::memory_order_relaxed);
}

bool Manager::IsSupportedGpu() {
    const auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    // Exclude non-NVIDIA GPUs (AMD, Intel). If unknown or NVIDIA, allow it.
    if (primaryGpu.vendorId == VendorId::AMD || primaryGpu.vendorId == VendorId::Intel)
        return false;
    return true;
}

bool Manager::IsReadyForMultiFrame() {
    return s_enabled.load(std::memory_order_relaxed) && IsSupportedGpu();
}

bool Manager::IsPacingReady() {
    return s_flipMeteringPatched.load(std::memory_order_relaxed);
}

uint32_t Manager::GetCeilingCompiled() {
    return s_ceilingCompiled.load(std::memory_order_relaxed);
}

uint32_t Manager::GetCeilingEffective() {
    uint32_t eff = s_ceilingEffective.load(std::memory_order_relaxed);
    if (eff > 0) return eff;
    return s_enabled.load(std::memory_order_relaxed) ? 5 : 1;
}

bool Manager::ModuleContains(HMODULE mod, const char* needle, size_t needle_len) {
    if (!mod) return false;
    auto* base = reinterpret_cast<uint8_t*>(mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < needle_len) continue;
        for (size_t off = 0; off + needle_len <= size; ++off) {
            if (std::memcmp(start + off, needle, needle_len) == 0) return true;
        }
    }
    return false;
}

bool Manager::HasKnownDlssgPath(HMODULE mod) {
    if (!mod) return false;
    wchar_t module_path[32768] = {};
    const DWORD length = GetModuleFileNameW(mod, module_path, ARRAYSIZE(module_path));
    if (length == 0 || length >= ARRAYSIZE(module_path)) return false;
    for (DWORD i = 0; i < length; ++i) {
        if (module_path[i] >= L'A' && module_path[i] <= L'Z') {
            module_path[i] = static_cast<wchar_t>(module_path[i] - L'A' + L'a');
        }
    }
    return std::wcsstr(module_path, L"nvngx_dlssg") != nullptr ||
           std::wcsstr(module_path, L"\\models\\dlssg\\") != nullptr ||
           std::wcsstr(module_path, L"/models/dlssg/") != nullptr;
}

bool Manager::IsDlssgProvider(HMODULE mod) {
    if (!mod) return false;
    if (HasKnownDlssgPath(mod)) return true;

    const bool has_d3d12_entry =
        GetProcAddress(mod, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") != nullptr;
    const bool has_vulkan_entry =
        GetProcAddress(mod, "NVSDK_NGX_VULKAN_PopulateDeviceParameters_Impl") != nullptr;

    if (!has_d3d12_entry && !has_vulkan_entry) return false;
    constexpr char kDlssgMarker[] = "dlfg_kernel";
    return ModuleContains(mod, kDlssgMarker, sizeof(kDlssgMarker) - 1);
}

bool Manager::PatchArchGatesInModule(HMODULE mod) {
    if (!mod || s_archPatched.load() || !IsSupportedGpu()) return false;

    auto* base = reinterpret_cast<uint8_t*>(mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    constexpr uint8_t kArchNew = 0x90; // Ada 0x190
    constexpr uint8_t kArchOld = 0xB0; // Blackwell

    std::vector<uint8_t*> found;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < 6) continue;

        for (size_t off = 0; off + 6 <= size; ++off) {
            // 3D B0 01 00 00 -> cmp eax, 0x1b0
            if (start[off] == 0x3D && start[off + 1] == kArchOld && start[off + 2] == 0x01 &&
                start[off + 3] == 0x00 && start[off + 4] == 0x00) {
                found.push_back(start + off + 1);
                continue;
            }
            // 81 /7 B0 01 00 00 -> cmp r32, 0x1b0
            if (start[off] == 0x81 && start[off + 1] >= 0xF8 && start[off + 1] <= 0xFF &&
                start[off + 2] == kArchOld && start[off + 3] == 0x01 && start[off + 4] == 0x00 &&
                start[off + 5] == 0x00) {
                found.push_back(start + off + 2);
            }
        }
    }

    if (found.empty()) {
        LOG_WARN("AdaMFGUnlock: No arch gates found in nvngx_dlssg module.");
        return false;
    }

    size_t sitesWritten = 0;
    for (uint8_t* site : found) {
        DWORD oldProtect = 0;
        if (VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_gateSites.push_back({site, *site});
            *site = kArchNew;
            VirtualProtect(site, 1, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), site, 1);
            sitesWritten++;
        }
    }

    if (sitesWritten > 0) {
        s_archPatched.store(true);
        LOG_INFO("AdaMFGUnlock: Successfully patched {} arch gates (0x1b0 -> 0x190) in nvngx_dlssg!", sitesWritten);
        return true;
    }
    return false;
}

bool Manager::PatchMidpointInModule(HMODULE mod) {
    if (!mod || s_midpointPatched.load()) return false;

    auto* base = reinterpret_cast<uint8_t*>(mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const size_t image_size = nt->OptionalHeader.SizeOfImage;
    const auto start = reinterpret_cast<uintptr_t>(base);

    std::vector<uint64_t*> slots;
    const uint8_t* fat = nullptr;
    size_t fat_size = 0;
    const internal::TemporalProfile* selected_profile = nullptr;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        uint8_t* sec = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        for (size_t off = 0; off + sizeof(uint64_t) <= size; off += sizeof(uint64_t)) {
            uint64_t value = 0;
            std::memcpy(&value, sec + off, sizeof(value));
            if (value < start || value >= start + image_size) continue;
            const auto* candidate = reinterpret_cast<const uint8_t*>(value);
            if (internal::ReadU32(candidate) != internal::kFatbinMagic) continue;

            const internal::TemporalProfile* name_profile = nullptr;
            for (const auto& profile : internal::kTemporalProfiles) {
                uint64_t entry_name = 0;
                uint64_t desc_name = 0;
                if (!internal::ReadRelativePointer(base, image_size, sec + off,
                                                   profile.entry_name_offset, entry_name) ||
                    !internal::ReadRelativePointer(base, image_size, sec + off,
                                                   profile.descriptor_name_offset, desc_name)) {
                    continue;
                }
                if (internal::PointsToCString(base, image_size, entry_name, profile.entry_name) &&
                    internal::PointsToCString(base, image_size, desc_name, profile.descriptor_name)) {
                    name_profile = &profile;
                    break;
                }
            }
            if (name_profile == nullptr) continue;

            const uint64_t declared = internal::ReadU64(candidate + 8);
            const size_t total = static_cast<size_t>(declared) + internal::kOuterHeader;
            if (total < 1024 || total > (16u << 20)) continue;
            if (value + total > start + image_size) continue;
            const auto* fat_profile = internal::FindTemporalProfile(candidate, total);
            if (fat_profile == nullptr || fat_profile != name_profile) continue;
            if (fat == nullptr) {
                fat = candidate;
                fat_size = total;
                selected_profile = fat_profile;
            } else if (candidate != fat) {
                continue;
            }
            slots.push_back(reinterpret_cast<uint64_t*>(sec + off));
        }
    }

    if (fat == nullptr || slots.empty() || selected_profile == nullptr) {
        LOG_WARN("AdaMFGUnlock: Temporal-kernel descriptor not found in nvngx_dlssg.");
        return false;
    }

    std::vector<uint8_t> rebuilt;
    std::string why;
    if (!internal::BuildTemporalFatbin(fat, fat_size, *selected_profile, rebuilt, why)) {
        LOG_ERROR("AdaMFGUnlock: Failed to rebuild temporal Fatbin: {}", why);
        return false;
    }

    void* mem = VirtualAlloc(nullptr, rebuilt.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (mem == nullptr) {
        LOG_ERROR("AdaMFGUnlock: Memory allocation for rebuilt Fatbin failed.");
        return false;
    }
    std::memcpy(mem, rebuilt.data(), rebuilt.size());

    for (uint64_t* slot : slots) {
        DWORD old_protect = 0;
        if (VirtualProtect(slot, sizeof(uint64_t), PAGE_READWRITE, &old_protect) == 0) continue;
        s_midpointPatches.push_back({slot, *slot});
        *slot = reinterpret_cast<uint64_t>(mem);
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(uint64_t), old_protect, &ignored);
    }

    if (s_midpointPatches.empty()) {
        VirtualFree(mem, 0, MEM_RELEASE);
        LOG_ERROR("AdaMFGUnlock: No descriptor slots writable.");
        return false;
    }

    s_fatbinAllocation = mem;
    s_midpointPatched.store(true);
    LOG_INFO("AdaMFGUnlock: Successfully redirected {} {} descriptors to temporal-corrected Fatbin!",
             s_midpointPatches.size(), selected_profile->descriptor_name);
    return true;
}

bool Manager::WriteFlipSite(uint8_t* at, const uint8_t* bytes, size_t length) {
    if (length == 0 || length > sizeof(FlipSite::original)) return false;
    DWORD old_protect = 0;
    if (VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old_protect) == 0) return false;
    FlipSite site = {};
    site.address = at;
    site.length = static_cast<uint8_t>(length);
    std::memcpy(site.original, at, length);
    s_flipSites.push_back(site);
    std::memcpy(at, bytes, length);
    DWORD ignored = 0;
    VirtualProtect(at, length, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, length);
    return true;
}

bool Manager::PatchFrameCountCeiling(HMODULE mod) {
    if (!mod || s_ceilingPatched.load()) return false;

    auto* base = reinterpret_cast<uint8_t*>(mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const uint8_t tail[] = {0x3B, 0xCA, 0x0F, 0x42, 0xD1};
    uint8_t* found = nullptr;
    size_t hits = 0;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < 10) continue;
        for (size_t off = 0; off + 10 <= size; ++off) {
            if (start[off] != 0xBA) continue;
            if (start[off + 2] != 0 || start[off + 3] != 0 || start[off + 4] != 0) continue;
            if (std::memcmp(start + off + 5, tail, sizeof(tail)) != 0) continue;
            const uint8_t ceiling = start[off + 1];
            if (ceiling == 0 || ceiling > 8) continue;
            if (found == nullptr) found = start + off;
            ++hits;
        }
    }

    if (hits != 1 || found == nullptr) return false;

    DWORD old_protect = 0;
    if (VirtualProtect(found, 10, PAGE_EXECUTE_READWRITE, &old_protect) == 0) return false;
    s_ceilingSite = found;
    s_ceilingOriginal = found[1];
    s_ceilingCmovOriginal = found[9];
    s_ceilingCompiled.store(found[1], std::memory_order_relaxed);
    s_ceilingEffective.store(s_ceilingCompiled.load(std::memory_order_relaxed), std::memory_order_relaxed);

    // cmovb edx, ecx -> cmovb edx, edx (0x0F 0x42 0xD2)
    // Preserves the plugin's own compiled ceiling (3 for older plugins, 5 for newer)
    // and prevents lowering it to ecx (1 on Ada/non-Blackwell)
    found[9] = 0xD2;

    DWORD ignored = 0;
    VirtualProtect(found, 10, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), found, 10);
    s_ceilingPatched.store(true);
    LOG_INFO("AdaMFGUnlock: Uncapped DLSS-G frame count ceiling (compiled={}, effective={}) via cmovb edx, edx!",
             s_ceilingCompiled.load(std::memory_order_relaxed), s_ceilingEffective.load(std::memory_order_relaxed));
    return true;
}

bool Manager::PatchFlipMeteringInModule(HMODULE mod) {
    if (!mod || s_flipMeteringPatched.load()) return false;

    auto* base = reinterpret_cast<uint8_t*>(mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    constexpr char kFlipMarker[] = "FG1 DLL has been detected";
    const size_t marker_len = sizeof(kFlipMarker) - 1;
    const uint8_t* marker = nullptr;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections && marker == nullptr; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < marker_len) continue;
        for (size_t off = 0; off + marker_len <= size; ++off) {
            if (std::memcmp(start + off, kFlipMarker, marker_len) == 0) {
                marker = start + off;
                break;
            }
        }
    }

    if (marker == nullptr) {
        return false;
    }

    unsigned int want_offset = 0;
    int want_value = -1;
    section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections && want_value < 0; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < 8) continue;
        for (size_t off = 0; off + 8 <= size && want_value < 0; ++off) {
            if (!(start[off] == 0x48 || start[off] == 0x4C)) continue;
            if (start[off + 1] != 0x8D) continue;
            if ((start[off + 2] & 0xC7) != 0x05) continue;
            int disp = 0;
            std::memcpy(&disp, start + off + 3, sizeof(disp));
            if (start + off + 7 + disp != marker) continue;

            const size_t window = 0x200;
            const size_t limit = (off + window < size) ? (off + window) : size;
            for (size_t w = off; w + 7 <= limit; ++w) {
                if (start[w] != 0xC6) continue;
                if (start[w + 1] < 0x80 || start[w + 1] > 0xBF) continue;
                unsigned int field = 0;
                std::memcpy(&field, start + w + 2, sizeof(field));
                const unsigned char imm = start[w + 6];
                if (field <= 0x100 || field >= 0x20000) continue;
                if (imm > 1) continue;
                want_offset = field;
                want_value = imm;
                break;
            }
        }
    }

    if (want_value < 0) {
        LOG_WARN("AdaMFGUnlock: DLSS-G plugin located but could not read flip-metering fallback state.");
        return true;
    }

    const uint8_t opposite = static_cast<uint8_t>(1 - want_value);
    section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < 7) continue;
        for (size_t off = 0; off + 7 <= size; ++off) {
            if (start[off] == 0xC6) {
                if (start[off + 1] < 0x80 || start[off + 1] > 0xBF) continue;
                unsigned int field = 0;
                std::memcpy(&field, start + off + 2, sizeof(field));
                if (field != want_offset) continue;
                if (start[off + 6] != opposite) continue;
                const uint8_t imm = static_cast<uint8_t>(want_value);
                WriteFlipSite(start + off + 6, &imm, 1);
                continue;
            }

            if (start[off] != 0x40 || start[off + 1] != 0x88) continue;
            const uint8_t modrm = start[off + 2];
            if (modrm < 0x80 || modrm > 0xBF) continue;
            const uint8_t rm = static_cast<uint8_t>(modrm & 7);
            if (rm == 4) continue;
            unsigned int field = 0;
            std::memcpy(&field, start + off + 3, sizeof(field));
            if (field != want_offset) continue;

            uint8_t replacement[7] = {0xC6, static_cast<uint8_t>(0x80 | rm), 0, 0, 0, 0, static_cast<uint8_t>(want_value)};
            std::memcpy(replacement + 2, &want_offset, sizeof(want_offset));
            WriteFlipSite(start + off, replacement, sizeof(replacement));
        }
    }

    if (s_flipSites.empty()) {
        LOG_WARN("AdaMFGUnlock: flip-metering field +0x{:X} found, but nothing writes it in patchable form.", want_offset);
        return true;
    }

    s_flipMeteringPatched.store(true);
    LOG_INFO("AdaMFGUnlock: forced flip-metering off in DLSS-G plugin (pinned field +0x{:X} to {} at {} sites); multi-frame will pace in software (RSYNC).",
             want_offset, want_value, s_flipSites.size());

    return true;
}

bool Manager::PatchNvngxDlssg(HMODULE dlssgModule) {
    if (!dlssgModule || !s_enabled.load() || !IsSupportedGpu()) return false;
    bool archOk = PatchArchGatesInModule(dlssgModule);
    bool midOk = PatchMidpointInModule(dlssgModule);
    return archOk || midOk;
}

bool Manager::PatchDlssgPlugin(HMODULE pluginModule) {
    if (!pluginModule || !s_enabled.load()) return false;

    // Do not scan arbitrary loaded DLLs for a short instruction sequence. The
    // Streamline DLSS-G plugin carries this diagnostic marker across the
    // supported provider builds and it is the same identity check used by the
    // legacy flip-metering path.
    constexpr char kFlipMarker[] = "FG1 DLL has been detected";
    if (!ModuleContains(pluginModule, kFlipMarker, sizeof(kFlipMarker) - 1)) return false;

    // On Ada (RTX 40), software flip pacing (RSYNC) is mandatory for multi-frame (3X+),
    // as Ada lacks the Blackwell hardware flip meter. Also unclamp the plugin's frame ceiling.
    const bool ceilingPatched = PatchFrameCountCeiling(pluginModule);
    const bool flipPatched = PatchFlipMeteringInModule(pluginModule);
    return ceilingPatched || flipPatched;
}

void Manager::OnModuleLoaded(HMODULE mod, const wchar_t* path) {
    if (!mod || !s_enabled.load()) return;

    if (IsDlssgProvider(mod)) {
        LOG_INFO("AdaMFGUnlock: OnModuleLoaded identified DLSS-G provider, patching immediately...");
        PatchNvngxDlssg(mod);
    } else {
        wchar_t modPath[MAX_PATH] = {};
        if (path != nullptr) {
            wcsncpy_s(modPath, path, MAX_PATH);
        } else {
            GetModuleFileNameW(mod, modPath, MAX_PATH);
        }

        std::wstring lower(modPath);
        for (auto& c : lower) c = towlower(c);

        if (lower.find(L"nvngx_dlssg") != std::wstring::npos ||
            lower.find(L"\\models\\dlssg\\") != std::wstring::npos ||
            lower.find(L"/models/dlssg/") != std::wstring::npos) {
            PatchNvngxDlssg(mod);
        } else if (lower.find(L"sl.dlss_g") != std::wstring::npos ||
                   lower.find(L"sl_dlss_g") != std::wstring::npos) {
            PatchDlssgPlugin(mod);
        }
    }
}

void Manager::CheckAndPatchAll() {
    if (!s_enabled.load()) return;
    static std::mutex s_patchMutex;
    std::lock_guard<std::mutex> lock(s_patchMutex);

    if (!s_archPatched.load() || !s_midpointPatched.load()) {
        HMODULE dlssg = GetModuleHandleW(L"nvngx_dlssg.dll");
        if (dlssg) PatchNvngxDlssg(dlssg);
    }

    if (!s_ceilingPatched.load() || !s_flipMeteringPatched.load()) {
        HMODULE plugin = GetModuleHandleW(L"sl.dlss_g.dll");
        if (plugin) PatchDlssgPlugin(plugin);
    }

    // If any component is still not patched, scan all loaded process modules (handles OTA hashed DLL names)
    if (!s_ceilingPatched.load() || !s_archPatched.load() || !s_midpointPatched.load() || !s_flipMeteringPatched.load()) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me = {};
            me.dwSize = sizeof(me);
            if (Module32FirstW(snap, &me)) {
                do {
                    if (me.hModule == dllModule) continue;

                    if ((!s_archPatched.load() || !s_midpointPatched.load()) && IsDlssgProvider(me.hModule)) {
                        PatchNvngxDlssg(me.hModule);
                    }

                    if (!s_ceilingPatched.load() || !s_flipMeteringPatched.load()) {
                        PatchDlssgPlugin(me.hModule);
                    }
                } while (Module32NextW(snap, &me));
            }
            CloseHandle(snap);
        }
    }
}

void Manager::RestoreAll() {
    for (const auto& site : s_gateSites) {
        DWORD old = 0;
        if (VirtualProtect(site.address, 1, PAGE_EXECUTE_READWRITE, &old)) {
            *site.address = site.original;
            VirtualProtect(site.address, 1, old, &old);
            FlushInstructionCache(GetCurrentProcess(), site.address, 1);
        }
    }
    s_gateSites.clear();
    s_archPatched.store(false);

    for (const auto& p : s_midpointPatches) {
        DWORD old = 0;
        if (VirtualProtect(p.slot, sizeof(uint64_t), PAGE_READWRITE, &old)) {
            *p.slot = p.original;
            VirtualProtect(p.slot, sizeof(uint64_t), old, &old);
        }
    }
    s_midpointPatches.clear();
    s_midpointPatched.store(false);

    for (const auto& site : s_flipSites) {
        DWORD old = 0;
        if (VirtualProtect(site.address, site.length, PAGE_EXECUTE_READWRITE, &old)) {
            std::memcpy(site.address, site.original, site.length);
            VirtualProtect(site.address, site.length, old, &old);
            FlushInstructionCache(GetCurrentProcess(), site.address, site.length);
        }
    }
    s_flipSites.clear();
    s_flipMeteringPatched.store(false);

    if (s_ceilingPatched.load() && s_ceilingSite != nullptr) {
        DWORD old = 0;
        if (VirtualProtect(s_ceilingSite, 10, PAGE_EXECUTE_READWRITE, &old)) {
            s_ceilingSite[1] = s_ceilingOriginal;
            s_ceilingSite[9] = s_ceilingCmovOriginal;
            VirtualProtect(s_ceilingSite, 10, old, &old);
            FlushInstructionCache(GetCurrentProcess(), s_ceilingSite, 10);
        }
        s_ceilingSite = nullptr;
        s_ceilingPatched.store(false);
    }
    s_ceilingOriginal = 0;
    s_ceilingCmovOriginal = 0;
    s_ceilingCompiled.store(0, std::memory_order_relaxed);
    s_ceilingEffective.store(0, std::memory_order_relaxed);
}

} // namespace AdaMFGUnlock
