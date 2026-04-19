# DX11VideoRenderer

A static library implementing a custom Media Foundation video renderer using DirectX 11, providing advanced rendering capabilities as an alternative to the built-in Enhanced Video Renderer (EVR).

**Status:** ✅ Built, tested, and integrated | ✅ Active renderer (`USE_DX11_RENDERER = true`)

## Overview

This static library provides a complete `IMFMediaSink` implementation that renders video frames using Direct3D 11, enabling:

- Custom rendering pipelines
- GPU adapter selection
- Real-time video sharpening via HLSL post-process shader
- Runtime color controls (Brightness, Contrast, Hue, Saturation)
- Hardware-accelerated color space conversion
- Extensible rendering architecture

### Architecture

```
Media Foundation (Session) → DX11VideoRenderer (MediaSink)
                                 ↓
                           StreamSink → Scheduler
                                 ↓
                             Presenter → DisplayManager
                                 ↓
                           D3D11 Device (GPU)
                                 ↓
                           DXGI Swap Chain
                                 ↓
                              Screen
```

## Files

| File | Type | Purpose |
|------|------|---------|
| `Activate.cpp/h` | Source | `IMFActivate` factory - Creates media sink with GPU selection |
| `MediaSink.cpp/h` | Source | `IMFMediaSink` + `IDX11VideoColorControl` - Main sink interface |
| `StreamSink.cpp/h` | Source | `IMFStreamSink` implementation - Receives video samples |
| `Presenter.cpp/h` | Source | Video frame presentation - Handles rendering, sharpening, and color controls |
| `Scheduler.cpp/h` | Source | Frame scheduling - Timing and sync |
| `Display.cpp/h` | Source | D3D11/DXGI management - Device and swap chain |
| `Logger.cpp/h` | Source | Thread-safe buffered logging |
| `Common.h` | Header | Synchronization primitives, utilities, and `IDX11VideoColorControl` interface |
| `DX11VideoRenderer.vcxproj` | Project | Visual Studio project file |

## Build Configuration

### Project Settings
- **Configuration Type:** Static Library (.lib)
- **Platform Toolset:** Visual Studio 2022 (v143)
- **C++ Standard:** C++17
- **Character Set:** Unicode
- **Runtime Library:** Multi-threaded DLL (`/MD` or `/MDd`)
- **Platform:** x64 only

### Required Libraries
```
d3d11.lib       # Direct3D 11 core
dxgi.lib        # DXGI (adapter, swap chain)
mf.lib          # Media Foundation core
mfuuid.lib      # Media Foundation GUIDs
mfplat.lib      # Media Foundation platform
strmiids.lib    # DirectShow GUIDs
```

### Linker Settings
- Debug: `mf.lib;mfuuid.lib;strmiids.lib;d3d11.lib;dxgi.lib;%(AdditionalDependencies)`
- Release: Same as Debug

## API Reference

### CDXVA2RendererActivate Class

**Purpose:** Factory class implementing `IMFActivate` to create the media sink.

```cpp
// Static creation
HRESULT CreateInstance(HWND hwndVideo, IMFActivate** ppActivate, UINT gpuAdapterIndex = 0);

// IUnknown methods
STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
STDMETHODIMP_(ULONG) AddRef();
STDMETHODIMP_(ULONG) Release();

// IMFActivate methods
STDMETHODIMP ActivateObject(REFIID riid, void** ppv);
STDMETHODIMP ShutdownObject();
```

### CMediaSink Class

**Purpose:** Implements `IMFMediaSink` interface.

```cpp
// Add stream sink
STDMETHODIMP AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType* pMediaType, IMFStreamSink** ppStreamSink);

// Get stream sink
STDMETHODIMP GetStreamSinkByIndex(DWORD dwIndex, IMFStreamSink** ppStreamSink);
```

### CStreamSink Class

**Purpose:** Implements `IMFStreamSink` interface.

```cpp
// Process samples from Media Foundation
STDMETHODIMP ProcessSample(IMFSample* pSample);

// Media type handling
STDMETHODIMP SetCurrentMediaType(IMFMediaType* pMediaType);
```

