// Adapted from y4my4my4m/OptiScaler_DLSSNR_Multipass_MFG, tag v4 (7b7220bb), GPL-3.0.
#include "pch.h"

#include "MfgUnlock.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <scanner/scanner.h>
#include <misc/IdentifyGpu.h>
#include <limits>

namespace
{
// mov ebx,1 / mov r8d,3 / cmp edi,0x1b0 / cmovl r8d,ebx. The two counts and the architecture
// constant together are unique in the module; the wildcards cover nothing, they are here only to
// keep the shape readable.
constexpr std::string_view kAdvertisePattern = "BB 01 00 00 00 41 B8 03 00 00 00 81 FF B0 01 00 00 44 0F 4C C3";

// cmp eax,0x1b0 / jl / cmp ebx,3 / jbe. The only comparison against the architecture constant that
// is followed by a signed branch and a count test.
constexpr std::string_view kValidatePattern = "3D B0 01 00 00 7C ? 83 FB 03 76";

// Five generated frames, the count both patched sites carry (6X).
constexpr uint8_t kMaxGeneratedFrames = 5;

// 310.9 restructured both gates. The count is no longer an immediate next to the comparison: the
// Blackwell branch starts at five and reads a configured value, and anything below Blackwell is sent
// to a branch that publishes one.
//     cmp ebp, 0x1b0
//     jl  ada          <- neutralised, so every card takes the Blackwell branch
//     mov edi, 0x5
constexpr std::string_view kAdvertisePattern309 = "81 FD B0 01 00 00 0F 8C ? ? ? ? BF 05 00 00 00";

// The capability flag in the same build is a setae rather than a branch.
//     cmp   eax, 0x1b0
//     setae al
constexpr std::string_view kValidatePattern309 = "3D B0 01 00 00 0F 93 C0";

MfgUnlock::Status g_status {};

uintptr_t UniqueAddress(HMODULE module, std::string_view pattern)
{
    const auto first = scanner::GetAddress(module, pattern);
    return first && !scanner::GetAddress(module, pattern, 0, first + 1) ? first : 0;
}

// The module's own file version, for the report. A signature that does not match is expected on a
// version nobody has looked at, and the version is the one thing that makes such a report actionable.
std::string ModuleVersion(HMODULE module)
{
    wchar_t path[MAX_PATH] {};

    if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
        return {};

    version_t file {};
    version_t product {};

    if (!Util::GetFileVersion(path, &file, &product))
        return {};

    return std::format("{}.{}.{}", file.major, file.minor, file.patch);
}

// A mapped address is only a valid patch target while it lands inside one of the module's own
// sections. Anything else is a stale pointer or somebody else's memory.
bool IsInsideSection(HMODULE module, uintptr_t address, size_t size, bool* executable)
{
    const auto base = reinterpret_cast<const uint8_t*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || size == 0)
        return false;

    const auto section = IMAGE_FIRST_SECTION(nt);

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto start = reinterpret_cast<uintptr_t>(base + section[i].VirtualAddress);
        const auto length = static_cast<size_t>(section[i].Misc.VirtualSize);

        if (length >= size && address >= start && address - start <= length - size)
        {
            if (executable != nullptr)
                *executable = (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;

            return true;
        }
    }

    return false;
}

struct BytePatch
{
    uintptr_t address = 0;
    std::vector<uint8_t> bytes;
};

