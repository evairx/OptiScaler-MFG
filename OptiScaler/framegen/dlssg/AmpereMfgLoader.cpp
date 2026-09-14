#include "pch.h"

#include "AmpereMfgLoader.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <misc/IdentifyGpu.h>
#include <proxies/Ntdll_Proxy.h>

#include <bcrypt.h>

#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")

#ifndef NV_GPU_ARCHITECTURE_GA100
#define NV_GPU_ARCHITECTURE_GA100 0x00000170
#endif

namespace AmpereMfgLoader
{
namespace
{
Status s_status;
bool s_setupAttempted = false;
std::recursive_mutex s_mutex;
} // namespace

Status LastStatus()
{
    std::lock_guard lock(s_mutex);
    return s_status;
}

std::string ResolveAutoKernelImage()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    const bool onLinux = State::Instance().isRunningOnLinux || gpu.usesVkd3dProton;
    return ResolveAutoKernelImage(static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id), gpu.name, onLinux);
}

std::string ResolveRouter()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return ResolveRouter(static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id), gpu.name);
}

namespace
{
// Set when the experimental patch fails, so the companion INI keeps the proven X4 bound even
// though the config still asks for more until the user changes it.
int s_iniMaxFramesOverride = 0;

int RequestedMaxFrames()
{
    auto* cfg = Config::Instance();
    int maxFrames = cfg->FGDLSSGAmpereMfgMaxFrames.value_or_default();

    if (cfg->FGDLSSGOverrideInterpolationCount.has_value() &&
        cfg->FGDLSSGOverrideInterpolationCount.value() > maxFrames)
    {
        maxFrames = std::min(5, cfg->FGDLSSGOverrideInterpolationCount.value());
    }

    if (maxFrames <= 0 || maxFrames > 5)
        maxFrames = 3;

    return maxFrames;
}

bool ReadAllBytes(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;

    const auto size = file.tellg();
    if (size <= 0)
        return false;

    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}

std::string Sha256File(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;

    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0) >= 0)
    {
        bool ok = true;
        std::vector<uint8_t> buffer(1 << 16);

        while (file && ok)
        {
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();

            if (read > 0 && BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0) < 0)
                ok = false;
        }

        std::vector<uint8_t> digest(32);
        if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0)
        {
            hex.reserve(digest.size() * 2);
            for (const auto byte : digest)
                hex += std::format("{:02x}", byte);
        }

        BCryptDestroyHash(hash);
    }

    BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

// Native 0.2.4 loader monoliths, matched by source hash (the five proxy names ship the same
// program). Offsets and anchors come from the published experimental patch audit.
struct ExperimentalSource
{
    std::string_view sha256;
    size_t iniBoundOffset;
    size_t countBoundOffset;
    size_t messageOffset;
};

constexpr ExperimentalSource kExperimentalSources[] = {
    { "c844646d835a7b88ed1382eea80403d38b433f8ac09cf92581c73698c44ae7c2", 38195, 233316, 463912 }, // version.dll
    { "1004dd4ee0edbe4e1af4c8c7b30d4786bea0f5e7c0412566996b4c2543ae7e36", 38195, 234292, 470648 }, // winmm.dll
    { "ef3c3d49c5b5c8a17289c24da9b22885570793d72f3db628fa500f9efdb20489", 38195, 233252, 463480 }, // dinput8.dll
    { "1619839e4d1b6145ce9a587ba807f42e64f2b0984af9e81700d42ccf46ff7253", 38195, 233764, 467960 }, // winhttp.dll
    { "8d29eddbd7f1c3e272d07f94ab8812a80ef5b7aeb73923320bf9a432ddcf74c0", 38195, 233332, 464088 }, // dxgi.dll
};

constexpr size_t kExperimentalMessageLength = 37;
constexpr uint8_t kExperimentalMessageBefore[kExperimentalMessageLength] = {
    'M', 'a', 'x', 'G', 'e', 'n', 'e', 'r', 'a', 't', 'e', 'd', 'F', 'r', 'a', 'm', 'e', 's', ' ',
    'm', 'u', 's', 't', ' ', 'b', 'e', ' ', '1', ',', ' ', '2', ' ', 'o', 'r', ' ', '3', 0
};

