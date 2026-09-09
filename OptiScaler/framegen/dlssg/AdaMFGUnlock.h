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

class Manager {
private:
    static inline std::atomic_bool s_enabled{ true };
    static inline std::atomic_bool s_archPatched{ false };
    static inline std::atomic_bool s_midpointPatched{ false };
    static inline std::atomic_bool s_flipMeteringPatched{ false };

    static inline std::vector<GateSite> s_gateSites;
    static inline std::vector<FatbinPatch> s_midpointPatches;
    static inline void* s_fatbinAllocation = nullptr;

    static bool PatchArchGatesInModule(HMODULE mod);
    static bool PatchMidpointInModule(HMODULE mod);
    static bool PatchFlipMeteringInModule(HMODULE mod);

public:
    static bool IsEnabled();
    static void SetEnabled(bool enabled);

    static bool IsArchPatched();
    static bool IsMidpointPatched();
    static bool IsFlipMeteringPatched();

    // Triggered when a module is loaded or during bootstrap
    static void OnModuleLoaded(HMODULE mod, const wchar_t* path = nullptr);
    static void CheckAndPatchAll();

    // Specific module hooks
    static bool PatchNvngxDlssg(HMODULE dlssgModule);
    static bool PatchDlssgPlugin(HMODULE pluginModule);

    static void RestoreAll();
};

} // namespace AdaMFGUnlock