// All-or-nothing. Every byte is prepared first, applied under its own protection change, and a
// failure anywhere restores what was already written so the module is never left half patched.
bool ApplyPatches(HMODULE module, const std::vector<BytePatch>& patches)
{
    struct Applied
    {
        uintptr_t address = 0;
        std::vector<uint8_t> original;
        DWORD protection = 0;
    };

    std::vector<Applied> applied;
    applied.reserve(patches.size());

    for (const auto& patch : patches)
    {
        if (patch.address == 0 || patch.bytes.empty() ||
            !IsInsideSection(module, patch.address, patch.bytes.size(), nullptr))
        {
            LOG_WARN("MFG unlock: patch target {:X} is not inside a mapped section", patch.address);
            break;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(patch.address), patch.bytes.size(),
                            PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LOG_WARN("MFG unlock: VirtualProtect failed at {:X}", patch.address);
            break;
        }

        Applied entry;
        entry.address = patch.address;
        entry.protection = oldProtect;
        entry.original.resize(patch.bytes.size());
        std::memcpy(entry.original.data(), reinterpret_cast<const void*>(patch.address), patch.bytes.size());
        std::memcpy(reinterpret_cast<void*>(patch.address), patch.bytes.data(), patch.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(patch.address), patch.bytes.size());

        DWORD ignored = 0;
        VirtualProtect(reinterpret_cast<LPVOID>(patch.address), patch.bytes.size(), oldProtect, &ignored);

        applied.push_back(std::move(entry));
    }

    if (applied.size() == patches.size())
        return true;

    for (auto it = applied.rbegin(); it != applied.rend(); ++it)
    {
        DWORD ignored = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(it->address), it->original.size(),
                           PAGE_EXECUTE_READWRITE, &ignored))
        {
            std::memcpy(reinterpret_cast<void*>(it->address), it->original.data(), it->original.size());
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(it->address), it->original.size());
            VirtualProtect(reinterpret_cast<LPVOID>(it->address), it->original.size(), it->protection, &ignored);
        }
    }

    LOG_WARN("MFG unlock: patch set rolled back ({} of {} applied)", applied.size(), patches.size());
    return false;
}