// Builds a patched copy of a pinned loader. Never touches the original file; any anchor or hash
// mismatch fails closed so the caller keeps the proven X4 configuration.
std::filesystem::path TryBuildExperimentalLoader(const std::filesystem::path& dllPath, int maxFrames,
                                                 std::string& detail)
{
    if (maxFrames < 4 || maxFrames > 5)
    {
        detail = "not an experimental multiplier";
        return {};
    }

    const auto hash = Sha256File(dllPath);
    if (hash.empty())
    {
        detail = "could not hash the loader";
        return {};
    }

    const ExperimentalSource* source = nullptr;
    for (const auto& candidate : kExperimentalSources)
    {
        if (hash == candidate.sha256)
        {
            source = &candidate;
            break;
        }
    }

    if (source == nullptr)
    {
        detail = std::format("loader hash {} is not a pinned Native 0.2.4 build", hash.substr(0, 16));
        return {};
    }

    std::vector<uint8_t> bytes;
    if (!ReadAllBytes(dllPath, bytes) || bytes.size() < source->messageOffset + kExperimentalMessageLength)
    {
        detail = "could not read the loader";
        return {};
    }

    if (bytes[source->iniBoundOffset] != 0x03 || bytes[source->countBoundOffset] != 0x02 ||
        std::memcmp(bytes.data() + source->messageOffset, kExperimentalMessageBefore, kExperimentalMessageLength) != 0)
    {
        detail = "loader does not carry the expected instruction anchors";
        return {};
    }

    bytes[source->iniBoundOffset] = static_cast<uint8_t>(maxFrames);
    bytes[source->countBoundOffset] = static_cast<uint8_t>(maxFrames - 1);

    std::vector<uint8_t> message(kExperimentalMessageLength, 0);
    std::format_to(message.begin(), "MaxGeneratedFrames must be 1 to {}", maxFrames);
    std::memcpy(bytes.data() + source->messageOffset, message.data(), message.size());

    auto output = dllPath.parent_path() / (dllPath.stem().wstring() + std::format(L"_x{}.dll", maxFrames + 1));

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        detail = "could not write the patched loader";
        return {};
    }

    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();

    if (!out.good())
    {
        detail = "could not write the patched loader";
        return {};
    }

    detail = std::format("{} patched for {}X (source {})", wstring_to_string(output.filename().wstring()),
                         maxFrames + 1, hash.substr(0, 16));
    return output;
}

int EffectiveIniMaxFrames()
{
    const int requested = RequestedMaxFrames();
    return s_iniMaxFramesOverride > 0 ? s_iniMaxFramesOverride : requested;
}
} // namespace

std::string GenerateIniContent()
{
    auto* cfg = Config::Instance();
    int maxFrames = EffectiveIniMaxFrames();

    std::string kernelImg = cfg->FGDLSSGAmpereMfgKernelImage.value_or("PTX");
    if (kernelImg != "PTX" && kernelImg != "Cubin")
        kernelImg = "PTX";

    if (kernelImg == "Auto")
    {
        std::string resolved = ResolveAutoKernelImage();
        kernelImg = (resolved != "Auto") ? resolved : "PTX";
    }

    int hwBilinear = cfg->FGDLSSGAmpereMfgHardwareBilinear.value_or_default() ? 1 : 0;
    std::string router = ResolveRouter();
    int logLevel = 1;

    LOG_INFO("AmpereMfgLoader: Router selected: {} for GPU: {}, maxFrames: {}, kernel: {}",
             router, IdentifyGpu::getPrimaryGpu().name, maxFrames, kernelImg);

    return FormatIniContent(maxFrames, kernelImg, hwBilinear, router, logLevel);
}

void WriteIniFiles()
{
    auto* cfg = Config::Instance();
    auto basePath = Util::DllPath().parent_path();
    std::string content = GenerateIniContent();

    std::vector<std::filesystem::path> targets;
    auto dllPath = std::filesystem::path(cfg->MainDllPath.value_or(basePath.wstring())) /
                   L"dlssg_sm86" / L"dlssg_sm86.dll";
    targets.push_back(dllPath.parent_path() / L"dlssg_sm86.ini");
    targets.push_back(basePath / L"dlssg_sm86.ini");
    targets.push_back(basePath / L"OptiScaler" / L"dlssg_sm86.ini");
    targets.push_back(basePath / L"OptiScaler" / L"dlssg_sm86" / L"dlssg_sm86.ini");

    for (const auto& iniPath : targets)
    {
        try
        {
            std::filesystem::create_directories(iniPath.parent_path());
            std::ofstream iniFile(iniPath, std::ios::out | std::ios::trunc);
            if (iniFile.is_open())
            {
                iniFile << content;
                iniFile.close();
            }
        }
        catch (const std::exception& ex)
        {
            LOG_DEBUG("AmpereMfgLoader: Could not write companion INI {}: {}",
                      wstring_to_string(iniPath.wstring()), ex.what());
        }
    }
}

