#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace AmpereMfgLoader
{
struct Status
{
    bool Enabled = false;
    bool DllFound = false;          // Pinned sdli1995 version.dll found beside the game executable
    bool IniWritten = false;        // sdli1995 dlssg_sm86.ini written beside the game executable
    bool DllLoaded = false;         // The pinned proxy is loaded from the game executable folder
    bool ExperimentalRequested = false;
    bool ExperimentalPatched = false;
    bool ExperimentalFallback = false;
    std::string ExperimentalDetail;
    bool Native6XRequested = false;
    bool Native6XRuntimeFound = false;
    bool Native6XActive = false;
    std::string Native6XDetail;
    int MaxInterpolationCount = 1; // 1=2X, 3=4X, 5=6X
    std::string ErrorMessage;
};

Status LastStatus();
int MaxInterpolationCount();
void TrySetup();
void WriteIniFiles();

inline std::string FormatNativeIniContent(int maxFrames, const std::string& router = "SM86", int logLevel = 1)
{
    if (maxFrames < 1 || maxFrames > 5)
        maxFrames = 5;

    const std::string validRouter = (router == "SM75" || router == "SM86") ? router : "SM86";
    const int validLogLevel = (logLevel >= 0 && logLevel <= 3) ? logLevel : 1;

    std::ostringstream ss;
    ss << "; sdli1995 0.3.5 native runtime. Restart the game after changing this file.\n";
    ss << "[General]\nEnabled=1\n\n";
    ss << "[FrameGeneration]\nOptimized=1\nMaxGeneratedFrames=" << maxFrames << "\n\n";
    ss << "[Compatibility]\nRouter=" << validRouter << "\n\n";
    ss << "[Logging]\nLevel=" << validLogLevel << "\nDirectory=dlssg_sm86\\logs\n\n";
    ss << "[Runtime]\nMode=Bundled\nCacheDirectory=\n";
    return ss.str();
}

inline bool IsTuringArch(uint32_t archId)
{
    return (archId == 0x00000160) || ((archId & 0xFFF0) == 0x0160);
}

inline bool IsAmpereArch(uint32_t archId)
{
    return (archId == 0x00000170) || ((archId & 0xFFF0) == 0x0170);
}

inline std::string ResolveRouter(uint32_t archId, const std::string& gpuName = "")
{
    if (IsTuringArch(archId))
        return "SM75";
    if (IsAmpereArch(archId))
        return "SM86";
    if (gpuName.find("RTX 20") != std::string::npos || gpuName.find("GTX 16") != std::string::npos ||
        gpuName.find("Turing") != std::string::npos)
        return "SM75";
    return "SM86";
}

std::string ResolveRouter();
std::string GenerateIniContent();
std::string ResolveAutoKernelImage();
} // namespace AmpereMfgLoader