// ---------------------------------------------------------------------------
// Temporal (midpoint) fallback, adapted from mavismmg/MFGAdaUnlock-RenoDx (MIT).
//
// The Blackwell retarget replaces the interpolation kernel with one that consumes
// the generated frame's temporal parameter. When a provider carries no usable
// Blackwell image, the Ada kernel still blends with a compiled-in 0.5, so 3X/4X
// repeat the same midpoint frames. This fallback rewrites the Ada kernel PTX: it
// injects the temporal weight, replaces the 104 blend constants, and repoints
// every descriptor at the rebuilt, truncated fatbin so the driver JITs the
// corrected program instead of loading the precompiled cubin.
// ---------------------------------------------------------------------------
namespace midpoint
{
constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kAdaArch = 89;
constexpr uint64_t kUncompressedFlags = 0x41;
constexpr size_t kExpectedMidpoints = 104;
constexpr char kJoinLabel[] = "$L__BB0_3:";
constexpr char kMidpointBits[] = "0f3F000000";
constexpr char kMulPrefix[] = "mul.ftz.f32 ";
constexpr char kCurrToPrev[] = "%f136";
constexpr char kPrevToCurr[] = "%f134";

// Structural expectations. NVIDIA renamed every kernel in 310.9, but the
// temporal program itself is unchanged. Keep exact profiles so an unrelated
// provider is not admitted merely because it contains a similar sequence.
struct TemporalProfile
{
    size_t ptxBytes;
    const char* entryName;
    const char* descriptorName;
    ptrdiff_t entryNameOffset;
    ptrdiff_t descriptorNameOffset;
};

constexpr TemporalProfile kTemporalProfiles[] = {
    { 99362, "main_kernel", "dlfg_kernel", 0x10, 0x28 },
    { 99626, "Kernel_EstimateIntermMvecsScatter", "EstimateIntermMvecsScatter", 0x10, -0x08 },
};

uint16_t ReadU16(const uint8_t* p)
{
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t ReadU32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint64_t ReadU64(const uint8_t* p)
{
    uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Plain LZ4 block format: token high nibble = literal run, low nibble = match
// length - 4, 16-bit little-endian back offset, 0xFF continuation bytes.
bool Lz4BlockDecompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize)
{
    size_t in = 0;
    size_t out = 0;

    while (in < srcSize)
    {
        const uint8_t token = src[in++];
        size_t literals = token >> 4;

        if (literals == 15)
        {
            uint8_t ext = 0;
            do
            {
                if (in >= srcSize)
                    return false;
                ext = src[in++];
                literals += ext;
            } while (ext == 0xFF);
        }

        if (literals > srcSize - in || literals > dstSize - out)
            return false;

        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;

        if (in == srcSize)
            break;

        if (srcSize - in < 2)
            return false;

        const size_t back = static_cast<size_t>(src[in]) | (static_cast<size_t>(src[in + 1]) << 8);
        in += 2;

        if (back == 0 || back > out)
            return false;

        size_t match = 4 + (token & 0x0F);
        if ((token & 0x0F) == 15)
        {
            uint8_t ext = 0;
            do
            {
                if (in >= srcSize)
                    return false;
                ext = src[in++];
                match += ext;
            } while (ext == 0xFF);
        }

        if (match > dstSize - out)
            return false;

        for (size_t i = 0; i < match; ++i)
            dst[out + i] = dst[out + i - back];

        out += match;
    }

    return in == srcSize && out == dstSize;
}

// Locates the sm_89 PTX entry inside a fatbin by walking its entry list.
bool FindAdaPtxEntry(const uint8_t* fat, size_t fatSize, size_t& entryOffset)
{
    if (fatSize < kOuterHeader || ReadU32(fat) != kFatbinMagic)
        return false;

    if (ReadU16(fat + 6) != kOuterHeader)
        return false;

    const uint64_t declared = ReadU64(fat + 8);
    if (declared + kOuterHeader != fatSize)
        return false;

    size_t p = kOuterHeader;
    while (p + 64 <= fatSize)
    {
        const uint32_t kind = ReadU16(fat + p);
        const uint32_t hdr = ReadU32(fat + p + 4);
        const uint64_t payload = ReadU64(fat + p + 8);

        if (hdr < 64 || payload == 0)
            return false;

        if (p + hdr + payload > fatSize)
            return false;

        if (kind == kPtxKind && ReadU32(fat + p + 28) == kAdaArch)
        {
            entryOffset = p;
            return true;
        }

        p += hdr + payload;
    }

    return false;
}

// Decompress the Ada PTX, rewrite the blend weights, and re-emit a truncated
// fatbin that ends after it (dropping the precompiled cubin forces the JIT).
bool BuildTemporalFatbin(const uint8_t* fat, size_t fatSize, const TemporalProfile& profile,
                         std::vector<uint8_t>& out, std::string& why)
{
    size_t entry = 0;
    if (!FindAdaPtxEntry(fat, fatSize, entry))
    {
        why = "no sm_89 PTX entry";
        return false;
    }

    const uint32_t hdr = ReadU32(fat + entry + 4);
    const uint32_t compressed = ReadU32(fat + entry + 16);
    const uint64_t raw = ReadU64(fat + entry + 56);

    if (compressed == 0 || raw == 0 || raw > (8u << 20))
    {
        why = "PTX entry is not compressed as expected";
        return false;
    }

    if (raw != profile.ptxBytes)
    {
        why = std::format("PTX is {} bytes, expected {}", raw, profile.ptxBytes);
        return false;
    }

    std::vector<uint8_t> ptx(static_cast<size_t>(raw));
    if (!Lz4BlockDecompress(fat + entry + hdr, compressed, ptx.data(), ptx.size()))
    {
        why = "LZ4 decompression failed";
        return false;
    }

    const std::string entrySignature = std::string(".entry ") + profile.entryName + "(";
    const std::string parameterName = std::string(profile.entryName) + "_param_0";
    const std::string parameterSignature =
        std::string(".param .align 8 .b8 ") + parameterName + "[144]";
    const std::string ptxText(reinterpret_cast<const char*>(ptx.data()), ptx.size());

    if (ptxText.find(entrySignature) == std::string::npos ||
        ptxText.find(parameterSignature) == std::string::npos ||
        ptxText.find(".reg .f32 %f<1362>;") == std::string::npos)
    {
        why = "temporal kernel signature changed";
        return false;
    }

    // The join label must be unique, or we are not looking at the kernel we think.
    const char* begin = reinterpret_cast<const char*>(ptx.data());
    const size_t n = ptx.size();
    const size_t labelLen = sizeof(kJoinLabel) - 1;
    size_t label = SIZE_MAX;

    for (size_t i = 0; i + labelLen <= n; ++i)
    {
        if (std::memcmp(begin + i, kJoinLabel, labelLen) != 0)
            continue;

        if (label != SIZE_MAX)
        {
            why = "join label is not unique";
            return false;
        }

        label = i;
    }

    if (label == SIZE_MAX)
    {
        why = "join label not found";
        return false;
    }

    size_t insertion = label + labelLen;
    while (insertion < n && begin[insertion] != '\n')
        ++insertion;

    if (insertion >= n)
    {
        why = "join label has no line end";
        return false;
    }

    ++insertion;

    // Every midpoint constant that terminates a mul.ftz.f32 line.
    const size_t midLen = sizeof(kMidpointBits) - 1;
    const size_t mulLen = sizeof(kMulPrefix) - 1;
    std::vector<size_t> marks;
    marks.reserve(kExpectedMidpoints);

    for (size_t i = 0; i + midLen < n; ++i)
    {
        if (std::memcmp(begin + i, kMidpointBits, midLen) != 0)
            continue;

        if (begin[i + midLen] != ';')
            continue;

        size_t line = i;
        while (line > 0 && begin[line - 1] != '\n')
            --line;

        if (i - line < mulLen)
            continue;

        if (std::memcmp(begin + line, kMulPrefix, mulLen) != 0)
            continue;

        marks.push_back(i);
    }

    if (marks.size() != kExpectedMidpoints)
    {
        why = std::format("found {} midpoint multiplies, expected {}", marks.size(), kExpectedMidpoints);
        return false;
    }

    if (marks.front() <= insertion)
    {
        why = "first midpoint precedes the injection point";
        return false;
    }

    const std::string temporalInput = "ld.param.f32 %f134, [" + parameterName + "+32];\r\n"
                                      "mov.f32 %f135, 0f3F800000;\r\n"
                                      "sub.ftz.f32 %f136, %f135, %f134;\r\n";

    std::vector<uint8_t> patched;
    patched.reserve(n + temporalInput.size());
    auto append = [&patched](const void* p, size_t bytes)
    {
        const auto* b = static_cast<const uint8_t*>(p);
        patched.insert(patched.end(), b, b + bytes);
    };

    append(ptx.data(), insertion);
    append(temporalInput.data(), temporalInput.size());

    size_t src = insertion;
    const size_t half = kExpectedMidpoints / 2;
    for (size_t i = 0; i < marks.size(); ++i)
    {
        append(ptx.data() + src, marks[i] - src);
        const char* scale = (i < half) ? kCurrToPrev : kPrevToCurr;
        append(scale, 5);
        src = marks[i] + midLen;
    }
    append(ptx.data() + src, n - src);

    const size_t padded = (patched.size() + 7) & ~size_t { 7 };
    const size_t finalSize = entry + hdr + padded;

    out.assign(fat, fat + entry + hdr);
    out.resize(finalSize, 0);
    std::memcpy(out.data() + entry + hdr, patched.data(), patched.size());

    const uint64_t payload64 = padded;
    const uint32_t zero32 = 0;
    const uint64_t zero64 = 0;
    std::memcpy(out.data() + entry + 8, &payload64, sizeof(payload64));
    std::memcpy(out.data() + entry + 16, &zero32, sizeof(zero32));
    std::memcpy(out.data() + entry + 40, &kUncompressedFlags, sizeof(kUncompressedFlags));
    std::memcpy(out.data() + entry + 56, &zero64, sizeof(zero64));

    const uint64_t outer = finalSize - kOuterHeader;
    std::memcpy(out.data() + 8, &outer, sizeof(outer));
    return true;
}

// Kernel names confirm the descriptor family; the exact sm_89 PTX size
// identifies the temporal program itself.
const TemporalProfile* FindTemporalProfile(const uint8_t* fat, size_t fatSize)
{
    size_t entry = 0;
    if (!FindAdaPtxEntry(fat, fatSize, entry))
        return nullptr;

    const uint64_t raw = ReadU64(fat + entry + 56);
    for (const auto& profile : kTemporalProfiles)
    {
        if (raw == profile.ptxBytes)
            return &profile;
    }

    return nullptr;
}

bool PointsToCString(const uint8_t* base, size_t imageSize, uint64_t value, const char* expected)
{
    const auto start = reinterpret_cast<uintptr_t>(base);
    if (value < start || value >= start + imageSize)
        return false;

    const char* s = reinterpret_cast<const char*>(value);
    const size_t len = std::strlen(expected);
    if (value + len + 1 > start + imageSize)
        return false;

    return std::memcmp(s, expected, len + 1) == 0;
}

bool ReadRelativePointer(const uint8_t* base, size_t imageSize, const uint8_t* slot,
                         ptrdiff_t displacement, uint64_t& value)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(base);
    const uintptr_t end = start + imageSize;
    const uintptr_t address = reinterpret_cast<uintptr_t>(slot);
    uintptr_t field = address;

    if (displacement >= 0)
    {
        const auto distance = static_cast<uintptr_t>(displacement);
        if (distance > UINTPTR_MAX - address)
            return false;
        field = address + distance;
    }
    else
    {
        const auto distance = static_cast<uintptr_t>(-(displacement + 1)) + 1;
        if (distance > address)
            return false;
        field = address - distance;
    }

    if (field < start || field > end || sizeof(value) > end - field)
        return false;

    std::memcpy(&value, reinterpret_cast<const void*>(field), sizeof(value));
    return true;
}

struct Patch
{
    uint64_t* slot;
    uint64_t original;
};

std::vector<Patch> g_patches;
void* g_allocation = nullptr;

// Replaces the temporal kernel fatbin in every descriptor that references it,
// redirecting all of them because we cannot tell which one the runtime picks.
unsigned int ApplyMidpointFix(HMODULE module, std::string& detail)
{
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        detail = "module is not a PE image";
        return 0;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        detail = "module has no NT headers";
        return 0;
    }

    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    const auto start = reinterpret_cast<uintptr_t>(base);

