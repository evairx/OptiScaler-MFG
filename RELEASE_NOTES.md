# OptiScaler-MFG v10.0.0-evairx

Welcome to the initial release of **OptiScaler-MFG (v10.0.0-evairx)**! 🎉 🎉 🎉

This fork of OptiScaler unlocks native **NVIDIA DLSS Frame Generation (MFG)** on **RTX 20, RTX 30, and RTX 40 series GPUs**, running directly on hardware **Tensor Cores** without requiring ReShade or optical flow shaders.

Always check the instructions below for compatibility and configuration details.

---

### 🌟 Major Highlights & New Features

#### 🚀 Native DLSS Multi-Frame Generation on Tensor Cores (RTX 20 / RTX 30 / RTX 40)
- **Definitive Multi-Frame Architecture (2X, 3X, 4X, 5X, 6X)**:
  - Fully incorporates and ports the ReShade `MFGAdaUnlock-RenoDx` engine directly into OptiScaler's native binary (`dxgi.dll`) without needing ReShade, external hooks, or sidecar add-ons.
  - **Zero-Flicker Midpoint Temporal Reconstruction**:
    - Decompresses Fatbin PTX and rewrites the interpolation kernel to inject dynamic temporal progress parameters (`%f134` and `%f136`) instead of the hardcoded `0.5f` midpoint constant.
    - Eliminates stuttering, duplicate cadence frames, and judder in 3X, 4X, and 6X modes.
  - **Quality Guard (Anti-Flicker & Halo Prevention)**:
    - Automatically cleanses mismatched HUD-less / UI separation tags when multi-frame generation is active.
    - Prevents UI tearing, halos, and flashing artifacts in Unreal Engine 5 games like *Black Myth: Wukong* and *Silent Hill 2*.
  - **Instant Load-Time Interception**:
    - Hooks into the Windows library loader (`Kernel32` / `KernelBase`) to patch `nvngx_dlssg.dll` and driver OTA models (`\models\dlssg\*.bin`) the very millisecond they are mapped into memory, before NGX can cache device capabilities.
  - **Pacing Guard & History Synchronization**:
    - Dynamically injects temporal history resets into Streamline (`slSetConstants` with `reset = eTrue`) upon switching multipliers, ensuring instantaneous and clean transitions.
    - Integrated with NVIDIA Reflex pacing to keep frame delivery butter-smooth.
- **Turing & Ampere Hardware Support (RTX 20 & RTX 30 series)**:
  - Bypasses architecture locks in NVIDIA DLSS-G via runtime PTX instruction redirection and dynamic gate patching.
  - Multi-frame generation executes directly on hardware **Tensor Cores** with native performance and minimum latency.

#### 🛠️ Seamless In-Game Menu & Flow
- **Direct "Unlock MFG" Toggle**:
  - `Unlock MFG` is always available and clickable in the menu under the *MFG RTX 20/30/40* section.
  - Clean restart workflow:
    - When enabled during gameplay, a clear reminder is displayed:
      `Save Settings and restart the game for MFG to take effect.`
    - Once the game is restarted with `Unlock MFG` enabled, the status confirms:
      `MFG is active and running on Tensor Cores.`
    - The **Multi-Frame Ratio** selector is unlocked, allowing you to choose between **Default (Game), 2X, 3X, 4X, 5X, and 6X**.
- **Memory & Crash Protections**:
  - Implemented strict Streamline structure version boundary checking to prevent stack overflows and memory corruption in Unreal Engine 5 games (such as *Silent Hill 2*).
  - Clean swapchain handling preventing DXGI access violations during initialization.
- **Custom Branding**:
  - OptiScaler in-game UI displays: `evairx/optiscaler-mfg v10.0.0 - <GameExe>`.

---

### 📦 What Comes Bundled:
- **OptiScaler-MFG v10.0.0-evairx** (with AdaMFGUnlock & DLSS-G Tensor Core unlockers)
- **NVIDIA Streamline 2.4.x / 2.7.x binaries** (`sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `nvngx_dlssg.dll`, etc.)
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
