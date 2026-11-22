# Splinter Cell: Double Agent (Xbox) - Native macOS ARM64 Port

An unofficial, native macOS (Apple Silicon ARM64) port of the original Xbox version of **Tom Clancy's Splinter Cell: Double Agent** (Version 2) via static recompilation. 

> [!NOTE]
> **Fork & Attribution:**  
> This project is a macOS Apple Silicon port and fork of [SCDA-XBERecomp](https://github.com/TGP482/SCDA-XBERecomp) by **TGP482**. The core static recompilation framework, Xbox kernel emulation layer, and ahead-of-time (AOT) translation were originally designed and developed by TGP482. This repository translates and adapts the codebase to run natively on macOS Apple Silicon (ARM64: M1, M2, M3, M4 chips) without Rosetta 2, Wine, or virtualization.

> [!IMPORTANT]
> **Legal Disclaimer:**  
> This project does **not** contain any copyrighted game assets, proprietary Xbox libraries, or intellectual property. You must provide your own legally acquired copy of the original Xbox game disc image (ISO / XISO).

---

## Table of Contents
1. [Why the Xbox Version?](#why-the-xbox-version)
2. [Current Status](#current-status)
   - [What Works (The Game is Running!)](#what-works-the-game-is-running)
   - [Issues Faced & Resolved](#issues-faced--resolved)
3. [Roadmap (Things We Are Willing to Do)](#roadmap-things-we-are-willing-to-do)
4. [Beginner's Step-by-Step Guide](#beginners-step-by-step-guide)
   - [Step 1: Install Required Tools](#step-1-install-required-tools)
   - [Step 2: Obtain the Repository](#step-2-obtain-the-repository)
   - [Step 3: Prepare the Game Disc Image](#step-3-prepare-the-game-disc-image)
   - [Step 4: Extract Game Assets](#step-4-extract-game-assets)
   - [Step 5: Compile the Game](#step-5-compile-the-game)
   - [Step 6: Configure Options](#step-6-configure-options)
   - [Step 7: Launch and Play](#step-7-launch-and-play)
   - [Step 8: Install the App](#step-8-install-the-app)
5. [Controls & Keyboard Layout](#controls--keyboard-layout)
6. [Troubleshooting & FAQ](#troubleshooting--faq)
7. [Credits & Acknowledgements](#credits--acknowledgements)
8. [License](#license)

---

## Why the Xbox Version?
Splinter Cell: Double Agent was released in two completely distinct versions:
- **Version 1 (PC / Xbox 360 / PS3):** Built on an experimental engine with daylight missions, differing mechanics, and known stability issues on modern systems.
- **Version 2 (Original Xbox / PS2):** Built by Ubisoft Montreal on the beloved *Chaos Theory* engine. It is widely considered by fans to be the true continuation of classic stealth gameplay, featuring the dynamic JBA Trust meter, exclusive missions (Iceland, Ellsworth Penitentiary, Cozumel, Shanghai, Kinshasa, and the exclusive Train & Submarine levels), and classic lighting and stealth mechanics.

This project preserves and brings the definitive Version 2 experience natively to macOS!

---

## Current Status

> [!TIP]
> **The game is running!** The single-player campaign and menus are fully functional and rendering at a smooth, locked 60 FPS natively on Apple Silicon (M1, M2, M3, M4) with hardware-accelerated 3D graphics, 6-channel surround audio, and responsive keyboard, mouse, and gamepad controls.

### What Works (The Game is Running!)
- **Full 3D Hardware-Accelerated Graphics:** Native rendering on Apple Silicon GPUs via MoltenVK (Vulkan over Metal) at locked 60 FPS, featuring high-resolution rendering, dynamic lighting, soft shadows, Night Vision, and Thermal Goggles.
- **Native Apple Silicon Recompilation:** 33,000+ original Xbox x86 functions are statically translated ahead-of-time into native 64-bit ARM machine code (`arm64`), running directly on bare metal without emulation layers or translation overhead.
- **Darwin Platform Abstraction:** Native macOS memory management (`mmap` guest address space at `0x300000000`), POSIX threading, and Xbox high-resolution performance counters.
- **Complete Controls (Keyboard, Mouse & Gamepad):** Full support for keyboard and mouse navigation (WASD movement, sub-pixel mouse look with automatic cursor locking in 3D play, interaction keys, weapon selection) as well as native Xbox and PlayStation controller support through SDL3.
- **6-Channel Surround Audio:** Full 48 kHz, 6-channel multi-positional 3D audio via SDL3, supporting Bink movie audio streams and Xbox SS2 / LS2 audio banks.
- **Direct ISO / XISO Mounting:** Direct disc image mounting and sector reading from raw `.iso` / `.xiso.iso` files, handling Xbox drive paths (`D:`, `T:`, `U:`, `Z:`).
- **Video & File Streaming:** Cutscene movie playback (`logos.bik`, `back_menu.bik`) and script parsing (`common.lin`, `Menu.lin`).
- **In-Game Settings & Developer Console:** Instant access to configuration via <kbd>F10</kbd> overlay, developer console (`~`), and real-time FPS counter.

### Issues Faced & Resolved
During the macOS Apple Silicon porting process, several architecture-specific hurdles were identified and resolved:
1. **Volk Dynamic Loader Collision on macOS:**  
   *Issue:* Volk's function pointer overrides conflicted with macOS dynamic loader stubs for Vulkan functions, causing memory access violations (`SIGBUS` / `EXC_BAD_ACCESS` in `vkCreateSampler`).  
   *Fix:* Removed Volk and linked directly against Homebrew's MoltenVK / Vulkan runtime.
2. **glslang 16 HLSL Support:**  
   *Issue:* The Homebrew `glslang` 16 package required explicit `-DENABLE_HLSL` preprocessor definitions to enable Xbox HLSL shader bytecode translation.  
   *Fix:* Added compiler definitions in `CMakeLists.txt`.
3. **Video Presentation & Black Screen Resolution:**  
   *Issue:* The game engine was executing and rendering internally, but displaying a black screen because `SDL_Vulkan_CreateSurface` was being invoked from Xbox guest Thread 14. macOS AppKit/Cocoa strictly requires that all `NSView` hierarchy modifications happen on the main thread (`dispatch_get_main_queue()`). Calling it off-thread caused macOS WindowServer to drop the `CAMetalLayer`.  
   *Fix:* Synchronously dispatched surface creation to the main queue using Grand Central Dispatch (`dispatch_sync(dispatch_get_main_queue(), ^{ ... })`), restoring clean hardware presentation to the window.
4. **Input Subsystem (Keyboard & Mouse Mapping):**  
   *Issue:* The original codebase relied on Windows-specific `RAWINPUT` (`WM_INPUT`) and `GetAsyncKeyState` (which were stubbed to return 0 on non-Windows platforms), causing all keyboard and mouse inputs to be ignored.  
   *Fix:* Implemented a native SDL3 event pipeline with lock-free atomic key states, scancode-to-virtual-key translation, relative mouse motion accumulation for camera look, and smart mouse grab toggling between gameplay and menus.
5. **Freeze on "SAVING GAME..." After Iceland Part 1:**  
   *Issue:* When completing the first section of the introductory Iceland mission (`x01_iceland_a`) and transitioning to Part 2 (`x01_iceland_b`), the game became permanently frozen on the *"SAVING GAME... Please don't close the game or turn off your PC"* screen at ~11.9 FPS. This was caused by two issues: the engine requested map texture and sound streaming banks from the virtual cache drive `Z:\Default\...` which did not exist on host file systems, and the volume of asset loads hit macOS's conservative default file descriptor limits (`EMFILE: Too many open files`).  
   *Fix:* Implemented an automatic path-redirection fallback in `NtCreateFile` and `NtOpenFile` (`src/kernel/files.cpp`) to route unmapped `Z:\Default` requests directly to the mounted disc image `D:\Default`. Additionally, raised the macOS process file descriptor limit via `setrlimit(RLIMIT_NOFILE, 10240)` in `main.cpp`, allowing seamless level transitions and checkpoint saving.
6. **Eliminating Checkerboard Squares, Diagonal Stripes, and Viewport Zoom:**  
   *Issue:* Severe square checkerboard blocks, stepped triangular artifacts, and diagonal cross-hatching appeared across the sky dome, character models (Sam Fisher's suit), snow terrain, mountain cliffs, and crates. Additionally, an earlier attempt to scale target texels accidentally introduced a 2.5x viewport zoom. Forensic analysis revealed four distinct bugs:
   1. **Viewport Zoom Bug in `FillTexelConstants`:** Setting `texsize[i].xy` to the host target dimensions (e.g. 1600x1200) broke rectangle texture sampling (`t.rect`). Shaders convert guest pixel coordinates ($0 \dots 640$) to normalized UVs via `uv / texsize.xy`. Dividing by 1600 clamped UVs to $[0, 0.4]$, sampling only the upper-left 40% of the buffer and zooming the camera by 2.5x.
   2. **MoltenVK Shadow Border Depth & Out-of-Bounds Shadowing:** MoltenVK lacks `VK_EXT_custom_border_color`, defaulting border depth to transparent black ($0.0$). Any shadow depth sample outside the light's frustum (such as the sky dome, distant background, or geometry behind the directional sun) failed depth comparison against $0.0$, falsely flagging the sky as occluded and projecting dark shadow blocks across the clouds.
   3. **Self-Shadowing Acne on Angled Surfaces:** On steep cliffs, snow slopes, and curved character models, depth changes by $30 \dots 100$ units per shadow texel. A fixed 8-unit depth bias was vastly exceeded by the surface slope, causing the surface to shadow itself and produce repeating diagonal stripes (`/// /// ///`).
   4. **Low-Resolution 16x16 Blur Chain:** The console's legacy edge blur chain (`sub_002EF9A1`) downsamples shadow alpha to a 16x16 surface and stretches it back over HD displays, creating 16x16 pixel blocks when active.
   *Fix:* 
   - Reverted `c.texsize[i].xy` in `FillTexelConstants` back to guest dimensions (`d.width`, `d.height`), restoring accurate 1:1 viewport scaling and untruncated subtitles/HUD.
   - Configured `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER` with `VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE` (depth $1.0$) on shadow samplers and added an explicit frustum bounds guard in `shadow()` so anything outside the light projection evaluates as unshadowed ($1.0$).
   - Implemented an artifact-free slope-scaled depth bias in `shadow()` using triangle-constant screen depth rates: `clamp(base_bias + 2.0 * max(abs(ddx(z)), abs(ddy(z))), base_bias, max_bias)`, eliminating diagonal stripes without 2x2 fragment quad dither.
   - Defaulted `shadow_filter = 0` (SHARP / OFF) to bypass the legacy 16x16 downsample chain and provide pristine 2048x2048 hardware PCF filtered shadows.

---

## Roadmap (Things We Are Willing to Do)
- [x] **Fix Presentation Blit in MoltenVK:** Fully resolved! The game renders smoothly with hardware acceleration on Apple Silicon.
- [x] **Implement Full Keyboard & Mouse Support via SDL3:** Fully implemented with smart mouse capture.
- [x] **Fix Level Transition & Save Screen Hang:** Resolved "SAVING GAME..." checkpoint freeze when transitioning to Iceland Part 2.
- [x] **Shadow Acne & Artifact Elimination:** Fixed self-shadowing acne and blocky PCF artifacts on Apple Silicon.
- [ ] **Multiplayer Testing:** Verify online/LAN Co-op and Versus multiplayer on macOS.
- [x] **macOS Application Bundle:** Self-contained `.app` bundle with drag-and-drop installation.

---

## Beginner's Step-by-Step Guide

Even if you have minimal experience using the macOS Terminal or compiling software, this guide will walk you through every step.

### Step 1: Install Required Tools
Open the **Terminal** app on your Mac (press <kbd>Cmd</kbd> + <kbd>Space</kbd>, type `Terminal`, and press <kbd>Enter</kbd>).

1. Install **Homebrew** (if you don't already have it):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```
2. Install the necessary development tools and libraries:
   ```bash
   brew install git cmake ninja sdl3 glslang molten-vk vulkan-headers vulkan-loader
   ```

---

### Step 2: Obtain the Repository
Navigate to the directory where you want to keep the project (for example, on an external drive or your home folder) and clone the repository:

```bash
cd "<PATH_TO_WHERE_YOU_WANT_THE_PROJECT>"
git clone https://github.com/aminerochdi1/SCDA-XBERecomp-MacOSPort
cd SCDA-XBERecomp-MacOSPort
```
> **Placeholder:** Replace `<PATH_TO_WHERE_YOU_WANT_THE_PROJECT>` with your actual folder path (e.g. `/Volumes/ORICO` or `~/Projects`).

---

### Step 3: Prepare the Game Disc Image
You need a legal copy of the original **Xbox** version of *Tom Clancy's Splinter Cell: Double Agent* in **ISO** or **XISO** format.
- **Recommended version:** Original Xbox (USA or Europe multilingual release, Redump or clean XISO).
- Verified SHA-1 hash for `default.xbe`: `91290baf93c2f6ae735a3106c48734665c826d84`.
- Note: PS2, GameCube, PC, or Xbox 360 ISOs are **not** compatible. It must be the original Xbox release.

Copy your game `.iso` file into the `assets` folder inside the project:
```bash
cp "<PATH_TO_YOUR_GAME_ISO>" "<PATH_TO_SCDA_XBERECOMP>/assets/"
```
> **Placeholders:**
> - `<PATH_TO_YOUR_GAME_ISO>`: Path to your game file (e.g. `~/Downloads/SplinterCellDoubleAgent.iso`).
> - `<PATH_TO_SCDA_XBERECOMP>`: Path to this repository folder.

---

### Step 4: Extract Game Assets
The static recompiler requires the game executables and data files to generate C++ code. We first build and run the included `extract_xbe` tool:

1. Create a temporary build directory for the tools:
   ```bash
   cd "<PATH_TO_SCDA_XBERECOMP>"
   cmake -B build-tools -GNinja -DCMAKE_BUILD_TYPE=Release
   ninja -C build-tools extract_xbe xbe_recomp
   ```
   > **Note:** the first `cmake` run warns that `assets/default.xbe` and `assets/sc4_adv.xbe` are missing. That is expected — they are pulled off the ISO in the next step.

2. Extract the assets from your ISO:
   ```bash
   ./build-tools/extract_xbe "assets/<YOUR_ISO_FILENAME>.iso" assets
   ```
   > **Placeholder:** Replace `<YOUR_ISO_FILENAME>.iso` with the exact filename of your ISO in the `assets/` folder.

   After this completes, the `assets` directory will contain:
   - `default.xbe` (Single-player executable, used by the recompiler)
   - `sc4_adv.xbe` (Versus/Multiplayer executable, used by the recompiler)

   The game's data files (`Default/`, `adv/`, movies, audio) are read straight from the `.iso` at runtime, so they are not unpacked here.

---

### Step 5: Compile the Game
Now, compile the recompiled game executables for macOS Apple Silicon:

```bash
cd "<PATH_TO_SCDA_XBERECOMP>"
cmake -B build-macos -GNinja -DCMAKE_BUILD_TYPE=Release
ninja -C build-macos SCDA SCDA_Versus
```

> [!TIP]
> The first compilation translates the entire Xbox game into native code and compiles tens of thousands of functions. This may take 5 to 15 minutes depending on your Apple Silicon chip.

When the build finishes, `build-macos/` will contain:
- `Splinter Cell Double Agent.app` — the single-player campaign as a self-contained macOS application
- `SCDA_Versus` — the multiplayer executable, which the app bundle also carries inside it

---

### Step 6: Configure Options
Settings are stored in `scda.toml`: in the build folder when running the build output, and in
`~/Library/Application Support/SCDA` when running the app bundle. You can edit this file to customize your experience:

```toml
# Display & Performance
display_mode = "windowed"     # "windowed", "borderless", or "fullscreen"
fps_cap = 60                  # Frame rate cap (30, 60, or 120)
fps_counter = true            # Set to true to show the FPS counter on screen
vsync = true                  # Reduce screen tearing

# Audio & Input
player1 = "keyboard+controller1"
skip_intros = false           # Set to true to bypass intro logos
```

---

### Step 7: Launch and Play
Open the app bundle, from the `build-macos` directory:

```bash
open "<PATH_TO_SCDA_XBERECOMP>/build-macos/Splinter Cell Double Agent.app"
```
or just double-click it in Finder. You can also use `./run_game.sh` from the repository root.

To run multiplayer, double-click `Play Versus (Multiplayer).command` (it sits beside the app and inside the
bundle's `Contents/Resources`). It starts the Versus build that ships in the same bundle.

---

### Step 8: Install the App
Drag `Splinter Cell Double Agent.app` into `/Applications` (copy `Play Versus (Multiplayer).command` along with it if
you want a one-click multiplayer launcher in your Applications folder). The bundle is self-contained:

- **Game data:** the disc image ships inside `Contents/Resources/assets`, so no external `assets` folder is needed
  once it is built. The bundle is roughly the size of your ISO (about 4 GB).
- **Libraries:** MoltenVK, the Vulkan loader, glslang and SPIRV-Tools are copied into `Contents/Frameworks` and
  their paths rewritten, so the app runs even on a Mac that never installed Homebrew's Vulkan packages.
- **Saves, settings and caches:** written to `~/Library/Application Support/SCDA` (`hdd/`, `scda.toml`, `logs/`,
  `shadercache/`), never into the bundle — so the app can be moved or replaced without losing progress.
- **Signing:** the bundle is ad-hoc signed for local use. It is not notarized, so if you copy it to another Mac,
  right-click the app and choose **Open** the first time.

---

## Controls & Keyboard Layout

The port provides full PC-style Keyboard & Mouse controls alongside native Gamepad support.

![Keyboard Layout](assets/input.jpeg)

| Category | Action | Key / Input |
| :--- | :--- | :--- |
| **Movement** | Move (Forward / Back / Strafe) | <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> |
| | Camera / Look | <kbd>Mouse Move</kbd> |
| | Walking Speed (Speed up / Slow down) | <kbd>Mouse Wheel Up</kbd> / <kbd>Down</kbd> |
| | Crouch / Stand Up | <kbd>C</kbd> |
| | Jump / Split Jump | <kbd>Shift</kbd> |
| | Back to Wall / Switch Hand | <kbd>Q</kbd> |
| **Weapons & Actions** | Primary Fire | <kbd>LMB (Left Click)</kbd> |
| | Alternate Fire (Sticky Shocker / Gadgets) | <kbd>RMB (Right Click)</kbd> |
| | Interact / Action | <kbd>Space</kbd> / <kbd>Enter</kbd> |
| | Equip / Holster Weapon | <kbd>E</kbd> or <kbd>MMB (Middle Click)</kbd> |
| | Quick Inventory (Hold to select weapons/gadgets) | <kbd>Ctrl</kbd> + <kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> |
| | Reload | <kbd>R</kbd> |
| | Whistle | <kbd>V</kbd> |
| | Co-op Action | <kbd>F</kbd> |
| **Vision & Tools** | EEV Vision / Sniper Zoom | <kbd>1</kbd> |
| | Night Vision | <kbd>2</kbd> |
| | Thermal Vision | <kbd>3</kbd> |
| | EMF Vision | <kbd>4</kbd> |
| | Sticky Camera | <kbd>5</kbd> |
| | OPSAT (PDA / Objectives) | <kbd>F1</kbd> |
| **System** | In-Game Settings Overlay (Rebind live) | <kbd>F10</kbd> |
| | Pause | <kbd>Esc</kbd> |
| | Quick Save / Quick Load | <kbd>F5</kbd> / <kbd>F8</kbd> |
| | Developer Console | <kbd>~</kbd> |

---

## Troubleshooting & FAQ

#### Q: The game opens, plays sound, but the window is black.
This was a known AppKit threading issue with the presentation pipeline that has now been **fully resolved**. If you encounter this, ensure your repository has the latest code and that you recompiled using `ninja -C build-macos SCDA`. Also ensure the game window has active focus.

#### Q: Can I use a PlayStation 2 or PC ISO?
No. This recompiler specifically translates original Microsoft Xbox x86 machine code (`.xbe`). Only original Xbox game rips are supported.

#### Q: How do I open the in-game settings overlay?
Press <kbd>F10</kbd> on your keyboard while the game is running to toggle the settings menu.

#### Q: Where are save games stored?
Save games are stored in standard Xbox format inside `hdd/UDATA/5553005e/`. That folder is `build-macos/hdd/`
when running the build output directly, and `~/Library/Application Support/SCDA/hdd/` when running the `.app` bundle.

---

## Credits & Acknowledgements
- **[TGP482](https://github.com/TGP482):** Creator of [SCDA-XBERecomp](https://github.com/TGP482/SCDA-XBERecomp) and [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp), without whom this project would not exist.
- **Joshhhuaaa:** Copyright holder of the upstream SCDA-XBERecomp codebase alongside TGP482.
- **ROCHDI MOHAMMED AMINE:** macOS Apple Silicon (ARM64) porting, Darwin platform translation, and maintenance.
- **[Khronos Group](https://www.khronos.org/) & [MoltenVK](https://github.com/KhronosGroup/MoltenVK):** Providing Vulkan support on Metal.
- **[libsdl-org/SDL](https://github.com/libsdl-org/SDL):** SDL3 for cross-platform audio, windowing, and input.
- **Ubisoft:** Creators of the *Splinter Cell* franchise.

---

## License
This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for full details.

```text
MIT License

Copyright (c) 2026 TGP482, Joshhhuaaa
Copyright (c) 2026, ROCHDI MOHAMMED AMINE (macOS ARM64 port)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
