# Multi-Stream Video Player Solution

A professional multi-stream video player application built with WPF/.NET 8 and Windows Media Foundation, capable of displaying and synchronizing playback of up to 16 video streams simultaneously.

## Session Updates

- **2026-04-19**: Added runtime color controls (Brightness, Contrast, Hue, Saturation) via a custom DX11 renderer interface. All four controls have dedicated sliders in WPF and are applied in both playing and paused states.
- **2026-04-12 23:51:57 IST**: Added real-time video sharpening (luma-only) to the DX11 renderer, including `Sharpen` and `Threshold` sliders in WPF with live value labels and paused-frame update support.

### Color Controls Update (What Was Implemented)

Color adjustments were added as a direct DX11 renderer-level feature, bypassing Media Foundation DSP insertion.

**Why not MF DSP (`CLSID_CColorControlDmo`)?**
The standard Media Foundation Color Control Transform was the first approach. Its CLSID (`CLSID_CColorControlDmo`) and properties (`MFPKEY_COLOR_BRIGHTNESS` etc.) are valid, but topology insertion failed at runtime with `0x80004001` (`E_NOTIMPL`) because the hardware-accelerated / DX11 pipeline does not support software-style DSP insertion at that stage. The DSP path was removed and replaced entirely by the renderer-level path below.

**What was implemented:**

1. **`IDX11VideoColorControl` custom interface** (`DX11VideoRenderer/Common.h`)
   - COM-style interface with `SetColorControls(brightness, contrast, hue, saturation)`
   - Implemented by both `CPresenter` and `CMediaSink` (forwarded to presenter)

2. **Video-processor filter application** (`DX11VideoRenderer/Presenter.cpp`)
   - `ApplyVideoProcessorColorControls()` maps each `-127..+127` slider value to the D3D11 video-processor filter range via `VideoProcessorSetStreamFilter`
   - Called every frame inside the video-processor path

3. **Paused-frame color updates via HLSL shader** (`DX11VideoRenderer/Presenter.cpp`)
   - `SharpenSettings` constant buffer extended with `fBrightness`, `fContrast`, `fHueRadians`, `fSaturation`
   - The sharpen pass (already used for paused repaint) applies these in YCbCr space: hue rotation, saturation scale, brightness/contrast on luma
   - This ensures paused-frame slider changes are visible without new decoded frames

4. **C++/CLI bridge properties** (`MediaFoundation.Player/MFVideoPlayer.h/.cpp`)
   - `VideoPlayer::Brightness/Contrast/Hue/Saturation` properties
   - On set: queries `m_pVideoDisplay` for `IDX11VideoColorControl` and forwards, then triggers repaint

5. **WPF integration** (`MultiStreamVideoPlayer`)
   - `NativeMediaPlayer` dependency properties + property-change callbacks
   - `MainViewModel` observable properties `Brightness`, `Contrast`, `Hue`, `Saturation` with `ColorMin = -127`, `ColorMax = 127`
   - Four sliders with live value labels added to `MainWindow.xaml`

#### Color Control Ranges

| Control | Range | Default | Effect |
|---------|-------|---------|--------|
| Brightness | -127 to +127 | 0 | Shifts luma up/down |
| Contrast | -127 to +127 | 0 | Scales luma around midpoint |
| Hue | -127 to +127 | 0 | Rotates CbCr plane (full rotation = ±π) |
| Saturation | -127 to +127 | 0 | Scales chroma amplitude |

### Video Sharpening Update (What Was Implemented)

The following sharpening-related updates were added across the solution:

1. **Luma-only HLSL sharpen pass (DX11 renderer)**
  - Added a post-process pixel shader in `DX11VideoRenderer/Presenter.cpp`.
  - Shader converts RGB to YCbCr, applies Laplacian sharpening on **Y (luma) only**, then converts back to RGB.
  - This avoids chroma noise/ringing while improving edge detail.

2. **Runtime sharpening parameters via constant buffer**
  - Added `SharpenSettings` constant buffer (`b0`) with:
    - `fSharpenStrength`
    - `fThreshold`
  - Host-side settings are updated per frame using `ID3D11DeviceContext::UpdateSubresource`.

3. **Pipeline integration in presenter**
  - Sharpen is applied after video processor output inside `CPresenter`.
  - Added dedicated resources for post-processing (fullscreen shaders, sampler, intermediate texture/SRV, settings buffer).

4. **WPF UI controls for sharpening**
  - Added two sliders in `MultiStreamVideoPlayer/Views/MainWindow.xaml`:
    - `Sharpen` (strength)
    - `Threshold`
  - Added live numeric labels beside each slider.
  - Added min/max constraints in `MainViewModel`:
    - Strength: `0.0` to `1.0`
    - Threshold: `0.0` to `0.02`

