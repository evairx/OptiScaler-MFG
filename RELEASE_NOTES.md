# OptiScaler-MFG v10.0.1-pre-optifg-test (Pre-release)

Welcome to the dedicated test pre-release of **OptiScaler-MFG (v10.0.1-pre-optifg-test)**! 🚀 🎮

This build specifically addresses and fixes **OptiFG Frame Generation pacing, elimination of black flickering / flashing, swapchain VSync decoupling, and overlay accuracy** in games without native FG (e.g. *Running Train*):
- **Elimination of Black Flickering (Swapchain Buffer Alignment)**: Fixed rapid black strobe / flashing caused by swapchain buffer count mismatch and out-of-bounds array indexing in D3D12. DXGI swapchains now respect the game's native buffer count, ensuring all presented backbuffers have valid engine contents.
- **Swapchain V-Sync Decoupling**: Automatically unlocks swapchain presentation (`SyncInterval = 0`) when Frame Generation is active unless explicitly forced ON by the user. Prevents fixed refresh displays (60Hz / 75Hz / 80Hz) from locking total output and collapsing base rendered FPS down to 10 FPS at 6X!
- **Safe Presentation Flags**: Safeguards against invalid tearing flags on swapchains that don't support tearing, preventing `DXGI_ERROR_INVALID_CALL` dropped frames.
- **Reflex Sleep Stall Removal**: Removed synchronous `ReflexSleep` calls inside `PresentEnd`, eliminating presentation thread sleep stalls that caused severe input latency and microstutter.
- **Accurate FPS & Base FPS Overlay**: Fixed inverted display math in the in-game overlay. Correctly displays `Total FPS / Base FPS` with accurate frametimes per interpolated frame instead of dividing base framerate by multiplier.
- **Full Architecture Support**: Retains complete Ada Lovelace (RTX 40), Ampere (RTX 30), and Turing (RTX 20) MFG engines.

---

### 🌟 Major Highlights & New Features

#### 🧠 Intelligent Unified Multi-Frame Engine (RTX 20 / RTX 30 / RTX 40)
- **Automatic Architecture Detection**:
  - Detects your GPU model and architecture ID automatically on startup.
  - The menu shows your detected GPU architecture: `Ada (RTX 40)`, `Ampere (RTX 30)`, or `Turing (RTX 20)`.
  - Toggling **Unlock MFG** automatically engages the correct unlock path without requiring manual configuration or conflicting settings.
- **Ampere & Turing Native Sideloading (SM86 / SM75)**:
  - Sideloads `OptiScaler/dlssg_sm86/dlssg_sm86.dll` in a safe background worker outside `DllMain`.
  - **Elimination of 2X Lock on RTX 30 / RTX 20**:
    - Previously, Streamline queries reported `numFramesToGenerateMax = 1`, causing games like *Crimson Desert* to lock to 2X and report MFG as disabled.
    - OptiScaler now reports `numFramesToGenerateMax = 3` and `DLSSG.MultiFrameCountMax = 3` across Streamline hooks and NGX parameters.
    - Added **Multi-Frame Ratio** dropdown (`2X`, `3X`, `4X`) directly into the OptiScaler menu for RTX 30 / RTX 20, allowing players to force 3X or 4X in real time without restarting the game.
  - **Fluidity & Stutter Elimination**:
    - Companion `dlssg_sm86.ini` is now synchronized and written to the game root, DLL folder, and OptiScaler directory.
    - Defaults `KernelImage=PTX` to ensure driver JIT compilation matches the physical SM architecture, eliminating shader compile stalls and erratic FPS drops.
    - Added **Hardware Bilinear (Fast Sampling)** toggle in the menu for RTX 30 (SM86) to minimize GPU frame-generation latency.
    - Added **Quality Guard (Anti-Flicker)** for Ampere/Turing to prevent HUD separation glitches and flickering in 3X and 4X modes.
- **Ada Lovelace Native In-Memory Retargeting (RTX 40)**:
  - Preserves the proven zero-allocation Blackwell `sm_120` -> `sm_89` in-place kernel retargeting for ratios up to 6X.
  - Retains in-memory redirection of older game-bundled DLSS-G to OptiScaler's modern v310.7.129 package.