    std::vector<uint64_t*> slots;
    const uint8_t* fat = nullptr;
    size_t fatSize = 0;
    const TemporalProfile* selectedProfile = nullptr;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0)
            continue;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
            continue;

        uint8_t* sec = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;

        for (size_t off = 0; off + sizeof(uint64_t) <= size; off += sizeof(uint64_t))
        {
            uint64_t value = 0;
            std::memcpy(&value, sec + off, sizeof(value));

            if (value < start || imageSize < kOuterHeader || value > start + imageSize - kOuterHeader)
                continue;

            const auto* candidate = reinterpret_cast<const uint8_t*>(value);
            if (ReadU32(candidate) != kFatbinMagic)
                continue;

            const TemporalProfile* nameProfile = nullptr;
            for (const auto& profile : kTemporalProfiles)
            {
                uint64_t entryName = 0;
                uint64_t descName = 0;

                if (!ReadRelativePointer(base, imageSize, sec + off, profile.entryNameOffset, entryName) ||
                    !ReadRelativePointer(base, imageSize, sec + off, profile.descriptorNameOffset, descName))
                    continue;

                if (PointsToCString(base, imageSize, entryName, profile.entryName) &&
                    PointsToCString(base, imageSize, descName, profile.descriptorName))
                {
                    nameProfile = &profile;
                    break;
                }
            }

            if (nameProfile == nullptr)
                continue;

            const uint64_t declared = ReadU64(candidate + 8);
            if (declared > static_cast<uint64_t>((std::numeric_limits<size_t>::max)() - kOuterHeader))
                continue;

            const size_t total = static_cast<size_t>(declared) + kOuterHeader;
            if (total < 1024 || total > (16u << 20))
                continue;
            if (total > start + imageSize - value)
                continue;

            const auto* fatProfile = FindTemporalProfile(candidate, total);
            if (fatProfile == nullptr || fatProfile != nameProfile)
                continue;

            if (fat == nullptr)
            {
                fat = candidate;
                fatSize = total;
                selectedProfile = fatProfile;
            }
            else if (candidate != fat)
            {
                continue; // a second temporal-looking kernel; leave it alone
            }

            slots.push_back(reinterpret_cast<uint64_t*>(sec + off));
        }
    }

    if (fat == nullptr || slots.empty() || selectedProfile == nullptr)
    {
        detail = "no supported temporal-kernel descriptor found";
        return 0;
    }

    std::vector<uint8_t> rebuilt;
    std::string why;
    if (!BuildTemporalFatbin(fat, fatSize, *selectedProfile, rebuilt, why))
    {
        detail = why;
        return 0;
    }

    void* mem = VirtualAlloc(nullptr, rebuilt.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (mem == nullptr)
    {
        detail = "allocation failed";
        return 0;
    }
    std::memcpy(mem, rebuilt.data(), rebuilt.size());

    for (uint64_t* slot : slots)
    {
        if (!IsInsideSection(module, reinterpret_cast<uintptr_t>(slot), sizeof(uint64_t), nullptr))
            continue;

        DWORD oldProtect = 0;
        if (VirtualProtect(slot, sizeof(uint64_t), PAGE_READWRITE, &oldProtect) == 0)
            continue;

        g_patches.push_back({ slot, *slot });
        *slot = reinterpret_cast<uint64_t>(mem);

        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(uint64_t), oldProtect, &ignored);
    }

    if (g_patches.empty())
    {
        VirtualFree(mem, 0, MEM_RELEASE);
        detail = "no descriptor slot was writable";
        return 0;
    }

    g_allocation = mem;
    detail = std::format("redirected {} {} descriptor(s) from a {}-byte fatbin to a {}-byte temporal-corrected rebuild",
                         g_patches.size(), selectedProfile->descriptorName, fatSize, rebuilt.size());
    return static_cast<unsigned int>(g_patches.size());
}
} // namespace midpoint

