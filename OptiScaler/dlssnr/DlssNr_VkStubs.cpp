// Placeholder Vulkan Neural Rendering API, used until the native Vulkan path is ported.
//
// The D3D12 NR pass and the menu are complete; the menu queries the Vulkan counters for its status
// line. These stubs keep the D3D12 build link-clean and report "not running", so the UI shows the
// Vulkan path as unavailable instead of breaking the build. Replace this file when
// DlssNrFeature_Vk.cpp and the Vulkan composition pass are ported.
#include "pch.h"

#include <shaders/dlssnr/DlssNr_Common.h>
#include <dlssnr/DlssNrFeature_Vk.h>

namespace DlssNr
{
bool IsRunningVk() { return false; }

const char* FailureReasonVk() { return "Vulkan Neural Rendering is not in this build"; }

unsigned long long FramesVk() { return 0; }

std::optional<double> LastGpuTimeVk() { return std::nullopt; }

bool ExposureOfferedVk() { return false; }

void ShutdownVk(bool) {}
} // namespace DlssNr