### CPresenter Class

**Purpose:** Handles video frame rendering, post-process sharpening, and color controls.

```cpp
// Initialization
HRESULT Initialize(HWND hwndVideo, UINT gpuAdapterIndex = 0);

// Frame processing
HRESULT ProcessFrame(IMFSample* pSample);
HRESULT PresentFrame();

// Post-processing controls (runtime, no topology change needed)
HRESULT SetUserSharpenSliderValue(float sliderValue);   // 0.0 – 1.0
HRESULT SetUserSharpenThreshold(float thresholdValue);  // 0.0 – 0.02 recommended
HRESULT SetColorControls(int brightness, int contrast, int hue, int saturation); // -127 to +127
```

### CScheduler Class

**Purpose:** Frame timing and scheduling.

```cpp
// Timing control
HRESULT Start(MFTIME rtStartTime);
HRESULT ScheduleSample(IMFSample* pSample, BOOL bPresentNow);
```

### CDisplayManager Class

**Purpose:** Direct3D and DXGI management.

```cpp
// Device creation
HRESULT Initialize(HWND hwndVideo);

// Swap chain
HRESULT CreateSwapChain(UINT width, UINT height);
HRESULT Present();
```

## Post-Processing Pipeline

The renderer applies effects in two stages after the D3D11 video processor:

### Stage 1 — Video Processor Filters (playing frames)
`ApplyVideoProcessorColorControls()` maps each color slider value to the corresponding D3D11 video-processor filter (`VideoProcessorSetStreamFilter`):
- `D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS`
- `D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST`
- `D3D11_VIDEO_PROCESSOR_FILTER_HUE`
- `D3D11_VIDEO_PROCESSOR_FILTER_SATURATION`

### Stage 2 — HLSL Sharpen/Color Pass (every repaint, including paused)
An intermediate render target is blitted through `PSMain` (embedded HLSL shader). The shader:
1. Converts RGB → YCbCr
2. Applies Laplacian edge-aware sharpening on **Y (luma) only**
3. Applies hue rotation and saturation scaling on **Cb/Cr**
4. Applies brightness offset and contrast scaling on **Y**
5. Converts back to RGB

Constant buffer `SharpenSettings (b0)`:
```hlsl
float fSharpenStrength;  // 0.0 – 20.0 (slider × 20)
float fThreshold;        // edge threshold (0.0 – ~0.02)
float fBrightness;       // -0.5 – +0.5
float fContrast;         // 0.0 – 2.0 (1.0 = neutral)
float fHueRadians;       // -π – +π
float fSaturation;       // 0.0 – 2.0 (1.0 = neutral)
```

This Stage 2 pass is the **only** color path active for paused frames, ensuring slider changes are always visible.

### `IDX11VideoColorControl` Interface

Declared in `Common.h`, implemented by both `CPresenter` and `CMediaSink` (which forwards to presenter):

```cpp
MIDL_INTERFACE("3F415F8C-4F66-4F46-9F91-6B7B7CF4E5A1")
IDX11VideoColorControl : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetColorControls(
        int brightness, int contrast, int hue, int saturation) = 0;
};
```

The `MediaFoundation.Player` layer retrieves this via `QueryInterface` on the `IMFVideoDisplayControl` pointer and invokes it directly.


## Supported Video Formats

### Input Formats (from Media Foundation)
```cpp
MFVideoFormat_NV12    // H.264, VP9, others (recommended)
MFVideoFormat_YUY2    // YUV 4:2:2 packed
MFVideoFormat_RGB32   // RGB 32-bit
MFVideoFormat_ARGB32  // ARGB 32-bit
```

### Output Format
```cpp
DXGI_FORMAT_B8G8R8A8_UNORM  // 32-bit RGBA
```

## Sample Processing Flow

```
Media Foundation
    ↓
CStreamSink::ProcessSample(IMFSample)
    ↓
CScheduler::ScheduleSample(IMFSample)
    ↓
Presentation Clock
    ↓
CPresenter::ProcessFrame(IMFSample)
    ↓
CPresenter::PresentFrame()
    ↓
Screen
```

## Integration with Media Foundation