std::string Hex(const uint8_t* bytes, size_t count)
{
    std::string out;

    for (size_t i = 0; i < count; ++i)
        out += std::format("{}{:02X}", i == 0 ? "" : " ", bytes[i]);

    return out;
}

// Rewrites count and neutralises the architecture clamp, so MultiFrameCountMax is published as five.
bool PatchAdvertise(HMODULE module)
{
    if (const auto at309 = UniqueAddress(module, kAdvertisePattern309); at309 != 0)
    {
        // The jl is a rel32, six bytes.
        const auto branchAt = at309 + 6;
        const uint8_t nop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x90 };

        LOG_INFO("MFG unlock: advertise (310.9) at {:X}, jl {} -> {}", at309,
                 Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)));

        return ApplyPatches(module, { { branchAt, std::vector<uint8_t>(nop, nop + sizeof(nop)) } });
    }

    const auto address = UniqueAddress(module, kAdvertisePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the advertise signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the r8d immediate, and the cmovl.
    const auto countAt = address + 7;
    const auto cmovAt = address + 17;

    const uint8_t count[] = { kMaxGeneratedFrames };
    const uint8_t nop[] = { 0x0F, 0x1F, 0x40, 0x00 };

    LOG_INFO("MFG unlock: advertise at {:X}, count {} -> {}, cmovl {} -> {}", address,
             *(const uint8_t*) countAt, kMaxGeneratedFrames, Hex((const uint8_t*) cmovAt, sizeof(nop)),
             Hex(nop, sizeof(nop)));

    return ApplyPatches(module, { { countAt, std::vector<uint8_t>(count, count + sizeof(count)) },
                                  { cmovAt, std::vector<uint8_t>(nop, nop + sizeof(nop)) } });
}

