# OptiScaler-MFG v10.0.2-pre3 (Private Test Build)

Welcome to **OptiScaler-MFG (v10.0.2-pre3)**! 🚀 🎮

### 🆕 What's New in v10.0.2-pre3:
- **OptiFG $\to$ Real NVIDIA DLSS-G Hardware MFG Bridge**:
  - Restored Streamline swapchain hooks (`hooks` and `exclusive_hooks` in `sl.dlss_g.json`) when DLSS-G is the active frame generation output (`activeFgOutput == FGOutput::DLSSG`). Allows `sl.dlss_g.dll` to intercept `IDXGISwapChain::Present` and execute real hardware frame interpolation on NVIDIA Tensor Cores.
  - Preserves `sl::kFeatureDLSS_G` and `sl::kFeatureReflex` feature loading in OptiFG mode so Streamline properly initializes the NVIDIA DLSS-G interposer and plugin pipeline.
  - Enables architecture spoofing for DLSS-G output on Ampere and Turing GPUs so RTX 30 and RTX 20 series can run real Tensor Core MFG through OptiFG.
- **Direct Color/HUDLess Buffer Feeding to DLSS-G**:
  - Automatically feeds the upscaled output buffer directly as `FG_ResourceType::HudlessColor` with `UntilPresentFromDispatch` validity if HUD fix did not capture a separate buffer, satisfying DLSS-G's color buffer requirements in games like *Running Train*.
- **DLSS-G Runtime Diagnostic Logging**:
  - Added real-time status inspection of `slDLSSGGetState` after dispatch, actively logging warnings for any flag failures (`eFailCommonConstantsInvalid`, `eFailReflexNotDetectedAtRuntime`, `eFailGetCurrentBackBufferIndexNotCalled`, etc.).
- **Fallback Outputs Intact**:
  - Preserves full fallback support for FSR 3.1 FG and XeFG when user selects them.

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
    - Added **Hardware Bilinear (Fast Sampling)** toggle in the menu for RTX 30 (SM86), written to `dlssg_sm86.ini` and consumed by the runtime.
    - **Not implemented in this build**: the **Quality Guard (Anti-Flicker)** checkbox is read from the INI and shown in the menu, but no consumer applies `FGDLSSGQualityGuard`.
- **Ada Lovelace Native In-Memory Retargeting (RTX 40)**:
  - Preserves the proven zero-allocation Blackwell `sm_120` -> `sm_89` in-place kernel retargeting for ratios up to 6X.
  - Retains in-memory redirection of older game-bundled DLSS-G to OptiScaler's modern v310.7.129 package.
- **Definitive Multi-Frame Architecture (2X, 3X, 4X, 5X, 6X)**:
  - Fully incorporates and ports the ReShade `MFGAdaUnlock-RenoDx` engine directly into OptiScaler's native binary (`dxgi.dll`) without needing ReShade, external hooks, or sidecar add-ons.
  - **In-Memory Dynamic Redirection of DLSS-G & Streamline**:
    - When a game attempts to load an older bundled DLSS-G (such as v3.7.0 in *Silent Hill 2*), OptiScaler intercepts the `LoadLibrary` call in memory and automatically redirects it to OptiScaler's modern `nvngx_dlssg.dll` v310.7.129.0 package.
    - Redirection resolves the `partial match (Adv:0 Val:0 Kernels:0)` case and lets the retargeting step process the embedded interpolation kernel containers of the modern module (31/31 on DLSSG 310.7).
  - **In-Place Dynamic Kernel Retargeting (`sm_120` -> `sm_89`)**:
    - Rewrites Blackwell interpolation kernels to Ada Lovelace architecture directly in memory upon DLL loading.
    - Addresses the previously observed crashes when switching between 2X, 3X, 4X, and 6X in the tested titles.
  - **Kernel retargeting scope (as implemented)**:
    - Rewrites the `.target sm_120` directive to `.target sm_89` inside the embedded PTX container of every detected FATBIN and parks the Ada cubin images (see `MfgUnlock.cpp`). Measured at 31/31 containers on DLSSG 310.7 and 33/33 on 310.1.
    - **Not implemented in this build**: the midpoint temporal reconstruction (`%f134`/`%f136`) and the automatic software RSYNC / flip-metering pacer claimed by earlier revisions of these notes. `DLSSG.ForceFlipMeteringOff` is parsed from the INI but is not applied anywhere.
  - **Resource & Struct Flow (Anti-Crash)**:
    - Preserves game scene buffers and the original Streamline struct versions without artificial overrides or tag zeroing.
    - Known risk: HUD/UI composition mismatches can still produce black lines or black screens in some titles; see `MFG_IMPLEMENTATION.md` for the pending guard work.
- **Turing & Ampere Support (RTX 20 & RTX 30 series)**:
  - Uses a separately distributed SM86/SM75 runtime (`OptiScaler/dlssg_sm86/`) instead of the NVIDIA-signed module: a third-party CUDA re-host of the DLSS-G model, capped at 3 generated frames (4X), with optional hardware bilinear sampling.
  - Runs on the GPU's Tensor Cores; it is not the official NVIDIA MFG pipeline and has no Blackwell hardware flip metering.

#### 🛠️ Seamless In-Game Menu & OptiFG Unification
- **OptiFG Multi-Frame Generation (2X, 3X, 4X, 5X, 6X)**:
  - The DLSS-G/MFG output is exposed for the OptiFG (upscaler) input and synchronizes `DLSSG.MultiFrameCountMax`, `DLSSG_Dx12::GetMaxInterpolationCount()`, and `SetInterpolatedFrameCount()` across Streamline.
  - Important limitation: in games without native DLSS-G/Streamline frame generation, this path is not expected to present additional frames, because the provider needs Streamline's presentation pipeline. Use **XeFG** or **FSR FG** as the output in those games.
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