5. **Managed/native propagation path**
  - Slider values flow through:
    - `MainViewModel` → `NativeMediaPlayer` dependency properties
    - `MediaFoundation.Player::VideoPlayer` (`SharpenStrength`, `SharpenThreshold`)
    - packed into `SetRenderingPrefs` payload
    - decoded in DX11 presenter and applied to shader constants.

6. **Paused-frame sharpening updates**
  - Slider changes trigger repaint while paused.
  - Fixed cumulative sharpening while paused by reusing the last unsharpened source frame for re-application.

7. **Upside-down output fix**
  - Corrected fullscreen pass UV mapping in the sharpen pass so processed output is no longer vertically inverted.

#### Recommended Sharpening Presets

- **Subtle**
  - `Sharpen`: `0.35`
  - `Threshold`: `0.010`

- **Balanced**
  - `Sharpen`: `0.60`
  - `Threshold`: `0.004`

- **Aggressive**
  - `Sharpen`: `0.90`
  - `Threshold`: `0.000`

Tip:
- For noisy/low-light footage, increase `Threshold` first.
- For clean footage, increase `Sharpen` first.


## Solution Structure

```
MultiStreamVideoPlayer.sln
│
├── MultiStreamVideoPlayer/          # WPF Application (.NET 8)
│   ├── Views/                        # XAML views
│   ├── ViewModels/                   # MVVM view models
│   ├── Models/                       # Data models
│   ├── Controls/                     # Custom controls (NativeMediaPlayer, TimelineControl)
│   ├── Converters/                   # Value converters
│   └── Styles/                       # XAML styles and themes
│
├── MediaFoundation.Player/           # C++/CLI Wrapper (x64)
│   ├── MFVideoPlayer.cpp/h           # Media Foundation implementation
│   └── AssemblyInfo.cpp              # Assembly metadata
│
└── DX11VideoRenderer/                # DirectX 11 Video Renderer (Static Library)
    ├── Activate.cpp/h                # IMFActivate factory
    ├── MediaSink.cpp/h               # IMFMediaSink implementation
    ├── StreamSink.cpp/h              # IMFStreamSink implementation
    ├── Presenter.cpp/h               # Video presentation
    ├── Scheduler.cpp/h               # Frame scheduling
    ├── Display.cpp/h                 # D3D11/DXGI management
    └── Common.h                      # Common utilities
```

## Projects

### 1. MultiStreamVideoPlayer (WPF/.NET 8)
Main application with multi-stream video playback, custom timeline, and gap detection.

**Key Features:**
- Up to 16 simultaneous video streams with auto-sizing grid
- Custom timeline control with draggable playhead
- Gap detection and auto-skip
- Synchronized playback across all streams
- Dark theme professional UI

**Technology:** .NET 8, WPF, CommunityToolkit.Mvvm

### 2. MediaFoundation.Player (C++/CLI)
Native Media Foundation wrapper providing high-performance video playback.

**Key Features:**
- Direct Media Foundation API access
- EVR (Enhanced Video Renderer) integration
- Hardware-accelerated video rendering
- Full playback control (Play/Pause/Stop/Seek)

**Technology:** C++/CLI, COM, Media Foundation

### 3. DX11VideoRenderer (Static Library)
Optional DirectX 11 custom video renderer for advanced scenarios.

**Key Features:**
- Custom D3D11 rendering pipeline
- Direct texture access
- Extensible for custom shaders

**Technology:** C++, DirectX 11, DXGI

## Build Requirements

- **Visual Studio 2022** (Community, Professional, or Enterprise)
- **Workloads:**
  - .NET desktop development
  - Desktop development with C++
  - C++/CLI support component
- **Windows SDK 10.0** or later
- **Platform:** x64

## Building

### Using Visual Studio
1. Open `MultiStreamVideoPlayer.sln` in Visual Studio 2022
2. Select **Debug** or **Release** configuration
3. Select **x64** platform
4. Build → Build Solution (F6)

### Using MSBuild
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
    MultiStreamVideoPlayer.sln `
    /p:Configuration=Debug `
    /p:Platform="Any CPU" `
    -verbosity:minimal
```

## Running

```powershell
.\MultiStreamVideoPlayer\bin\Debug\net8.0-windows\MultiStreamVideoPlayer.exe
```

## Usage

1. **Add Videos**: Click "📁 Add Streams" and select up to 16 video files
2. **Play**: Click the play button to start synchronized playback
3. **Seek**: Drag the playhead or click on the timeline
4. **Remove**: Click "×" on any video card to remove it

---

### Key Implementation Notes

1. **Window Handle Timing**: The HWND must be created (in `BuildWindowCore`) before calling `OpenUrl`. The `OnSourceChanged` callback defers to `BuildWindowCore` if the handle isn't ready.

