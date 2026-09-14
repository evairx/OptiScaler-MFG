# OptiScaler-MFG evairx/optiscaler_dev-1 (NO TESTED)

**This is an untested development build.** CI compiles it for Windows x64, but nothing here
has been runtime-verified on a real GPU in a game. Behavior may change, break, or crash.
This build supersedes the older `v10.0.2-pre1` / `v10.0.2-pre3` test builds.

---

## Frame Generation architecture (breaking change)

- **Native NVIDIA MFG is an unlocker, not a generator.** It patches the game's own
  `nvngx_dlssg.dll` in memory; it does not synthesize frames like FSR FG or XeFG.
- The **MFG Unlocker is disabled by default** and only appears for the game's native
  DLSS-G path (`FG Input: DLSSG via Streamline` + `FG Output: None`). It can no longer be
  enabled from OptiFG configurations, and it is never applied to OptiScaler's own modules
  (OptiFG / OptiScaler DLSS-G copies) or to NVIDIA OTA model caches.
- **Mutual exclusion:** while the unlocker is enabled, the autonomous backends
  (OptiFG input, AMD FSR FG, Intel XeSS 3 XeFG) are disabled in the menu with an
  explanatory tooltip. Disable the unlocker to use autonomous frame generation again.
- **NVIDIA DLSSG removed from the autonomous FG Output list.** OptiFG / Streamline input
  configurations now only expose **AMD FSR FG 2X** and **Intel XeSS 3 XeFG** as outputs.
- The OptiFG DLSSG backend is hard-limited to a single generated frame (2X) and never
  drives Multi-Frame Generation.
- Overlay/OSD labels are explicit: `NVIDIA DLSSG OptiFG 2X`, `AMD FSR FG`,
  `Intel XeSS 3 XeFG`, `NVIDIA Native Streamline MFG`. No more "NVIDIA DLSS MFG" label on
  the autonomous backend.

## Ada / RTX 40 — MFG unlocker updated

- Gate patches for DLSS-G 310.x providers (advertise + capability validate) raise the
  generated-frame ceiling to 5 (6X total).
- **Transactional patching:** every byte write is validated against the module's PE
  sections first; a failure anywhere rolls the whole patch set back so the provider is
  never left half-patched.
- **Temporal midpoint correction (fallback).** When the Blackwell image cannot be
  retargeted for Ada, the Ada interpolation kernel PTX is rewritten so the blend weight
  comes from the kernel's own temporal parameter instead of the compiled-in `0.5`
  constant. This prevents 3X/4X from producing repeated midpoint frames.
  Ported and adapted from `mavismmg/MFGAdaUnlock-RenoDx` (MIT, itself based on
  dashdogy's RTX40MFG-Unlock).
- The unlocker only targets the native game module registered by the load hooks; model
  and OTA cache copies are excluded.

## Ampere / Turing (RTX 30 / RTX 20) — experimental X5/X6 with safe fallback

- New hash-pinned loader patcher for the Native 0.2.4 sidecar
  (`dlssg_sm86.dll`, five known source hashes): raises the INI parser bound and the
  evaluation count bound, and updates the error text, mirroring the published
  experimental X5/X6 audit.
- A **patched copy** (`dlssg_sm86_x5.dll` / `dlssg_sm86_x6.dll`) is written next to the
  original; the original file is never modified.
- If the loader hash or instruction anchors do not match, or writing fails, the loader
  **falls back to the proven X4 configuration** (`MaxGeneratedFrames=3`) and surfaces the
  reason in the menu.
- Menu selector extended to `5X` / `6X` (marked experimental). X5/X6 are capability
  limits: the game must request and present those multipliers. Like the upstream
  experimental packages, these paths are not GPU-validated.

## Intel XeSS 3 XeFG (all vendors)

- XeFG output supports a dynamic interpolation count up to the maximum reported by the
  runtime (2X–6X range when reported), with creation-time clamping and safe fallback when
  optional exports are missing.
- XeLL latency reduction is wired in when available and no longer blocks swapchain
  creation when unavailable.
- Runtime library paths are now independent (`XeSSPath`, `XeFGPath`, `XeLLPath`) so one
  library can no longer overwrite another.

## Fixes

- Build break: removed a duplicated local declaration in the FG menu.
- Native DLSS-G advertisement (`FrameGeneration.Available`) is no longer hidden when the
  game module is not registered yet.
- Fixed a stuck "save and restart" banner: the active unlocker state now latches when the
  native flow is actually observed.
- Streamline hooks only treat a real `nvngx_dlssg.dll` as the native module; paths under
  NVIDIA model/ProgramData caches are excluded.
- Ampere/Turing sideload no longer waits for the game to load DLSS-G before initializing.
- FSR FG reports its fixed 2X interpolation count instead of silently accepting any value.

## Not included in this build

- **DLSS Neural Rendering (NR / "DLSS 5" NR)** — tracked, not ported yet.
- **NVIDIA Smooth Motion** — not implemented upstream in OptiScaler; nothing to port.
- **Forced XeMFG X5/X6** via the external unlocker ASI — this build respects the runtime's
  reported maximum instead.
- Independent X5/X6 validation for RTX 20 through the separate SM75 binary package.

## Installation notes

- Back up your current configuration and sidecar DLLs before testing.
- The Ampere/Turing X5/X6 path needs an **unpatched, hash-matching** Native 0.2.4 loader
  in `OptiScaler/dlssg_sm86/`. Already-patched packages are rejected on purpose.
- Save settings and restart the game after changing frame generation or unlocker options.

## Credits

- `dashdogy/RTX40MFG-Unlock` — original Ada MFG research and midpoint diagnosis.
- `mavismmg/MFGAdaUnlock-RenoDx` (MIT) — temporal midpoint PTX fix and transactional
  patching approach.
- `sdli1995/dlssg_for_sm86` — SM86/SM75 sidecar loader.
- `pipotoufikxyz-lgtm` experimental X5/X6 audits — byte-level reference for the
  hash-pinned loader patches.
- `NVIDIAGameWorks/Streamline` and Intel XeSS SDK for the public interfaces.
