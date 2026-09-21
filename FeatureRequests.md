# Feature Requests

Community requests for this fork, with references to how the sibling
[OptiScaler-DLSSNR-PreSR-Multipass](https://github.com/ShyVortex/OptiScaler-DLSSNR-PreSR-Multipass)
/ [DLSS Unlocked](https://github.com/ShyVortex/dlss-unlocked) line handles the same problems.
Nothing here is a demand — it is a wish list from an RTX 20/30 user, plus the evidence behind it.

## 1. Reuse NR surfaces across DX12 format changes (cutscene stutter in FINAL FANTASY VII REBIRTH)

With Neural Rendering and **Generate model before upscale** enabled, FINAL FANTASY VII REBIRTH
hitches on **every cutscene camera cut**. One short cutscene produced ~30 history resets inside
two minutes, each one landing exactly on a camera change.

The log signature is the pair emitted by
[`ReleaseSurfacesIfFormatChanged`](OptiScaler/shaders/dlssnr/DlssNr_Dx12_Resources.cpp):

```text
DlssNr_Dx12::State::Run DLSS-NR: the game asked for a history reset (1 so far)
DlssNr_Dx12::State::ReleaseSurfacesIfFormatChanged DLSS-NR rebuilding surfaces: format 26 -> 10 (inject point changed)
DlssNr_Dx12::State::ReleaseSurfacesIfFormatChanged DLSS-NR rebuilding surfaces: format 10 -> 26 (inject point changed)
```

The game flips the observed colour format back and forth (26 ↔ 10) around each cut, so the surfaces
are dropped and reallocated over and over: `nr.output`, `nr.passScratch`, `nr.passClamp`,
`nr.colorCopy`, `nr.hdrCopy`, `nr.colorSmall`, `nr.outputNative` and `nr.activeColor`. That is eight
committed resources, several of them full-frame, released and rebuilt within a frame or two of each
other — and the flip-flop returns to the original format, so the first release was wasted work.

DLSS Unlocked fixed this by **caching the DX12 surfaces across format transitions** instead of
parking them, so a transition back to a format that was already allocated does not have to
reallocate: [dlss-unlocked#33](https://github.com/ShyVortex/dlss-unlocked/issues/33),
shipped as `DACFAEEE` "DLSS-NR: cache DX12 surfaces during format transitions" in
[backend v0.9.6](https://github.com/ShyVortex/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.9.6)
and confirmed fixed by the reporter.

Could the same caching be ported here? The state already parks resources through
`ParkNrResource` / `lifetime`, so a small format-keyed cache beside `State` may be enough.
A cheap fallback would be to keep the surfaces when the previous format is seen again.

## 2. FSR-based Multi Frame Generation as an output

[MFG_IMPLEMENTATION.md](MFG_IMPLEMENTATION.md) removes the interpolation backends that used to pose
as DLSS-G (DLSS Enabler / Artur, Nukem `dlssg-to-fsr3`, FFX, Combo), and keeps **FSR FG standalone**
and **Intel XeFG** as explicit outputs. Both of those are single generated frame per input frame.

DLSS Unlocked keeps the DLSS Enabler headless companion, so an FSR-based **multi**-frame path
(3X / 4X, and FFX/Combo mixing for higher multipliers) is still available there — and it turns out to
be genuinely useful in practice, including where the DLSS-G path cannot present extra frames and on
non-NVIDIA GPUs.

Requested here:

1. Re-expose an FSR-based MFG output (DLSS Enabler headless and/or FSR4-FG multi-frame) as a first-class
   choice, not as a DLSS-G fallback.
2. Allow it to be combined with the `dlssg_sm86` sidecar on RTX 20/30, or at least document the
   recommended pairing when the DLSS-G sidecar is unavailable in a given game.
3. Keep the current guarantee that FSR is never silently substituted when DLSS-G validation fails.

## 3. Smooth Motion

[`dlssg_for_sm86-MFG-version`](https://github.com/pipotoufikxyz-lgtm/dlssg_for_sm86-MFG-version)
— a Smooth Motion build of the same SM86/SM75 sidecar this fork already uses — ships
[Smooth Motion 1.4.1](https://github.com/pipotoufikxyz-lgtm/dlssg_for_sm86-MFG-version/releases/tag/SM),
and the same build adds X5 / X6 for SM86 and SM75.

The catch is that it is enabled outside the game: NVIDIA Profile Inspector, plus that package's own
proxy / ASI loader next to the real executable, which is easy to get wrong when OptiScaler already
owns the proxy name.

Requested here:

1. An `OptiScaler.ini` key and overlay toggle for Smooth Motion (no Profile Inspector round trip),
   e.g. under `[DLSSG]` next to the existing SM86 options:
   ```ini
   [DLSSG]
   AmpereMfgUnlock = true
   AmpereMfgSmoothMotion = auto   ; new: Smooth Motion pacing in the SM86/SM75 sidecar
   AmpereMfgMaxFrames = auto      ; allow X5 / X6 where the sidecar build supports them
   ```
2. Either ship the Smooth Motion build as an option, or document it as a supported drop-in for
   `OptiScaler/dlssg_sm86/`, including which proxy name to keep when `dxgi.dll` / `version.dll` is
   already taken by another mod.
3. A note on interactions with `AmpereMfgOptimized`, `AmpereMfgRouter` and `AmpereMfgSpoofArchToGame`,
   and whether Smooth Motion may be combined with Neural Rendering.

## Environment of the reporter

FINAL FANTASY VII REBIRTH (Steam) 1.0.0.5, Windows 11 25H2 (26200), NVIDIA GeForce RTX 3080 Ti 12GB,
driver 616.92, DLSS + MFG with `External=true` and the SM86 sidecar.
