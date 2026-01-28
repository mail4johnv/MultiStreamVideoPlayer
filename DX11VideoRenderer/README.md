# DX11 Video Renderer

A DirectX 11 custom video renderer for Media Foundation, based on Microsoft's Windows-classic-samples DX11VideoRenderer.

## Overview

This is a static library that implements a custom Media Foundation video renderer using DirectX 11. It can be used as an alternative to the built-in EVR (Enhanced Video Renderer) when you need more control over video rendering.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Media Foundation Pipeline                                    │
│                                                              │
│  Media Source → Decoder → [DX11VideoRenderer] → Display     │
│                                                              │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ DX11VideoRenderer Components                                 │
│                                                              │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐    │
│  │  Activate    │──▶│  MediaSink   │──▶│  StreamSink  │    │
│  │  (Factory)   │   │  (IMFMediaSink)  │  (IMFStreamSink)  │
│  └──────────────┘   └──────────────┘   └──────┬───────┘    │
│                                               │             │
│                                               ▼             │
│                     ┌──────────────┐   ┌──────────────┐    │
│                     │   Display    │◀──│  Presenter   │    │
│                     │ (D3D11/DXGI) │   │ (Scheduling) │    │
│                     └──────────────┘   └──────────────┘    │
│                                               │             │
│                                               ▼             │
│                                        ┌──────────────┐    │
│                                        │  Scheduler   │    │
│                                        │ (Frame Timing)│   │
│                                        └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

## Files

| File | Description |
|------|-------------|
| `Common.h` | Thread synchronization primitives, SafeRelease template, CBase, CCritSec, CAutoLock |
| `Display.h/cpp` | CDisplayManager - Manages D3D11 device, swap chain, and DXGI resources |
| `Presenter.h/cpp` | CPresenter - Handles video frame presentation and timing |
| `Scheduler.h/cpp` | CScheduler - Frame scheduling for smooth playback |
| `StreamSink.h/cpp` | CStreamSink - Implements IMFStreamSink for receiving video samples |
| `MediaSink.h/cpp` | CMediaSink - Implements IMFMediaSink as the main sink interface |
| `Activate.h/cpp` | CDXVA2RendererActivate - IMFActivate factory for creating the renderer |

## Key Classes

### CDXVA2RendererActivate
Factory class implementing `IMFActivate`. Creates the DX11 video renderer media sink.

```cpp
// Create the renderer activate object
IMFActivate* pActivate = nullptr;
HRESULT hr = CreateDX11VideoRendererActivate(hwnd, &pActivate);
```

### CMediaSink
Main media sink implementing `IMFMediaSink`. Manages the stream sink and coordinates with the presenter.

### CStreamSink
Stream sink implementing `IMFStreamSink` and `IMFMediaEventGenerator`. Receives video samples from the pipeline.

### CPresenter
Handles video presentation:
- Manages DirectX 11 resources
- Schedules frame presentation
- Handles resize and format changes

### CDisplayManager
Manages the DirectX 11 rendering infrastructure:
- D3D11 device and device context
- DXGI swap chain
- Texture and render target views

### CScheduler
Handles frame timing for smooth playback at the correct frame rate.

## Build Configuration

### Project Settings
- **Configuration Type**: Static Library (.lib)
- **Platform Toolset**: Visual Studio 2022 (v143)
- **C++ Standard**: C++17
- **Character Set**: Unicode
- **Runtime Library**: Multi-threaded Debug DLL (/MDd) for Debug, Multi-threaded DLL (/MD) for Release

### Required Libraries
- `d3d11.lib` - DirectX 11
- `dxgi.lib` - DXGI
- `mf.lib` - Media Foundation
- `mfuuid.lib` - Media Foundation GUIDs
- `mfplat.lib` - Media Foundation Platform

### Required Headers
- `<d3d11.h>` - DirectX 11
- `<dxgi1_2.h>` - DXGI 1.2
- `<mfapi.h>` - Media Foundation API
- `<mfidl.h>` - Media Foundation interfaces
- `<evr.h>` - Enhanced Video Renderer (for interfaces)

## Usage in MediaFoundation.Player

### Current Status: DISABLED

The DX11 renderer is currently disabled in favor of the built-in EVR. To enable:

1. Open `MFVideoPlayer.cpp`
2. Change `#define USE_DX11_RENDERER false` to `true`
3. Rebuild the solution

### Integration Code

```cpp
// In MFVideoPlayer.cpp - CreateOutputNode
if (USE_DX11_RENDERER) {
    hr = CreateDX11VideoRendererActivate(m_hwndVideo, &pActivate);
} else {
    hr = MFCreateVideoRendererActivate(m_hwndVideo, &pActivate);
}
```

## Advantages over EVR

| Feature | EVR | DX11VideoRenderer |
|---------|-----|-------------------|
| Hardware acceleration | ✅ | ✅ |
| Custom shaders | ❌ | ✅ |
| Direct texture access | Limited | Full |
| Custom compositing | ❌ | ✅ |
| Frame manipulation | ❌ | ✅ |
| HDR support | Limited | Customizable |

## Development History

### December 2025 - Initial Implementation

Created as a static library based on Microsoft's Windows-classic-samples.

#### Issues Fixed

