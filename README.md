# YOLOv8 CS2 Overlay - C++

Updated version of https://github.com/Leksa667/YOLOv8-Overlay-CS2 and merged from python to C++ to be more fast
Real-time overlay for Counter-Strike 2 powered by YOLOv8 + ONNX Runtime.  
Transparent Win32 window drawn over the game - zero impact on CS2 performance.

---

## Features

- **DXGI Desktop Duplication** - near-zero latency GPU screen capture
- **CUDA/GPU inference** via ONNX Runtime (CPU fallback automatic)
- **4-class detection** - CT body, CT head, T body, T head
- **3 box styles** - full box, corner brackets, skeleton
- **Aimbot modes** - FOV assist (hold key) and lock mode (toggle)
- **Head / body aim point** toggle
- **Recoil Control System (RCS)** with adjustable strength
- **Arduino USB HID mode** - mouse movements sent via ATmega32U4 serial bridge with built-in humanization (sub-pixel accumulation, jitter, inertia residual)
- **Per-class color picker** and visibility toggles
- **Configurable hotkeys** (reassignable in-overlay)
- **Draggable HUD** with 4 tabs: Aim, Visuals, Misc, Config
- Excluded from OBS/game capture via `WDA_EXCLUDEFROMCAPTURE`

---

## Requirements

| Tool | Version |
|------|---------|
| Windows 10/11 | 64-bit |
| Visual Studio 2022 | with C++ Desktop workload |
| ONNX Runtime | ≥ 1.18 (GPU build recommended) |
| CUDA + cuDNN | 12.x / 9.x (for GPU inference) |
| OpenCV | 4.9.0 (bundled via setup.ps1) |

---

## Quick Setup

### 1 - Download dependencies

```powershell
.\setup.ps1
```

Downloads and extracts OpenCV 4.9.0 and ONNX Runtime CPU into `deps\`.

### 2 - (Optional) Switch to GPU inference

```powershell
.\setup_gpu_ort.ps1
```

Replaces the CPU ONNX Runtime with the GPU (CUDA 12 / cuDNN 9.x) build.  
Requires an NVIDIA GPU with up-to-date drivers.  
After running this script, rebuild the solution.

### 3 - Build

Open `YOLOv8_Overlay_CS2.sln` in Visual Studio 2022.  
Select **Release | x64** and press **Ctrl+Shift+B**.

The executable lands in `x64\Release\YOLOv8_Overlay_CS2.exe`.

### 4 - Add the model

Place `best.onnx` next to the executable:

```
x64\Release\
├── YOLOv8_Overlay_CS2.exe
├── best.onnx               ← your YOLOv8 model (ONNX format)
├── onnxruntime.dll
└── onnxruntime_providers_shared.dll
```

### 5 - Run

Launch **CS2 in borderless windowed mode**, then run:

```powershell
.\x64\Release\YOLOv8_Overlay_CS2.exe
```

<img width="1280" height="960" alt="screen" src="https://github.com/user-attachments/assets/2b50a9ef-1456-4040-aa22-98b2ffb46fbf" />


A startup dialog lets you choose between **Normal Mode** (Windows SendInput) and **Arduino Mode** (USB HID bridge).

---

## Hotkeys

| Key | Action |
|-----|--------|
| **Insert** | Show / hide the HUD menu |
| **F1** | Toggle detection on / off |
| **F2** | Show / hide class labels |
| **F3** | Show / hide FOV circle |
| **K** (hold) | FOV aimbot while held |
| **End** | Exit overlay |

All hotkeys are remappable from the **Config** tab in the HUD.

---

## Arduino HID Mode

For bypassing software-level input detection, flash `arduino_mouse\arduino_mouse.ino` to an **Arduino Leonardo**, **Micro**, or **Pro Micro** (ATmega32U4).

**Serial protocol (115200 baud):**

| Command | Effect |
|---------|--------|
| `M <dx> <dy>\n` | Relative mouse move |
| `B\n` | Left click |
| `L\n` | Left button down |
| `U\n` | Left button up |

**Humanization features built into the firmware:**
- Sub-pixel accumulation (Q8.8 fixed-point) - fractional moves are never dropped
- Report-rate timing jitter - ~37% of reports staggered 400–1600 µs
- Inertia residual - 1 px trailing move after large movements
- PRNG seeded from analog pin noise (A0/A1) - unique signature every boot

**How to flash:**
1. Arduino IDE → Tools → Board → **Arduino Micro** (or Leonardo)
2. Tools → Port → select upload port
3. Upload `arduino_mouse\arduino_mouse.ino`
4. Select the new COM port in the overlay startup dialog

---

## Project structure

```
YOLOv8-Overlay-CS2-CPP\
├── src\
│   ├── main.cpp                   ← overlay window, HUD, aimbot, inference loop
│   ├── screen_capture.cpp/h       ← GDI and DXGI capture
│   └── yolo_detector.cpp/h        ← ONNX Runtime YOLOv8 wrapper
├── arduino_mouse\
│   └── arduino_mouse.ino          ← ATmega32U4 USB HID firmware
├── deps\                          ← populated by setup.ps1
│   ├── opencv\
│   └── onnxruntime\
├── setup.ps1                      ← downloads OpenCV + ORT CPU
├── setup_gpu_ort.ps1              ← swaps ORT to GPU (CUDA 12)
├── YOLOv8_Overlay_CS2.sln
└── YOLOv8_Overlay_CS2.vcxproj
```

---

## Performance

| Mode | Capture | Inference | FPS (estimate) |
|------|---------|-----------|----------------|
| DXGI + CUDA | ~1 ms | ~5–8 ms | 120–200 |
| DXGI + CPU | ~1 ms | ~40–80 ms | 15–25 |
| GDI + CPU | ~5–10 ms | ~40–80 ms | 10–20 |

GPU build is strongly recommended. Run `setup_gpu_ort.ps1` once after initial setup.

---

## Training your own model

See `train.py` in the companion Python repository.  
Export with:

```bash
yolo export model=best.pt format=onnx imgsz=640 opset=17
```

The model must output shape `[1, 4+NUM_CLASSES, num_anchors]` — standard YOLOv8 ONNX export.