- **Definitive Multi-Frame Architecture (2X, 3X, 4X, 5X, 6X)**:
  - Fully incorporates and ports the ReShade `MFGAdaUnlock-RenoDx` engine directly into OptiScaler's native binary (`dxgi.dll`) without needing ReShade, external hooks, or sidecar add-ons.
  - **In-Memory Dynamic Redirection of DLSS-G & Streamline**:
    - When a game attempts to load an older bundled DLSS-G (such as v3.7.0 in *Silent Hill 2*), OptiScaler intercepts the `LoadLibrary` call in memory and automatically redirects it to OptiScaler's modern `nvngx_dlssg.dll` v310.7.129.0 package.
    - Completely eliminates the `partial match (Adv:0 Val:0 Kernels:0)` error and unlocks all 31 midpoint interpolation containers.
  - **In-Place Dynamic Kernel Retargeting (`sm_120` -> `sm_89`)**:
    - Rewrites Blackwell interpolation kernels to Ada Lovelace architecture directly in memory upon DLL loading.
    - Completely eliminates crashes when switching between 2X, 3X, 4X, and 6X modes.
  - **Zero-Flicker Midpoint Temporal Reconstruction**:
    - Decompresses Fatbin PTX and rewrites the interpolation kernel to inject dynamic temporal progress parameters (`%f134` and `%f136`) instead of the hardcoded `0.5f` midpoint constant.
    - Eliminates stuttering, duplicate cadence frames, and judder in 3X, 4X, and 6X modes.
  - **Automatic Software Pacing (RSYNC) & Freeze Prevention**:
    - Derives and pins DLSS-G's flip metering offset dynamically from `sl.dlss_g.dll`, forcing fallback onto the software RSYNC pacer.
    - Prevents Blackwell hardware flip metering waits on Ada (RTX 40), eliminating black screens and frozen frames in 3X, 4X, and 6X modes.
    - Matches the ReShade `MFGAdaUnlock-RenoDx` architecture gate rewrite (`0x190`), with automated pacing checks before passing requests.
  - **Clean Native Resource & Struct Flow (Anti-Crash & Zero Black Lines)**:
    - Preserves all game scene buffers and original Streamline struct versions intact without artificial overrides or tag zeroing.
    - Completely prevents crashes and eliminates black lines and black screens in Unreal Engine 5 (*Black Myth: Wukong*, *Silent Hill 2*).
- **Turing & Ampere Hardware Support (RTX 20 & RTX 30 series)**:
  - Bypasses architecture locks in NVIDIA DLSS-G via runtime PTX instruction redirection and dynamic gate patching.
  - Multi-frame generation executes directly on hardware **Tensor Cores** with native performance and minimum latency.

#### 🛠️ Seamless In-Game Menu & OptiFG Unification
- **OptiFG Multi-Frame Generation (2X, 3X, 4X, 5X, 6X)**:
  - Games without native Frame Generation can now force Multi-Frame Generation at up to 6X on RTX 40 (and up to 4X on RTX 30/20) using full OptiScaler OptiFG!
  - Synchronizes `DLSSG.MultiFrameCountMax`, `DLSSG_Dx12::GetMaxInterpolationCount()`, and `SetInterpolatedFrameCount()` across Streamline so intermediate frames are properly generated and pacing is respected.
- **Unified Menu Flow (No More Duplicate DLSS-G Sections)**:
  - Eliminated confusing redundant sections in the menu.
  - When playing games with **Native DLSS-G** (Cyberpunk, Wukong, Silent Hill 2), only the Native Game DLSS-G section is shown.
  - When playing games with **OptiFG** (`FG Output = DLSSG`), all controls (`Active`, `Unlock MFG`, and `Multi-Frame Ratio`) are consolidated into a single unified control block that applies dynamically in real time.
- **Custom Branding**:
  - OptiScaler in-game UI displays: `evairx/optiscaler-mfg v10.0.1 - <GameExe>`.

---

### 📦 What Comes Bundled:
- **OptiScaler-MFG v10.0.1** (`dxgi.dll` with AdaMFGUnlock & DLSS-G Tensor Core unlockers)
- **dlssg_sm86 runtime** (`OptiScaler/dlssg_sm86/` with `dlssg_sm86.dll` & optimized PTX JIT profile)
- **NVIDIA Streamline 2.7.x binaries** (`sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `nvngx_dlssg.dll` v310.7.129.0)
- **AMD FidelityFX SDK** (FSR 3.1 & FSR 2.2 upscalers and FG)
- **Intel XeSS SDK** (XeSS and XeFG)
- Automated installation script (`setup_windows.bat`)

---

### 🎮 Installation Instructions

1. **Extract all files**:
   - For **Unreal Engine** games (e.g., *Silent Hill 2*, *Black Myth: Wukong*, *Star Wars Jedi: Survivor*):
     Extract the archive directly into the directory where the shipping binary lives, e.g.:
     `<GameFolder>\SHProto\Binaries\Win64\` (or `<GameFolder>\<GameCode>\Binaries\Win64\`), **NOT** in the root launcher folder.
   - For standard games:
     Extract into the main folder where the game's `.exe` is located.
2. Run `setup_windows.bat` (or rename `OptiScaler.dll` to `dxgi.dll`).
3. Launch the game and press **`Insert`** to open the OptiScaler menu.
4. Check **`Unlock MFG`**, click **Save Settings**, and restart the game.
5. On the next launch, enjoy native DLSS Frame Generation running on your Tensor Cores!
