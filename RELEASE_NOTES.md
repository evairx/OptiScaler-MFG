# evairx/OptiScalerMFG dev-6

Normal release (not a pre-release). CI builds Windows x64 only; see Validation below for the author's GPU smoke test.

## NVIDIA DLSS MFG unlock, native (RTX 40 / Ada)
- Blackwell (sm_120) kernels retargeted to Ada (sm_89), giving the game's own DLSS-G native multi-frame generation up to 6X.
- Transactional DLSSG 310.7/310.9 gates with rollback, temporal midpoint PTX correction, boundary artifact mitigation (modes 0/1/2, default balanced) and a silhouette guard.

## RTX 30 / 20 (Ampere / Turing)
- Hash-pinned SM86 X5/X6 loader with the tested X4 fallback path.
- Experimental sdli1995 0.3.x runtime support (native 6X, `[DLSSG] AmpereNative6XRuntime`, default off).

## Intel XeFG MFG
- Native 2X-6X unlock (five byte patches plus pacing per generated frame): `[XeFG] UnlockMFG`, `MaxInterpolatedFrames`, `ExtraPacing`.
- Menu options 2X-6X and the `XeFG` label.
- The game's DLSS-G is forced OFF when the output is XeFG/FSRFG, and swapchain recreation is fixed (RE Engine / Onimusha).

## DLSS Neural Rendering (DLSS 5 / NR)
- Full migration to wilsjo's architecture: D3D12 multipass pre/post, native Vulkan, D3D11 bridge, finished-picture, GpuLifetime, CompatibilityRuntime, NgxDiagnostics and MenuModel.
- `nvngx_dlssnr.dll` runtime (ShortFuse RTX 20/30/40) bundled in the package by CI (release `nr-runtime`).
- Neural Rendering menu restored in the left column.

## REFramework
- Native warning (toast plus menu header) when a RE Engine game lacks `dinput8.dll`; protected RE Engine titles need it to inject (tested on Onimusha: Way of the Sword).

## Upstream ports
- Scaled-frame freeze detection with false frames (MFG pacing above 2X), typed DXGI exports without LOG_FUNC (Win11 crash), GpuTime `_trigger` timing, FSRFG fixes (swapchain/device), and older UE compatibility in FindFilePath.

## Menu / unlocker
- Native DLSS MFG section shown only when FG Input and FG Output are `None`; FG selectors locked while the unlocker is active; correct save-and-restart prompt; `Enable DLSS MFG Unlocker` checkbox; noisy status messages removed.

## Versioning
- Builds identify as `evairx/OptiScalerMFG dev-N` (tag `dev-6`, asset `OptiScalerMFG_dev-6_<date>.7z`).

## Validation
- CI build: Windows x64. Author GPU smoke test: Onimusha: Way of the Sword (RE Engine) with REFramework - native RTX 40 MFG and XeFG OK, NR OK.
