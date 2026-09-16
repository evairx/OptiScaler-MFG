# evairx/OptiScalerMFG dev-6

Work-in-progress base for the next integration round. CI compiles Windows x64 only; nothing is runtime-verified on a GPU.

## Planned for dev-6
- NR sync from wilsjo2 (release-v0.8.4 memory and pass controls, nr-game-validation retirement, starfield serialization, nr-direct-runtime, nr-init-diagnostics).
- Small upstream ports (pacing freeze detect for MFG above 2X, DXGI export hardening, _trigger reset, FSRFG checks, UE compat).
- Optional: mavismmg 1.0 boundary artifact mitigation and silhouette guard.
- Experimental: sdli1995 0.3.x for native 6X on RTX 20/30 via `[DLSSG] AmpereNative6XRuntime` (default off): a hash-pinned runtime dropped by hand in `OptiScaler/dlssg_sm86/` (0.3.1/0.3.0, `version.dll` / `dlssg_sm86.dll` / `dlssg_native_031.dll`) is used as-is, never patched, with `MaxGeneratedFrames=5` and the resolved Router; missing binary keeps the tested 0.2.4 X4/X5-X6 path intact.

# evairx/OptiScalerMFG dev-5

Normal release (not a pre-release). CI compiles Windows x64 only; nothing is runtime-verified on a GPU.

## New in dev-5
- **Fixed the save-and-restart prompt disappearing**: the unlocker state was being latched a frame after the toggle by the Streamline hooks, which erased the red prompt immediately. It is now only latched where the patch actually lands (module load for Ada, sidecar load for Ampere), so the prompt stays until the game is restarted.
- **Releases are normal releases from dev-5 on** (tag `dev-5`, asset `OptiScalerMFG_dev-5_<date>.7z`).

## New in dev-4
- **Menu**: turning *Enable DLSS MFG Unlocker* on or off now always shows the red "Save Settings and restart the game" prompt, and the status chatter (the green `MFG Active: nvngx_dlssg.dll ... unlocked`, the partial-match and the Ampere readiness lines) is gone. The unlocker state is latched once the patch lands, so the prompt only appears when a restart is really needed.
- **Versioning**: builds are named `evairx/OptiScalerMFG dev-N` (tag `dev-N`, asset `OptiScalerMFG_dev-N_<date>.7z`). No more `10.0.2` version prefix.

## New in dev-3
- **XeFG**: the MFG selector now lists `2X`, `3X`, `4X`, `5X` and `6X` as named options (values above 6X remain in a Custom slot), and the FG output is labelled just `XeFG`.
- **Native NVIDIA MFG unlocker restored for the game's own DLSS-G.** dev-2 only registered a native `nvngx_dlssg.dll` when the load was not redirected, so games whose DLSS-G is redirected to OptiScaler's modern package (Silent Hill 2 ships 3.7.0) never showed *Enable MFG Unlocker* and the unlock never applied. The module is now registered in both cases, the unlock is applied at module load like pre-1 (not only once the game enables DLSS-G), and the menu section appears whenever the game has its own DLSS-G and FG Input/FG Output are both `None`.
- **Menu flow for the unlocker**: renamed to *Enable DLSS MFG Unlocker*; while it is enabled the FG Input/FG Output selectors are locked out to avoid conflicts, and the native MFG section is hidden as soon as any FG input/output is selected.
- **Ampere/Turing**: the advertised multi-frame ceiling follows the SM86 X5/X6 patched-loader bound (up to 6X) instead of being clamped to 4X.
- **NR (DLSS 5)**: `nvngx_dlssnr.dll` (ShortFuse RTX 20/30/40) is bundled in the release package, fetched by CI from the `nr-runtime` release or the `NVNGX_DLSSNR_URL` repository variable.
- Branding: builds identify as `evairx/OptiScaler-MFG dev-3`.

## New in dev-2
- **Native XeFG MFG unlock (2X to 6X)** — no external ASI. Ports upstream's in-memory `libxess_fg.dll` patch set (five verified byte patches with rollback) plus per-generated-frame pacing, so Intel MFG no longer falls back to 2X on non-Intel adapters and 5X/6X stay selected. `[XeFG] UnlockMFG`, `MaxInterpolatedFrames` (default 5 = 6X) and `ExtraPacing`. V-Sync is required above 4X.
- **DLSS Neural Rendering ("DLSS 5") — D3D12 stage 1.** Native port of the NR pass from the GPL fork: runs over what the upscaler just wrote (native DLSS or OptiScaler's own upscalers), on the same command list, before frame generation consumes it. Adds the `[DlssNr]` config block and the Neural Rendering menu section. Vulkan/bridges and the scan/compare tooling are not in this stage.

## Carried from dev-1
- Native NVIDIA MFG unlocker separated from autonomous FG: off by default, only offered for the game's own DLSS-G, mutually exclusive with OptiFG backends.
- Ada MFG: temporal midpoint correction + transactional patching with rollback, native-module-only targeting.
- Ampere/Turing: hash-pinned X5/X6 loader patcher with automatic fallback to the proven X4 path.
- Autonomous FG Output list now only exposes AMD FSR FG 2X and Intel XeSS 3 XeFG.

