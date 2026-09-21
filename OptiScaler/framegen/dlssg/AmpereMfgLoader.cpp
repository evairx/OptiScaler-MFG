#include "pch.h"

#include "AmpereMfgLoader.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <misc/IdentifyGpu.h>
#include <proxies/Ntdll_Proxy.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>

#pragma comment(lib, "bcrypt.lib")

namespace AmpereMfgLoader
{
namespace
{
Status s_status;
bool s_setupAttempted = false;
std::recursive_mutex s_mutex;

constexpr std::string_view kSdli035Hash =
    "c3934a09399f022504227c72df0bf8c0de55f9a08880dddde898c5262cefa838";

std::string Sha256File(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0)
    {
        std::array<uint8_t, 1 << 16> buffer {};
        bool ok = true;
        while (file && ok)
        {
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read > 0 && BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0) < 0)
                ok = false;
        }

        std::array<uint8_t, 32> digest {};
        if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0)
        {
            result.reserve(digest.size() * 2);
            for (const auto byte : digest)
                result += std::format("{:02x}", byte);
        }

        BCryptDestroyHash(hash);
    }

    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

int RequestedMaxFrames()
{
    auto* cfg = Config::Instance();
    int maxFrames = cfg->FGDLSSGAmpereMfgMaxFrames.value_or_default();
    if (cfg->FGDLSSGOverrideInterpolationCount.has_value())
        maxFrames = std::max(maxFrames, cfg->FGDLSSGOverrideInterpolationCount.value());

    if (maxFrames <= 0 || maxFrames > 5)
        maxFrames = 5;
    return maxFrames;
}

bool IsLoadedFrom(const HMODULE module, const std::filesystem::path& expected)
{
    if (module == nullptr)
        return false;

    wchar_t path[MAX_PATH] {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return false;

    auto loaded = std::filesystem::path(std::wstring(path, length)).lexically_normal().wstring();
    auto wanted = expected.lexically_normal().wstring();
    std::transform(loaded.begin(), loaded.end(), loaded.begin(), ::towlower);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), ::towlower);
    return loaded == wanted;
}
} // namespace

Status LastStatus()
{
    std::lock_guard lock(s_mutex);
    return s_status;
}

int MaxInterpolationCount()
{
    std::lock_guard lock(s_mutex);
    return std::clamp(s_status.MaxInterpolationCount, 1, 5);
}

std::string ResolveAutoKernelImage()
{
    return "PTX";
}

std::string ResolveRouter()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return ResolveRouter(static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id), gpu.name);
}

std::string GenerateIniContent()
{
    return FormatNativeIniContent(RequestedMaxFrames(), ResolveRouter(), 1);
}

void WriteIniFiles()
{
    const auto iniPath = Util::ExePath().parent_path() / L"dlssg_sm86.ini";
    try
    {
        std::ofstream iniFile(iniPath, std::ios::out | std::ios::trunc);
        if (iniFile.is_open())
        {
            iniFile << GenerateIniContent();
            iniFile.close();
        }
    }
    catch (const std::exception& ex)
    {
        LOG_DEBUG("AmpereMfgLoader: Could not write sdli1995 INI {}: {}", wstring_to_string(iniPath.wstring()),
                  ex.what());
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
    if (!cfg->FGDLSSGAmpereNative6XRuntime.value_or_default())
        return;

    s_status.Enabled = true;

    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    if (gpu.vendorId != VendorId::Nvidia)
    {
        s_status.ErrorMessage = "sdli1995 MFG requires an NVIDIA GPU.";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    const uint32_t archId = static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id);
    const bool isAmpere = IsAmpereArch(archId) || gpu.name.find("RTX 30") != std::string::npos;
    const bool isTuring = IsTuringArch(archId) || gpu.name.find("RTX 20") != std::string::npos ||
                          gpu.name.find("GTX 16") != std::string::npos;
    if (!isAmpere && !isTuring)
    {
        s_status.ErrorMessage = "sdli1995 MFG requires an RTX 20 or RTX 30 GPU.";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    const auto proxyPath = Util::ExePath().parent_path() / L"version.dll";
    std::error_code error;
    if (!std::filesystem::exists(proxyPath, error))
    {
        s_status.ErrorMessage = "sdli1995 version.dll is missing beside the game executable.";
        LOG_WARN("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    if (Sha256File(proxyPath) != kSdli035Hash)
    {
        s_status.ErrorMessage = "version.dll beside the game executable is not the pinned sdli1995 0.3.5 build.";
        LOG_WARN("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    s_status.DllFound = true;
    s_status.Native6XRequested = true;
    s_status.Native6XRuntimeFound = true;
    s_status.MaxInterpolationCount = 5;
    s_status.Native6XDetail = "sdli1995 0.3.5 primary game proxy, 6X ceiling";

    // The proxy must be beside the game executable. Do not load the nested copy from OptiScaler:
    // that creates a second loader and prevents the game from using sdli1995 as its provider.
    WriteIniFiles();
    s_status.IniWritten = true;

    NtdllProxy::Init();
    HMODULE proxy = GetModuleHandleW(L"version.dll");
    if (!IsLoadedFrom(proxy, proxyPath))
        proxy = NtdllProxy::LoadLibraryExW_Ldr(proxyPath.c_str(), nullptr, 0);

    if (!IsLoadedFrom(proxy, proxyPath))
    {
        s_status.ErrorMessage = "sdli1995 version.dll was found but could not be loaded as the primary proxy.";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return;
    }

    s_status.DllLoaded = true;
    s_status.Native6XActive = true;
    s_status.ErrorMessage.clear();
    LOG_INFO("AmpereMfgLoader: {}", s_status.Native6XDetail);
}

} // namespace AmpereMfgLoader
