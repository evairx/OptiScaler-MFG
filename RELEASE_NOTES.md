# OptiScaler-MFG evairx/optiscaler_dev-2 (NO TESTED)

Untested development build. CI compiles Windows x64 only; nothing is runtime-verified on a GPU.

## New in dev-2
- **Native XeFG MFG unlock (2X to 6X)** — no external ASI. Ports upstream's in-memory `libxess_fg.dll` patch set (five verified byte patches with rollback) plus per-generated-frame pacing, so Intel MFG no longer falls back to 2X on non-Intel adapters and 5X/6X stay selected. `[XeFG] UnlockMFG`, `MaxInterpolatedFrames` (default 5 = 6X) and `ExtraPacing`; the MFG menu now offers 2X-4X plus a Custom multiplier. V-Sync is required above 4X.
- **DLSS Neural Rendering ("DLSS 5") — D3D12 stage 1.** Native port of the NR pass from the GPL fork: runs over what the upscaler just wrote (native DLSS or OptiScaler's own upscalers), on the same command list, before frame generation consumes it. Adds the `[DlssNr]` config block and the Neural Rendering menu section. Ships the ShortFuse-compatible `nvngx_dlssnr.dll` (RTX 20/30/40) when available at build time, plus the `nvngx.dll_dlssnr.dll` forwarder. Vulkan/bridges and the scan/compare tooling are not in this stage.

## Carried from dev-1
- Native NVIDIA MFG unlocker separated from autonomous FG: off by default, only offered for the game's own DLSS-G, mutually exclusive with OptiFG backends.
- Ada MFG: temporal midpoint correction + transactional patching with rollback, native-module-only targeting.
- Ampere/Turing: hash-pinned X5/X6 loader patcher with automatic fallback to the proven X4 path.
- Autonomous FG Output list now only exposes AMD FSR FG 2X and Intel XeSS 3 XeFG.
