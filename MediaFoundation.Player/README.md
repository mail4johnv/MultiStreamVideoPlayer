# Media Foundation Video Player

This is a C++/CLI wrapper around Microsoft Media Foundation for high-performance video playback in .NET applications.

## Features

- **Native Media Foundation API**: Full access to MF Session and Topology
- **Hardware Acceleration**: Uses EVR (Enhanced Video Renderer) for GPU-accelerated rendering
- **Proper Playback Control**: Play, Pause, Stop, Seek operations
- **Audio/Video Support**: Handles both audio and video streams
- **Volume Control**: Per-channel volume and mute support
- **Event Notifications**: MediaOpened, MediaEnded, MediaFailed events
- **DX11 Video Renderer**: Optional DirectX 11 custom video renderer (static library)

## Architecture

### C++/CLI Layer (`MFVideoPlayer.cpp/h`)
- `VideoPlayer` class: Main player implementation
- Creates and manages IMFMediaSession
- Builds topology from media source
- Handles EVR presenter for video rendering
- Manages audio renderer for audio playback

### C# Wrapper (`NativeMediaPlayer.cs`)
- `NativeMediaPlayer` class: WPF HwndHost-based control
- Wraps C++/CLI VideoPlayer
- Exposes WPF dependency properties
- Provides familiar MediaElement-like API

## Usage

```csharp
<controls:NativeMediaPlayer Source="{Binding FilePath}"
                            Tag="{Binding}"
                            Volume="0.5"
                            Loaded="MediaPlayer_Loaded" />
```

```csharp
private void MediaPlayer_Loaded(object sender, RoutedEventArgs e)
{
    if (sender is NativeMediaPlayer player)
    {
        player.MediaOpened += Player_MediaOpened;
        player.Play();
    }
}
```

## Build Requirements

- Visual Studio 2022 or later
- C++/CLI support (.NET Core)
- Windows SDK 10.0 or later
- Platform: x64 only

## Media Foundation Components Used

- **IMFMediaSession**: Session management and playback control
- **IMFTopology**: Playback pipeline graph
- **IMFSourceResolver**: Media file opening
- **IMFVideoDisplayControl**: Video rendering to HWND
- **IMFAudioStreamVolume**: Audio volume control
- **EVR (Enhanced Video Renderer)**: Hardware-accelerated video rendering
- **SAR (Streaming Audio Renderer)**: Audio output

## Performance

This implementation provides better performance than MediaElement for:
- Multiple simultaneous video streams
- High-resolution video playback
- Frame-accurate seeking
- Low-latency video rendering

---

## Development History & Bug Fixes

### Version 1.0 - Initial Implementation (December 2025)

#### Bugs Fixed

1. **NativeMediaPlayer Not Tracked in MainViewModel**
   - **Issue**: `_nativePlayers` list was not being populated when `RegisterMediaElement` was called
   - **Fix**: Added proper pattern matching for `Controls.NativeMediaPlayer` in `RegisterMediaElement`

2. **IMFVideoDisplayControl Activation Issue**
   - **Issue**: Video display control was being retrieved incorrectly, causing video to not render
   - **Fix**: Use `MFGetService` with `MR_VIDEO_RENDER_SERVICE` after topology is ready (MESessionTopologyStatus with MF_TOPOSTATUS_READY)

3. **interior_ptr Compilation Error**
   - **Issue**: `IID_PPV_ARGS(&m_pVideoDisplay)` failed with interior_ptr error
   - **Fix**: Use local pointer variable before assigning to member

4. **Missing strmiids.lib Linker Error**
   - **Issue**: Unresolved external symbol for DirectShow GUIDs
   - **Fix**: Added `strmiids.lib` to additional dependencies

5. **CPlayerCallback::Invoke Not Looping**
   - **Issue**: Only one Media Foundation event was processed, then events stopped
   - **Fix**: Call `m_pSession->BeginGetEvent(this, nullptr)` at end of Invoke to continue receiving events