// Drops the Ada branch and raises the accepted count, so a request for five is not rejected.
bool PatchValidate(HMODULE module)
{
    if (const auto at309 = UniqueAddress(module, kValidatePattern309); at309 != 0)
    {
        // setae al -> mov al, 1, so the flag is set whatever the architecture reports.
        const auto setAt = at309 + 5;
        const uint8_t always[] = { 0xB0, 0x01, 0x90 };

        LOG_INFO("MFG unlock: validate (310.9) at {:X}, setae {} -> {}", at309,
                 Hex((const uint8_t*) setAt, sizeof(always)), Hex(always, sizeof(always)));

        return ApplyPatches(module, { { setAt, std::vector<uint8_t>(always, always + sizeof(always)) } });
    }

    const auto address = UniqueAddress(module, kValidatePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the validate signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the jl, and the immediate of the count test behind it.
    const auto branchAt = address + 5;
    const auto countAt = address + 9;

    const uint8_t nop[] = { 0x90, 0x90 };
    const uint8_t count[] = { kMaxGeneratedFrames };

    LOG_INFO("MFG unlock: validate at {:X}, jl {} -> {}, count {} -> {}", address,
             Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)), *(const uint8_t*) countAt,
             kMaxGeneratedFrames);

    return ApplyPatches(module, { { branchAt, std::vector<uint8_t>(nop, nop + sizeof(nop)) },
                                  { countAt, std::vector<uint8_t>(count, count + sizeof(count)) } });
}

// Gives Ada the Blackwell kernels the module already carries.
constexpr uint32_t kArchAda = 89;
constexpr uint32_t kArchBlackwell = 120;

// No such shader model. Parks an image where nothing will ask for it.
constexpr uint32_t kArchParked = 122;

