#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <atomic>

namespace AdaMFGUnlock {

struct GateSite {
    uint8_t* address;
    uint8_t original;
};

struct FatbinPatch {
    uint64_t* slot;
    uint64_t original;
};

struct FlipSite {
    uint8_t* address;
    uint8_t original[7];
    uint8_t length;
};

class Manager {
private:
    // This is an opt-in compatibility patch. Never touch a game's DLSS-G
    // provider until the user explicitly enables MFG unlock.
    static inline std::atomic_bool s_enabled{ false };
    static inline std::atomic_bool s_archPatched{ false };
    static inline std::atomic_bool s_midpointPatched{ false };
    static inline std::atomic_bool s_flipMeteringPatched{ false };
    static inline std::atomic_bool s_ceilingPatched{ false };

    static inline std::vector<GateSite> s_gateSites;
    static inline std::vector<FatbinPatch> s_midpointPatches;
    static inline std::vector<FlipSite> s_flipSites;
    static inline void* s_fatbinAllocation = nullptr;

    static inline uint8_t* s_ceilingSite = nullptr;
    static inline uint8_t s_ceilingOriginal = 0;
    static inline uint8_t s_ceilingCmovOriginal = 0;
    static inline std::atomic_uint32_t s_ceilingCompiled { 0 };
    static inline std::atomic_uint32_t s_ceilingEffective { 0 };

    static bool ModuleContains(HMODULE mod, const char* needle, size_t needle_len);
    static bool HasKnownDlssgPath(HMODULE mod);
    static bool IsDlssgProvider(HMODULE mod);

    static bool WriteFlipSite(uint8_t* at, const uint8_t* bytes, size_t length);
    static bool PatchArchGatesInModule(HMODULE mod);
    static bool PatchMidpointInModule(HMODULE mod);
    static bool PatchFlipMeteringInModule(HMODULE mod);
    static bool PatchFrameCountCeiling(HMODULE mod);

public:
    static bool IsEnabled();
    static void SetEnabled(bool enabled);

    static bool IsArchPatched();
    static bool IsMidpointPatched();
    static bool IsFlipMeteringPatched();
    static bool IsCeilingPatched();
    static bool IsSupportedGpu();
    static bool IsReadyForMultiFrame();
    static bool IsPacingReady();
    static uint32_t GetCeilingCompiled();
    static uint32_t GetCeilingEffective();

    // Triggered when a module is loaded or during bootstrap
    static void OnModuleLoaded(HMODULE mod, const wchar_t* path = nullptr);
    static void CheckAndPatchAll();

    // Specific module hooks
    static bool PatchNvngxDlssg(HMODULE dlssgModule);
    static bool PatchDlssgPlugin(HMODULE pluginModule);

    static void RestoreAll();
};

} // namespace AdaMFGUnlock