6. **Threading Exception on MediaOpened**
   - **Issue**: Firing managed events from Media Foundation callback thread caused exceptions
   - **Fix**: MediaOpened event is now fired from the MF callback thread; C# side uses `Dispatcher.BeginInvoke` to marshal to UI thread

7. **Source Opened Before Window Ready**
   - **Issue**: `OpenUrl` was called in `OnSourceChanged` before `BuildWindowCore` created the window handle
   - **Fix**: `OnSourceChanged` now checks if `_hwndHost != IntPtr.Zero`; if not, defers to `BuildWindowCore`

8. **Window Size Using NaN Values**
   - **Issue**: `Width` and `Height` properties returned NaN in WPF before layout
   - **Fix**: Use `ActualWidth` and `ActualHeight` with fallback to 640x480 if still 0

9. **Invalid Window Handle Validation**
   - **Issue**: Video renderer was created with NULL or invalid window handle
   - **Fix**: Added `IsWindow(m_hwndVideo)` check before creating video renderer

---

## DX11 Video Renderer (Optional)

The solution includes a DirectX 11 custom video renderer based on Microsoft's Windows-classic-samples. This is currently **disabled** but ready for use.

### Files (in `DX11VideoRenderer/` folder)
- `Common.h` - Thread synchronization, SafeRelease, base classes
- `Display.h/cpp` - D3D11 device and swap chain management
- `Presenter.h/cpp` - Video frame presentation
- `Scheduler.h/cpp` - Frame timing and scheduling
- `StreamSink.h/cpp` - IMFStreamSink implementation
- `MediaSink.h/cpp` - IMFMediaSink implementation
- `Activate.h/cpp` - IMFActivate factory for creating the renderer

### Enabling DX11 Renderer

In `MFVideoPlayer.cpp`, change:
```cpp
#define USE_DX11_RENDERER false  // Change to true
```

### DX11 Renderer Bugs Fixed (v1.0)

1. **GUID Format in Solution File**
   - **Issue**: Project GUID had incorrect format with curly braces
   - **Fix**: Used proper GUID format in .sln file

2. **Runtime Library Mismatch**
   - **Issue**: DX11VideoRenderer used `/MTd` while other projects used `/MDd`
   - **Fix**: Changed to `/MDd` (Multi-threaded Debug DLL) for consistency

### DX11 Renderer Bugs Fixed (v1.1 - Integration Fixes)

3. **GetService Returning Wrong Object (Critical)**
   - **Issue**: `CMediaSink::GetService` returned MediaSink instead of Presenter for `MR_VIDEO_RENDER_SERVICE`
   - **Fix**: Changed to return `m_pPresenter->QueryInterface()` so Media Foundation can get `IMFVideoDisplayControl`

4. **Missing Video Acceleration Service**
   - **Issue**: `MR_VIDEO_ACCELERATION_SERVICE` not handled
   - **Fix**: Added handler to delegate to Presenter for DXGI device manager access

5. **Software Buffer Not Rendered**
   - **Issue**: Non-DXGI (software) buffers were ignored, causing blank video
   - **Fix**: Implemented `ProcessSoftwareBuffer()` to copy software frames to GPU staging texture

6. **Media Type Format Detection**
   - **Issue**: Video format not detected from media type subtype
   - **Fix**: Added NV12/YUY2/RGB32 format detection in `SetCurrentMediaType()`

### DX11 Renderer Debug Output

Debug logging added using `OutputDebugStringA`. View with DebugView or Visual Studio:
- `[DX11Renderer]` - StreamSink ProcessSample events
- `[DX11Scheduler]` - Scheduler Start/ScheduleSample/PresentSample
- `[DX11Presenter]` - Presenter Initialize/ProcessFrame/PresentFrame

---

## Future Improvements

- [ ] Enable and test DX11VideoRenderer for custom rendering scenarios
- [ ] Add shader-based video effects
- [ ] Implement hardware-accelerated color space conversion
- [ ] Add support for HDR video
- [ ] Implement smooth slow-motion playback
- [ ] Add video snapshot/frame capture functionality
