# OptiScaler-MFG evairx/optiscaler_dev-3 (NO TESTED)

Untested development build. CI compiles Windows x64 only; nothing is runtime-verified on a GPU.

## New in dev-3
- **XeFG**: the MFG selector now lists `2X`, `3X`, `4X`, `5X` and `6X` as named options (values above 6X remain in a Custom slot), and the FG output is labelled just `XeFG`.
- **Native NVIDIA MFG unlocker restored for the game's own DLSS-G.** dev-2 only registered a native `nvngx_dlssg.dll` when the load was not redirected, so games whose DLSS-G is redirected to OptiScaler's modern package (Silent Hill 2 ships 3.7.0) never showed *Enable MFG Unlocker* and the unlock never applied. The module is now registered in both cases, the menu section no longer demands `FG Input: DLSSG`, and the flow gate is "the game owns DLSS-G and OptiScaler has no FG output".
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
