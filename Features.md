## Features
* Supports multiple upscaling backends (XeSS, FSR 2.1.2, FSR 2.2.1, FSR 3.1 and DLSS)
* Experimental support for frame generation (OptiFG by FSR) with version 0.7.0 and above
* Supports DLSS 3.7 and above (check [installation instructions](#install-as-non-nvngx))
* Supports DLSS-D (Ray Reconstruction) on Nvidia cards (Supports changing presets and using OptiScaler enhancements)
* Ability to modify DLSS/DLSS-D presets on the fly
* Supports XeSS v1.3.x's Ultra Performance, NativeAA modes (**Not using default XeSS 1.3.x scaling ratios, rather the old ones**) 
* An [in-game menu](https://github.com/optiscaler/OptiScaler/blob/master/Config.md) for tuning and saving settings on the fly (Shortcut key is **INSERT**)
* Optional real NVIDIA DLSS Multi Frame Generation (2x/3x/4x/6x) on supported RTX 40 GPUs and compatible DLSS-G providers
* **RCAS** support with **MAS** (Motion Adaptive Sharpening) for all Dx12 & Dx11 upscalers
* **Output Scaling** option (0.5x to 3.0x) for backends running on Dx12 & Dx11
* Supports DXGI spoofing (when running as `dxgi.dll`) as Nvidia GPUs (with XeSS detection to enable XMX on Intel Arc cards)
* Supports Vulkan spoofing (needs to be enabled from `nvngi.ini`) as Nvidia GPUs (not working for Doom Eternal)
* Supports loading specific `nvapi64.dll` file (when running in non-nvngx mode)
* Supports loading specific `nvngx_dlss.dll` file (when running in non-nvngx mode)
* Supports overriding scaling ratios
* Supports overriding DRS range
* Autofixes for [colored lights](https://github.com/optiscaler/OptiScaler/blob/master/Config.md#resource-barriers-dx12-only) on Unreal Engine & AMD graphics cards 
* Autofixes for [missing exposure texture](https://github.com/optiscaler/OptiScaler/blob/master/Config.md#init-flags) information
* Ability to modify [Mipmap Lod Bias](https://github.com/optiscaler/OptiScaler/blob/master/Config.md#mipmap-lod-bias-override-dx12-only) game value
* Supports [Fakenvapi](https://github.com/FakeMichau/fakenvapi) integration which enables Reflex hooking and injecting Anti-Lag 2 or LatencyFlex (LFX)
* Provides explicit standalone FSR Frame Generation and Intel XeFG outputs; they are never selected as an implicit DLSS MFG fallback
