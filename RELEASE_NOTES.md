# OptiScaler-MFG evairx/optiscaler_dev-2 (NO TESTED)

Untested development build. CI compiles Windows x64 only; nothing is runtime-verified on a GPU.

## New in dev-2
- **Native experimental XeFG MFG unlock (5X/6X)** — no external ASI. Menu toggle + `[XeFG] UnlockExperimental`; creation keeps the runtime limit, the dynamic setter applies the unlocked value and keeps the last accepted count if the runtime refuses. V-Sync recommended above 4X.
- **DLSS Neural Rendering ("DLSS 5") — D3D12 stage 1.** Native port of the NR pass from the GPL fork: runs over what the upscaler just wrote (native DLSS or OptiScaler's own upscalers), on the same command list, before frame generation consumes it. Adds the `[DlssNr]` config block and the Neural Rendering menu section. Needs the user-supplied `nvngx_dlssnr.dll` plus the bundled `nvngx.dll_dlssnr.dll` forwarder. Vulkan/bridges and the scan/compare tooling are not in this stage.

## Carried from dev-1
- Native NVIDIA MFG unlocker separated from autonomous FG: off by default, only offered for the game's own DLSS-G, mutually exclusive with OptiFG backends.
- Ada MFG: temporal midpoint correction + transactional patching with rollback, native-module-only targeting.
- Ampere/Turing: hash-pinned X5/X6 loader patcher with automatic fallback to the proven X4 path.
- Autonomous FG Output list now only exposes AMD FSR FG 2X and Intel XeSS 3 XeFG.