1. **GUID Format Error**
   - Project GUID in solution file had incorrect format
   - Fixed by using proper GUID format without extra braces

2. **Runtime Library Mismatch**
   - Initially used `/MTd` (static runtime)
   - Changed to `/MDd` to match MediaFoundation.Player project
   - Prevents linker conflicts

3. **Intermediate Directory Conflict**
   - Warning about shared intermediate directory with other projects
   - Non-critical warning, can be fixed by using unique obj directory

### December 2025 - Integration Fixes (v1.1)

Major fixes to enable DX11VideoRenderer to work correctly with Media Foundation pipeline.

#### Critical Bugs Fixed

4. **GetService Returning Wrong Object (Critical)**
   - **Issue**: `CMediaSink::GetService` returned `this` (MediaSink) for `MR_VIDEO_RENDER_SERVICE`
   - **Problem**: Media Foundation couldn't get `IMFVideoDisplayControl` since it's implemented by `CPresenter`, not `CMediaSink`
   - **Fix**: Changed to return `m_pPresenter->QueryInterface(riid, ppvObject)`
   - **File**: `MediaSink.cpp`

5. **Missing Video Acceleration Service**
   - **Issue**: `MR_VIDEO_ACCELERATION_SERVICE` was not handled in `GetService`
   - **Problem**: Media Foundation couldn't get DXGI device manager for hardware-accelerated decoding
   - **Fix**: Added `MR_VIDEO_ACCELERATION_SERVICE` handler that delegates to `m_pPresenter->GetService()`
   - **File**: `MediaSink.cpp`

6. **Software Buffer Not Processed**
   - **Issue**: When Media Foundation sends software (non-DXGI) buffers, `ProcessFrame` just set `m_bCanProcessNextSample = TRUE` without rendering
   - **Problem**: No video displayed when hardware acceleration not used
   - **Fix**: Implemented `ProcessSoftwareBuffer()` method that:
     - Locks the IMF2DBuffer/IMFMediaBuffer
     - Creates a staging D3D11 texture
     - Copies software buffer to GPU texture
     - Processes using video processor
   - **Files**: `Presenter.cpp`, `Presenter.h`

7. **Staging Texture Management**
   - **Issue**: No mechanism to upload software frames to GPU
   - **Fix**: Added `CreateStagingTexture()` method and `m_pStagingTexture` member
   - **File**: `Presenter.cpp`, `Presenter.h`

8. **Media Type Format Detection**
   - **Issue**: `SetCurrentMediaType` didn't detect DXGI format from video subtype
   - **Fix**: Added format detection for NV12, YUY2, RGB32/ARGB32 subtypes
   - **File**: `Presenter.cpp`

#### Debug Logging Added

Added comprehensive debug logging using `OutputDebugStringA` to trace:
- `[DX11Renderer]` - StreamSink events (ProcessSample)
- `[DX11Scheduler]` - Scheduler events (Start, ScheduleSample, PresentSample)
- `[DX11Presenter]` - Presenter events (Initialize, SetVideoWindow, ProcessFrame, PresentFrame)

Use **DebugView** (Sysinternals) or Visual Studio Output window to see debug output.

## API Reference

### GetService Flow (Fixed)

```cpp
// Media Foundation queries for video display control:
// 1. MF calls IMFGetService::GetService on MediaSink
// 2. MediaSink delegates to Presenter for MR_VIDEO_RENDER_SERVICE
// 3. Presenter returns IMFVideoDisplayControl interface

// CMediaSink::GetService (fixed implementation)
if (guidService == MR_VIDEO_RENDER_SERVICE) {
    return m_pPresenter->QueryInterface(riid, ppvObject);
}
else if (guidService == MR_VIDEO_ACCELERATION_SERVICE) {
    return m_pPresenter->GetService(guidService, riid, ppvObject);
}
```

### Sample Processing Flow

```
1. Media Foundation sends sample to StreamSink::ProcessSample
2. StreamSink calls ProcessSampleInternal
3. ProcessSampleInternal schedules via Scheduler::ScheduleSample
4. Scheduler calls OnSampleReady → Presenter::ProcessFrame
5. ProcessFrame checks buffer type:
   - DXGI buffer: Direct ProcessFrameUsingVideoProcessor
   - Software buffer: ProcessSoftwareBuffer → Copy to staging → ProcessFrameUsingVideoProcessor
6. Scheduler calls Presenter::PresentFrame
7. PresentFrame calls DisplayManager::Present (swap chain)
```

## Future Improvements

- [ ] Add IMFVideoDisplayControl implementation for compatibility
- [ ] Implement custom pixel shaders for video effects
- [ ] Add support for multiple output formats
- [ ] Implement HDR tone mapping
- [ ] Add frame capture functionality
- [ ] Optimize for 4K video playback
- [ ] Add support for stereoscopic 3D video
- [ ] Improve software buffer performance with staging pool

## References

- [Microsoft DX11VideoRenderer Sample](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/DX11VideoRenderer)
- [Media Foundation Architecture](https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-architecture)
- [Custom Media Sinks](https://docs.microsoft.com/en-us/windows/win32/medfound/media-sinks)
- [DirectX 11 Programming Guide](https://docs.microsoft.com/en-us/windows/win32/direct3d11/dx-graphics-overviews)
