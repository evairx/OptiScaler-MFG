#include "pch.h"

#include "AmpereMfgLoader.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <misc/IdentifyGpu.h>
#include <proxies/Ntdll_Proxy.h>

#include <fstream>
#include <sstream>
#include <mutex>

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

std::string GenerateIniContent()
{
    auto* cfg = Config::Instance();
    int maxFrames = cfg->FGDLSSGAmpereMfgMaxFrames.value_or_default();
    if (cfg->FGDLSSGOverrideInterpolationCount.has_value() &&
        cfg->FGDLSSGOverrideInterpolationCount.value() > maxFrames)
    {
        maxFrames = std::min(3, cfg->FGDLSSGOverrideInterpolationCount.value());
    }

    if (maxFrames <= 0 || maxFrames > 3)
        maxFrames = 3;

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

    // Generate and write companion dlssg_sm86.ini in all locations where dlssg_sm86.dll looks
    WriteIniFiles();
    s_status.IniWritten = true;

    // Load dlssg_sm86.dll
    NtdllProxy::Init();
    LOG_INFO("AmpereMfgLoader: Loading {}", wstring_to_string(dllPath.wstring()));
    HMODULE hMod = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);
    if (!hMod)
        hMod = LoadLibraryW(dllPath.c_str());

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
    LOG_INFO("AmpereMfgLoader: SM86 MFG loaded successfully from {}", wstring_to_string(dllPath.wstring()));
}

} // namespace AmpereMfgLoader