void TrySetup()
{
    std::lock_guard lock(s_mutex);
    if (s_setupAttempted)
        return;
    s_setupAttempted = true;

    auto* cfg = Config::Instance();
    if (!cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
        return;

    s_status.Enabled = true;

    // GPU guard: verify Nvidia Turing or Ampere architecture
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    if (gpu.vendorId != VendorId::Nvidia)
    {
        s_status.ErrorMessage = "SM86/SM75 MFG requires an NVIDIA GPU.";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    const uint32_t archId = static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id);
    const bool isAmpere = IsAmpereArch(archId) || (gpu.name.find("RTX 30") != std::string::npos);
    const bool isTuring = IsTuringArch(archId) || (gpu.name.find("RTX 20") != std::string::npos || gpu.name.find("GTX 16") != std::string::npos);

    if (!isAmpere && !isTuring)
    {
        s_status.ErrorMessage = std::format(
            "SM86/SM75 MFG requires an RTX 20 series (Turing) or RTX 30 series (Ampere) GPU. Detected arch 0x{:x} ({}).",
            archId, gpu.name);
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    // If Ada MFG unlock was set, disable it cleanly for RTX 30/20 to prevent conflict
    if (cfg->FGDLSSGAdaMfgUnlock.value_or_default() || cfg->FGDLSSGUnlockAdaMFG.value_or_default())
    {
        LOG_INFO("AmpereMfgLoader: Turing/Ampere GPU detected ({}), prioritizing SM86/SM75 MFG unlock", gpu.name);
        cfg->FGDLSSGAdaMfgUnlock.set_volatile_value(false);
        cfg->FGDLSSGUnlockAdaMFG.set_volatile_value(false);
        State::Instance().activeUnlockAdaMFG = false;
    }

    // Locate dlssg_sm86.dll
    auto basePath = Util::DllPath().parent_path();
    auto dllPath = std::filesystem::path(cfg->MainDllPath.value_or(basePath.wstring())) /
                   L"dlssg_sm86" / L"dlssg_sm86.dll";
    std::error_code fileError;
    if (!std::filesystem::exists(dllPath, fileError))
        dllPath = basePath / L"OptiScaler" / L"dlssg_sm86" / L"dlssg_sm86.dll";
    if (!std::filesystem::exists(dllPath, fileError))
    {
        dllPath = basePath / L"dlssg_sm86" / L"dlssg_sm86.dll";
    }
    if (!std::filesystem::exists(dllPath, fileError))
    {
        // Fallback: check directly beside OptiScaler DLL
        auto fallbackPath = basePath / L"dlssg_sm86.dll";
        if (std::filesystem::exists(fallbackPath, fileError))
        {
            dllPath = fallbackPath;
        }
        else
        {
            s_status.DllFound = false;
            s_status.ErrorMessage = "dlssg_sm86.dll not found in OptiScaler/dlssg_sm86/ or dlssg_sm86/ subfolders.";
            LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
            return;
        }
    }
    s_status.DllFound = true;

    // Experimental X5/X6: build a patched copy of a hash-pinned loader and point the sideload at
    // it. Any mismatch or failure falls back to the proven X4 configuration.
    auto loadPath = dllPath;
    const int requestedFrames = RequestedMaxFrames();

    if (requestedFrames > 3)
    {
        s_status.ExperimentalRequested = true;

        std::string patchDetail;
        auto patched = TryBuildExperimentalLoader(dllPath, requestedFrames, patchDetail);

        if (!patched.empty())
        {
            loadPath = patched;
            s_status.ExperimentalPatched = true;
            s_status.ExperimentalDetail = patchDetail;
            LOG_INFO("AmpereMfgLoader: experimental {}X loader ready: {}", requestedFrames + 1, patchDetail);
        }
        else
        {
            s_status.ExperimentalFallback = true;
            s_status.ExperimentalDetail = patchDetail;
            s_iniMaxFramesOverride = 3;
            LOG_WARN("AmpereMfgLoader: experimental {}X unavailable ({}); falling back to proven X4",
                     requestedFrames + 1, patchDetail);
        }
    }

    // Generate and write companion dlssg_sm86.ini in all locations where dlssg_sm86.dll looks
    WriteIniFiles();
    s_status.IniWritten = true;

    // Load the patched experimental loader when available, otherwise the stock one.
    NtdllProxy::Init();
    LOG_INFO("AmpereMfgLoader: Loading {}", wstring_to_string(loadPath.wstring()));
    HMODULE hMod = NtdllProxy::LoadLibraryExW_Ldr(loadPath.c_str(), NULL, 0);
    if (!hMod)
        hMod = LoadLibraryW(loadPath.c_str());

    if (!hMod)
    {
        DWORD err = GetLastError();
        s_status.DllLoaded = false;
        s_status.ErrorMessage = "Failed to load dlssg_sm86.dll (error code " + std::to_string(err) + ").";
        LOG_ERROR("AmpereMfgLoader: Failed to load dlssg_sm86.dll, error: {}", err);
        return;
    }

    s_status.DllLoaded = true;
    s_status.ErrorMessage.clear();
    LOG_INFO("AmpereMfgLoader: SM86 MFG loaded successfully from {}", wstring_to_string(loadPath.wstring()));
}

} // namespace AmpereMfgLoader
