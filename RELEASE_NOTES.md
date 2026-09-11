# OptiScaler-MFG v10.0.2-pre2 (Private Test Build)

Welcome to **OptiScaler-MFG (v10.0.2-pre2)**! 🚀 🎮

### 🆕 What's New in v10.0.2-pre2:
- **OptiFG DLSS-G Mutex Decoupling**: Fully decoupled DLSS-G from global FG mutexes during upscaler inputs and swapchain presentation. Eliminates render-thread serialization stalls and restores high base render rates in games without native Streamline (such as *The Last of Us Part 1*).
- **Latency Queue Reduction**: Configured `SetMaximumFrameLatency(1)` on both native and wrapped swapchains, eliminating up to ~50 ms of queued presentation delay.
- **Tearing Presentation Decoupling**: Automatically grants `DXGI_PRESENT_ALLOW_TEARING` on flip presentation when Frame Generation is active to avoid frame pacing stalls.
- **In-Game FPS Overlay Math Corrected**: Fixed base FPS calculation so `Total FPS / Base FPS` reflects exact swapchain presentation cadence and true un-interpolated render frequency.
- **Dying Light: The Beast / Dying Light 2 Crash Fix**: Added `DisableHudfix` quirk to prevent Access Violation crashes when entering 3D gameplay.

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