2. **Event Threading**: Media Foundation callbacks run on a worker thread. Managed events must be marshaled to the UI thread using `Dispatcher.BeginInvoke`.

3. **Topology Ready State**: The `IMFVideoDisplayControl` can only be retrieved after receiving `MESessionTopologyStatus` with `MF_TOPOSTATUS_READY`.

4. **EVR vs DX11**: The **DX11VideoRenderer is the active renderer** (`USE_DX11_RENDERER = true` in MFVideoPlayer.cpp). EVR is the fallback. Color controls and sharpening require the DX11 renderer.

5. **DX11 GetService Pattern**: For DX11VideoRenderer, `CMediaSink::GetService` must delegate to `CPresenter` for `MR_VIDEO_RENDER_SERVICE` and `MR_VIDEO_ACCELERATION_SERVICE`. This is how Media Foundation gets `IMFVideoDisplayControl` and `IMFDXGIDeviceManager`.

6. **Software Buffer Handling**: When hardware-accelerated decoding isn't available, Media Foundation sends software buffers. The DX11VideoRenderer now handles these by copying to a staging D3D11 texture before processing.

### DX11VideoRenderer Debug Output

Debug logging added to trace DX11 renderer execution. View with DebugView (Sysinternals) or Visual Studio Output:

| Prefix | Component | Events |
|--------|-----------|--------|
| `[DX11Renderer]` | StreamSink | ProcessSample calls |
| `[DX11Scheduler]` | Scheduler | Start, ScheduleSample, PresentSample |
| `[DX11Presenter]` | Presenter | Initialize, SetVideoWindow, SetCurrentMediaType, ProcessFrame, PresentFrame |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ WPF Application (MultiStreamVideoPlayer)                        │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ MainViewModel (MVVM)                                      │  │
│  │  - Video stream management                                │  │
│  │  - Playback control                                       │  │
│  │  - Timeline synchronization                               │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                  │
│  ┌───────────────────────────▼───────────────────────────────┐  │
│  │ NativeMediaPlayer (HwndHost)                              │  │
│  │  - WPF Dependency Properties                              │  │
│  │  - Event Handling                                         │  │
│  │  - HWND Management                                        │  │
│  └───────────────────────────┬───────────────────────────────┘  │
└──────────────────────────────┼──────────────────────────────────┘
                               │ .NET Interop
┌──────────────────────────────▼──────────────────────────────────┐
│ MediaFoundation.Player (C++/CLI)                                │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ VideoPlayer Class                                         │  │
│  │  - IMFMediaSession                                        │  │
│  │  - IMFMediaSource                                         │  │
│  │  - IMFTopology                                            │  │
│  │  - IMFVideoDisplayControl (EVR)                           │  │
│  └───────────────────────────┬───────────────────────────────┘  │
└──────────────────────────────┼──────────────────────────────────┘
                               │ COM Interfaces
┌──────────────────────────────▼──────────────────────────────────┐
│ Windows Media Foundation (Native)                               │
│                                                                 │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐     │
│  │ Source         │  │ Topology       │  │ Session        │     │
│  │ Resolver       │  │ Builder        │  │ Manager        │     │
│  └────────────────┘  └────────────────┘  └────────────────┘     │
│                                                                 │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐     │
│  │ EVR            │  │ SAR            │  │ Presentation   │     │
│  │ (Video)        │  │ (Audio)        │  │ Clock          │     │
│  └────────────────┘  └────────────────┘  └────────────────┘     │ 
└─────────────────────────────────────────────────────────────────┘
```

---

## Debug Console

The application includes a debug console for troubleshooting. Console output shows:
- NativeMediaPlayer lifecycle events
- Media Foundation session events
- Topology status changes
- Playback state changes

To disable the debug console, remove `AllocConsole()` from `App.xaml.cs`.

---

## Future Improvements

- [x] Enable and integrate DX11VideoRenderer
- [x] Real-time video sharpening with HLSL shader
- [x] Runtime color controls (Brightness/Contrast/Hue/Saturation)
- [ ] Implement HDR support
- [ ] Add video snapshot capture
- [ ] Implement playback speed control
- [ ] Add bookmark/marker support
- [ ] Multi-monitor support
- [ ] Settings persistence
- [ ] Recent files list
- [ ] Keyboard shortcuts

---


### Build errors
- Ensure Visual Studio 2022 with C++/CLI support is installed
- Build with x64 platform
- Use MSBuild from Visual Studio, not `dotnet build`

---

## Credits

Built with:
- .NET 8 WPF
- CommunityToolkit.Mvvm
- Windows Media Foundation
- DirectX 11 (optional DX11VideoRenderer)