// Offsets inside a fatbin image header: payload length, and the architecture the image answers for.
constexpr size_t kImagePayloadSize = 8;
constexpr size_t kImageArch = 28;

unsigned int RewriteBlackwellKernels(HMODULE module)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    const auto section = IMAGE_FIRST_SECTION(nt);

    const uint8_t magic[] = { 0x50, 0xED, 0x55, 0xBA };
    unsigned int rewritten = 0;

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = section[i];

        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;

        uint8_t* start = base + s.VirtualAddress;
        uint8_t* end = start + s.Misc.VirtualSize;

        for (uint8_t* c = std::search(start, end, magic, magic + sizeof(magic)); c < end;
             c = std::search(c + 1, end, magic, magic + sizeof(magic)))
        {
            if (end - c < 16)
                break;

            const auto headerSize = *reinterpret_cast<const uint16_t*>(c + 6);
            const auto fatSize = *reinterpret_cast<const uint64_t*>(c + 8);

            if (headerSize != 0x10 || fatSize == 0 || fatSize > (uint64_t) (end - c - 16))
                continue;

            uint8_t* blackwell = nullptr;
            size_t blackwellHeader = 0;
            size_t blackwellPayload = 0;
            std::vector<uint8_t*> ada;
            bool valid = true;

            for (uint8_t* image = c + 16; image < c + 16 + fatSize;)
            {
                const auto remaining = (uint64_t) (c + 16 + fatSize - image);
                if (remaining < kImageArch + sizeof(uint32_t))
                {
                    valid = false;
                    break;
                }
                const auto kind = *reinterpret_cast<const uint16_t*>(image);
                const auto imageHeader = *reinterpret_cast<const uint32_t*>(image + 4);
                const auto payload = *reinterpret_cast<const uint64_t*>(image + kImagePayloadSize);
                const auto arch = *reinterpret_cast<const uint32_t*>(image + kImageArch);

                if (imageHeader < kImageArch + sizeof(uint32_t) || imageHeader > remaining ||
                    payload == 0 || payload > remaining - imageHeader)
                {
                    valid = false;
                    break;
                }

                // kind 1 is PTX, 2 is a cubin. Only the PTX can be retargeted; the cubin is parked.
                if (kind == 1 && arch == kArchBlackwell)
                {
                    blackwell = image;
                    blackwellHeader = imageHeader;
                    blackwellPayload = payload;
                }
                else if (arch == kArchAda)
                {
                    ada.push_back(image);
                }

                image += imageHeader + payload;
            }

            if (!valid || blackwell == nullptr || ada.empty())
                continue;

            const char from[] = ".target sm_120";
            const char to[] = ".target sm_89 ";
            static_assert(sizeof(from) == sizeof(to), "the directive rewrite must not change length");

            uint8_t* body = blackwell + blackwellHeader;
            uint8_t* bodyEnd = body + blackwellPayload;
            auto at = std::search(body, bodyEnd, from, from + sizeof(from) - 1);

            if (at == bodyEnd)
                continue;

            const uint32_t ada89 = kArchAda;
            const uint32_t parked = kArchParked;

            // Prepare one complete container first. A failed protection change must not leave
            // its PTX target and architecture headers disagreeing, or count a partial rewrite.
            std::vector<uint8_t> patched(c, c + 16 + fatSize);
            std::memcpy(patched.data() + (at - c), to, sizeof(to) - 1);
            std::memcpy(patched.data() + (blackwell + kImageArch - c), &ada89, sizeof(ada89));

            for (uint8_t* image : ada)
                std::memcpy(patched.data() + (image + kImageArch - c), &parked, sizeof(parked));

            if (ApplyPatches(module, { { reinterpret_cast<uintptr_t>(c), patched } }))
                ++rewritten;
        }
    }

    LOG_INFO("MFG unlock: {} kernel containers answer Ada with the Blackwell image", rewritten);

    return rewritten;
}
} // namespace

bool MfgUnlock::IsSupportedGpu()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    if (gpu.vendorId != VendorId::Nvidia)
        return false;
    if (gpu.nvidiaArchInfo.architecture_id != 0 && gpu.nvidiaArchInfo.architecture_id != NV_GPU_ARCHITECTURE_AD100)
        return false;
    return true;
}