### Creating the Renderer

```cpp
CDXVA2RendererActivate* pActivate = nullptr;
HRESULT hr = CDXVA2RendererActivate::CreateInstance(hwndVideo, &pActivate, gpuAdapterIndex);

if (SUCCEEDED(hr))
{
    IMFTopologyNode* pOutputNode = nullptr;
    MFCreateTopologyNode(MFTopology_OutputNode, &pOutputNode);
    pOutputNode->SetObject(pActivate);
    // ... add to topology ...
}
```

## Debug Logging

### Debug Output Prefixes

| Prefix | Component | Information |
|--------|-----------|-------------|
| `[DX11Activate]` | Activate | ActivateObject, ShutdownObject calls |
| `[DX11MediaSink]` | MediaSink | Sink state, stream management |
| `[DX11StreamSink]` | StreamSink | ProcessSample calls, format info |
| `[DX11Scheduler]` | Scheduler | Start, ScheduleSample, PresentSample |
| `[DX11Presenter]` | Presenter | Initialize, SetVideoWindow, ProcessFrame |

### Viewing Debug Output

**Visual Studio:**
```
Debug → Windows → Output
Select "Debug" output pane
```

**DebugView (Sysinternals):**
- Download DebugView.exe
- Run as administrator
- View all OutputDebugStringA calls

## Thread Safety

### Thread-Safe Components
- `CThreadSafeLogger` - Buffered I/O with critical sections
- `CCritSec` - Critical section wrapper for synchronization
- All Media Foundation callbacks marshaled appropriately

### Not Thread-Safe
- D3D11 device context (single-threaded)
- IMFSample processing (must be sequential)
- State transitions (use critical sections)

## Performance Considerations

### Typical Performance
- **1080p 30fps:** 1-3% GPU, minimal CPU
- **4K 30fps:** 5-10% GPU, minimal CPU
- **16x 1080p:** 40-60% GPU (depends on GPU model)

### Optimization Tips
1. **Select High-End GPU** for multi-stream scenarios
2. **Hardware Decode** - Use DXVA-2 for H.264/H.265
3. **Format Selection** - Prefer NV12 (native D3D11 format)
4. **Logging** - Disable logging in Release builds
5. **VSync** - Leave enabled for smooth presentation

## Known Limitations

1. **Single Video Stream** - Only one stream sink supported
2. **Progressive Frames Only** - No interlaced video support
3. **No Overlay** - Text/graphics overlay not implemented
4. **No HDR** - HDR10 not yet supported
5. **GPU-Specific** - Requires DX11-capable GPU

## Building and Testing

### Build Steps
```bash
# 1. Open MultiStreamVideoPlayer.sln
# 2. Select DX11VideoRenderer project
# 3. Set x64 platform
# 4. Build → Build Project (Ctrl+B)

# Output:
# DX11VideoRenderer/bin/Debug/DX11VideoRenderer.lib
# DX11VideoRenderer/bin/Release/DX11VideoRenderer.lib
```

### Enable in Media Foundation Player

Edit `MediaFoundation.Player/MFVideoPlayer.cpp`:

```cpp
#define USE_DX11_RENDERER false    // Change to true
```

Then rebuild `MediaFoundation.Player.dll`.

## Future Enhancements

- [ ] Multi-stream sink support
- [x] Custom shader effects (sharpening)
- [x] Runtime color controls (Brightness/Contrast/Hue/Saturation)
- [ ] Color space conversion (10-bit, HDR)
- [ ] Interlaced video support
- [ ] Video effect composition
- [ ] Performance counters
- [ ] Hardware accelerated scaling
- [ ] Deinterlacing support

## License

Apache License 2.0 - Inherited from parent project

See [../LICENSE](../LICENSE) for full details

## References

- [Media Foundation Documentation](https://learn.microsoft.com/en-us/windows/win32/medfound/)
- [DirectX 11 Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/)
- [DXGI Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-dxgi)
- [Windows-classic-samples](https://github.com/Microsoft/Windows-classic-samples)
- [IMFActivate Interface](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imfactivate)
- [IMFMediaSink Interface](https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nn-mfidl-imfmediasink)
