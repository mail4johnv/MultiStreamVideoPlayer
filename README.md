# Multi-Stream Video Player Solution

A professional multi-stream video player application built with WPF/.NET 8 and Windows Media Foundation, capable of displaying and synchronizing playback of up to 16 video streams simultaneously.

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

## Development History

### December 2025 - Initial Release

#### Bugs Fixed

| # | Issue | Description | Solution |
|---|-------|-------------|----------|
| 1 | NativeMediaPlayer not tracked | Players weren't registered in ViewModel | Added pattern matching for NativeMediaPlayer in RegisterMediaElement |
| 2 | Video not displaying | IMFVideoDisplayControl not retrieved correctly | Use MFGetService after topology is ready |
| 3 | interior_ptr error | C++/CLI compilation error with IID_PPV_ARGS | Use local pointer before assigning to member |
| 4 | Linker error | Missing strmiids.lib | Added to additional dependencies |
| 5 | Events stopped | BeginGetEvent not called after event handling | Call BeginGetEvent at end of Invoke callback |
| 6 | Threading exception | Managed events fired from wrong thread | Use Dispatcher.BeginInvoke in C# event handlers |
| 7 | Source before window | OpenUrl called before HWND created | Defer opening until BuildWindowCore |
| 8 | NaN window size | Width/Height return NaN before layout | Use ActualWidth/ActualHeight with defaults |
| 9 | Invalid HWND | Video renderer created with null window | Added IsWindow validation |

#### DX11VideoRenderer Issues Fixed (v1.0)

| # | Issue | Solution |
|---|-------|----------|
| 1 | GUID format error | Corrected GUID format in solution file |
| 2 | Runtime library mismatch | Changed from /MTd to /MDd |

#### DX11VideoRenderer Issues Fixed (v1.1 - Integration)

| # | Issue | Description | Solution |
|---|-------|-------------|----------|
| 3 | GetService wrong object | `CMediaSink::GetService` returned MediaSink instead of Presenter | Return `m_pPresenter->QueryInterface()` for `MR_VIDEO_RENDER_SERVICE` |
| 4 | Missing acceleration service | `MR_VIDEO_ACCELERATION_SERVICE` not handled | Added handler delegating to Presenter for DXGI device manager |
| 5 | Software buffers ignored | Non-DXGI buffers weren't rendered (blank video) | Implemented `ProcessSoftwareBuffer()` with staging texture upload |
| 6 | Staging texture missing | No mechanism to upload CPU frames to GPU | Added `CreateStagingTexture()` and `m_pStagingTexture` member |
| 7 | Format not detected | Video subtype not mapped to DXGI format | Added NV12/YUY2/RGB32 detection in `SetCurrentMediaType()` |

### Key Implementation Notes

1. **Window Handle Timing**: The HWND must be created (in `BuildWindowCore`) before calling `OpenUrl`. The `OnSourceChanged` callback defers to `BuildWindowCore` if the handle isn't ready.

2. **Event Threading**: Media Foundation callbacks run on a worker thread. Managed events must be marshaled to the UI thread using `Dispatcher.BeginInvoke`.

3. **Topology Ready State**: The `IMFVideoDisplayControl` can only be retrieved after receiving `MESessionTopologyStatus` with `MF_TOPOSTATUS_READY`.

4. **EVR vs DX11**: Currently using EVR (built-in). DX11VideoRenderer is available but disabled. Enable by setting `USE_DX11_RENDERER = true` in MFVideoPlayer.cpp.

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
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ MainViewModel (MVVM)                                       │  │
│  │  - Video stream management                                 │  │
│  │  - Playback control                                        │  │
│  │  - Timeline synchronization                                │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                   │
│  ┌───────────────────────────▼───────────────────────────────┐  │
│  │ NativeMediaPlayer (HwndHost)                               │  │
│  │  - WPF Dependency Properties                               │  │
│  │  - Event Handling                                          │  │
│  │  - HWND Management                                         │  │
│  └───────────────────────────┬───────────────────────────────┘  │
└──────────────────────────────┼──────────────────────────────────┘
                               │ .NET Interop
┌──────────────────────────────▼──────────────────────────────────┐
│ MediaFoundation.Player (C++/CLI)                                 │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ VideoPlayer Class                                          │  │
│  │  - IMFMediaSession                                         │  │
│  │  - IMFMediaSource                                          │  │
│  │  - IMFTopology                                             │  │
│  │  - IMFVideoDisplayControl (EVR)                            │  │
│  └───────────────────────────┬───────────────────────────────┘  │
└──────────────────────────────┼──────────────────────────────────┘
                               │ COM Interfaces
┌──────────────────────────────▼──────────────────────────────────┐
│ Windows Media Foundation (Native)                                │
│                                                                  │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐    │
│  │ Source         │  │ Topology       │  │ Session        │    │
│  │ Resolver       │  │ Builder        │  │ Manager        │    │
│  └────────────────┘  └────────────────┘  └────────────────┘    │
│                                                                  │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐    │
│  │ EVR            │  │ SAR            │  │ Presentation   │    │
│  │ (Video)        │  │ (Audio)        │  │ Clock          │    │
│  └────────────────┘  └────────────────┘  └────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
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

- [ ] Enable and test DX11VideoRenderer
- [ ] Add custom shader effects
- [ ] Implement HDR support
- [ ] Add video snapshot capture
- [ ] Implement playback speed control
- [ ] Add bookmark/marker support
- [ ] Multi-monitor support
- [ ] Settings persistence
- [ ] Recent files list
- [ ] Keyboard shortcuts

---

## Troubleshooting

### Video not playing
1. Check the debug console for error messages
2. Ensure video codecs are installed
3. Verify the video file is not corrupted
4. Try MP4 format (best compatibility)

### Audio issues
- Only the first video has audio (by design)
- Check system volume
- Verify the video has an audio track

### Build errors
- Ensure Visual Studio 2022 with C++/CLI support is installed
- Build with x64 platform
- Use MSBuild from Visual Studio, not `dotnet build`

---

## License

This project is provided as-is for educational and demonstration purposes.

## Credits

Built with:
- .NET 8 WPF
- CommunityToolkit.Mvvm
- Windows Media Foundation
- DirectX 11 (optional DX11VideoRenderer)