void MfgUnlock::TryApply(HMODULE requestedModule)
{
    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() &&
        !Config::Instance()->FGDLSSGUnlockAdaMFG.value_or_default())
        return;

    // The kernel retarget is Ada-specific. Do not patch Ampere/Turing or change Blackwell's working path.
    if (!IsSupportedGpu())
        return;

    // Latches once nvngx_dlssg.dll is present; before that every call rescans for it.
    static bool snippetDone = false;

    if (!snippetDone)
    {
        if (requestedModule != nullptr && State::Instance().nativeDlssgModule != nullptr &&
            requestedModule != State::Instance().nativeDlssgModule)
        {
            LOG_WARN("MFG unlock: refusing to patch a module that is not the native game's DLSS-G");
            return;
        }

        auto module = requestedModule ? requestedModule : State::Instance().nativeDlssgModule;
        if (module == nullptr)
            module = GetModuleHandleW(L"nvngx_dlssg.dll");
        if (module == nullptr)
            return;

        // Verify this is actually a DLSS-G module before inspecting or latching
        wchar_t modPath[MAX_PATH] = {};
        if (GetModuleFileNameW(module, modPath, MAX_PATH))
        {
            std::wstring p(modPath);
            std::transform(p.begin(), p.end(), p.begin(), ::towlower);
            if (p.find(L"dlssg") == std::wstring::npos)
                return;
        }

        snippetDone = true;
        g_status.ModuleFound = true;
        g_status.SnippetVersion = ModuleVersion(module);

        // Validate both gates before touching either. Ambiguous/unknown versions remain unmodified.
        const bool knownGates =
            (UniqueAddress(module, kAdvertisePattern309) && UniqueAddress(module, kValidatePattern309)) ||
            (UniqueAddress(module, kAdvertisePattern) && UniqueAddress(module, kValidatePattern));
        if (!knownGates)
        {
            LOG_WARN("MFG unlock: unsupported or ambiguous DLSSG {} signatures; left unchanged",
                     g_status.SnippetVersion);
            return;
        }

        if (Config::Instance()->FGDLSSGAdaBlackwellKernels.value_or(true))
        {
            g_status.KernelsRewritten = RewriteBlackwellKernels(module);

            // No Blackwell image to retarget: correct the Ada kernel's blend weights directly so
            // 3X/4X produce frames at their own temporal positions instead of repeated midpoints.
            if (g_status.KernelsRewritten == 0)
            {
                std::string detail;
                g_status.TemporalFixPatches = midpoint::ApplyMidpointFix(module, detail);
                g_status.TemporalFixDetail = detail;

                if (g_status.TemporalFixPatches > 0)
                    LOG_INFO("MFG unlock: temporal midpoint fix applied: {}", detail);
                else
                    LOG_WARN("MFG unlock: temporal midpoint fix unavailable: {}", detail);
            }
        }

        if (g_status.KernelsRewritten == 0 && g_status.TemporalFixPatches == 0)
        {
            LOG_WARN("MFG unlock: no compatible interpolation kernels; frame-count gates left unchanged");
            return;
        }

        const bool advertise = PatchAdvertise(module);
        const bool validate = PatchValidate(module);
        g_status.AdvertiseMatched = advertise;
        g_status.ValidateMatched = validate;

        if (advertise && validate)
            LOG_INFO("MFG unlock: nvngx_dlssg.dll patched for {} generated frames", kMaxGeneratedFrames);
        else
            LOG_WARN("MFG unlock: nvngx_dlssg.dll incomplete, advertise {}, validate {}", advertise, validate);
    }
}

unsigned int MfgUnlock::UnlockedMax()
{
    const auto& status = LastStatus();
    const bool contentReady = status.KernelsRewritten > 0 || status.TemporalFixPatches > 0;

    return status.AdvertiseMatched && status.ValidateMatched && contentReady ? kMaxGeneratedFrames : 0;
}

bool MfgUnlock::Pending()
{
    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() &&
        !Config::Instance()->FGDLSSGUnlockAdaMFG.value_or_default())
        return false;

    if (g_status.ModuleFound)
        return false;

    if (!IsSupportedGpu())
        return false;

    return true;
}

const MfgUnlock::Status& MfgUnlock::LastStatus() { return g_status; }
