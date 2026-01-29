# MediaFoundation.Player

A C++/CLI wrapper around Windows Media Foundation providing high-performance video playback with GPU selection and optional DirectX 11 rendering for .NET applications.

## Overview

This project wraps the complex Media Foundation COM interfaces in a managed C++/CLI layer, making it easy to integrate professional video playback into .NET WPF applications.

**Architecture:** C++/CLI → Media Foundation → Video Renderer (EVR or DX11)

## Features

### Media Foundation Integration
- Direct access to `IMFMediaSession` for playback control
- Topology building for audio/video routing
- Hardware accelerated video decoding (DXVA-2)
- Multi-format support via Media Foundation decoders

### Playback Control
- **Play/Pause/Stop** - Full playback control
- **Seek** - Precise seeking to any position
- **Volume** - Volume adjustment (0-100)
- **Mute** - Toggle audio on/off
- **Duration** - Query video duration
- **Playback rate** - Adjust playback speed (via topology)

### GPU Management
- **Enumerate GPUs** - Discover all DXGI adapters
- **GPU Properties** - Query memory, vendor, device ID
- **GPU Selection** - Choose adapter for rendering/decoding
- **Fallback** - Automatic fallback to default GPU

### Video Output
- **EVR (Default)** - Enhanced Video Renderer
  - Built-in Windows component
  - Hardware accelerated
  - Proven stability
  
- **DX11 (Optional)** - DirectX 11 custom renderer
  - Advanced customization
  - Shader support ready
  - Custom effects capability

### Logging
- **Thread-safe logging** - Safe for multi-threaded Media Foundation
- **Buffered I/O** - High performance logging
- **Auto-flush** - Configurable flush interval
- **Managed interface** - Easy to call from C#

## Build

### Prerequisites
- Visual Studio 2022 (C++ tools required)
- Windows SDK 10.0+
- .NET 8 SDK
- Platform: **x64 only**

### Project Configuration
- **Type:** DLL (Dynamic Library)
- **Runtime:** Multi-threaded DLL (`/MD` or `/MDd`)
- **C++ Standard:** C++17
- **CLR Support:** .NET Core
- **Platform:** x64

### Build Steps

```bash
# From Visual Studio
# 1. Open MultiStreamVideoPlayer.sln
# 2. Select MediaFoundation.Player project
# 3. Set x64 platform
# 4. Build Solution (F6)

# Output
# bin\Debug\MediaFoundation.Player.dll
# bin\Release\MediaFoundation.Player.dll
```

### Required Libraries
```
mf.lib          # Media Foundation core
mfplat.lib      # Media Foundation platform
mfuuid.lib      # Media Foundation GUIDs
mfreadwrite.lib # Media Foundation read/write
evr.lib         # Enhanced Video Renderer
shlwapi.lib     # Shell utilities
strmiids.lib    # DirectShow GUIDs
d3d11.lib       # Direct3D 11 (for DX11 option)
dxgi.lib        # DXGI (GPU enumeration)
```

### Required Headers
```cpp
<d3d11.h>       // DirectX 11
<dxgi1_2.h>     // DXGI 1.2+
<mfapi.h>       // Media Foundation API
<mfidl.h>       // Media Foundation interfaces
<evr.h>         // Enhanced Video Renderer
```

### Linker Settings
- Debug: `mf.lib;mfuuid.lib;strmiids.lib;d3d11.lib;dxgi.lib;%(AdditionalDependencies)`
- Release: Same as Debug

## API Reference

### VideoPlayer Class

#### Constructors
```cpp
VideoPlayer::VideoPlayer()
```

#### Playback Methods
```cpp
void Play()                           // Start playback
void Pause()                          // Pause playback
void Stop()                           // Stop playback
void Seek(MFTIME position)            // Seek to position (100-nanosecond units)
void OpenUrl(String^ url)             // Open and load video file
```

#### Properties
```cpp
TimeSpan Duration { get; }            // Video duration
TimeSpan Position { get; set; }       // Current position
double Volume { get; set; }           // Volume (0.0 - 1.0)
bool IsMuted { get; set; }            // Mute state
PlayerState State { get; }            // Current playback state
IntPtr VideoWindow { get; set; }      // HWND for rendering
bool HasVideo { get; }                // Check if video stream present
bool HasAudio { get; }                // Check if audio stream present
```

#### GPU Management
```cpp
List<GPUAdapterInfo^>^ EnumerateGPUs()         // Enumerate available GPUs
void SetGPUAdapter(unsigned int adapterIndex)  // Select GPU (0-based index)
```

#### Events
```cpp
event EventHandler^ MediaOpened;      // Fired when video loaded
event EventHandler^ MediaEnded;       // Fired when playback ends
event EventHandler^ MediaFailed;      // Fired on error
```

### ManagedLogger Class

#### Static Methods
```cpp
static void Initialize(String^ logFilePath)   // Start logging to file
static void Log(String^ message)               // Log informational message
static void LogWarning(String^ message)        // Log warning
static void LogError(String^ message)          // Log error
static void LogInfo(String^ message)           // Log info
static bool IsInitialized()                    // Check if logger initialized
static void Flush()                            // Flush log to disk
```

## Usage from C# (WPF)

### Creating Player
```csharp
using MediaFoundation.Player;

// In WPF HwndHost-derived control
IntPtr hwnd = /* window handle from BuildWindowCore */;
var player = new VideoPlayer();
player.VideoWindow = hwnd;
```

### Playback Control
```csharp
// Open and play video
player.OpenUrl(@"C:\video.mp4");
player.Play();

// Seek to 30 seconds
player.Position = TimeSpan.FromSeconds(30);

// Control volume
player.Volume = 0.5;  // 50%
player.IsMuted = true;
```

### GPU Selection
```csharp
// Get available GPUs
var gpus = player.EnumerateGPUs();

foreach (var gpu in gpus)
{
    Console.WriteLine($"{gpu.Description}: {gpu.DedicatedVideoMemory / 1024 / 1024} MB");
}

// Select specific GPU
player.SetGPUAdapter(1);  // Second GPU
```

### Event Handling
```csharp
player.MediaOpened += (s, e) => 
{
    Console.WriteLine("Video loaded, duration: " + player.Duration);
};

player.MediaEnded += (s, e) =>
{
    Console.WriteLine("Playback finished");
};
```

## Implementation Details

### Media Foundation Pipeline

```
User Code
    ↓
VideoPlayer (C++/CLI)
    ↓
IMFMediaSession → Topology → Source Reader/EVR
    ↓
Media Foundation Decoders
    ↓
GPU/Software Rendering
```

### Event Handling

- Media Foundation callbacks execute on **worker thread**
- Events marshaled to UI thread via **Dispatcher.BeginInvoke()**
- Prevents cross-thread access violations
- Ensures thread-safe operations

### GPU Enumeration

```cpp
// DXGI adapter enumeration
IDXGIFactory* pFactory;
IDXGIAdapter* pAdapter;

for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) == S_OK; ++i)
{
    DXGI_ADAPTER_DESC desc;
    pAdapter->GetDesc(&desc);
    // Query GPU info...
}
```

### DX11 Renderer Integration

To enable optional DX11 renderer:

```cpp
// In MFVideoPlayer.cpp, change:
#define USE_DX11_RENDERER false    // Change to true

// Then rebuild
```

## Threading Model

### Safe for Multi-Threaded Use
- All public methods thread-safe
- Critical sections protect shared state
- Media Foundation callbacks handled internally
- UI thread marshaling handled automatically

### Not Safe
- Do not call methods from finalizer
- Do not hold locks while calling methods
- Do not call from DLL unload

## Error Handling

### Debugging
Enable debug logging:
```csharp
ManagedLogger.Initialize("mfplayer_debug.log");
```

Check Visual Studio Output window (Debug builds):
- Media Foundation events
- GPU selection info
- Playback state changes

## Troubleshooting

### Video doesn't play
- **Check:** Video codec supported (try with Windows Media Player)
- **Verify:** GPU drivers current
- **Enable:** Debug logging
- **Look:** Event Viewer for Media Foundation errors

### Black screen / no rendering
- **Verify:** HWND is valid
- **Check:** Topology ready before rendering
- **Try:** Different GPU adapter
- **Test:** With EVR (default) before DX11

### GPU enumeration fails
- **Ensure:** DXGI drivers installed
- **Check:** Multi-GPU enabled in BIOS
- **Try:** Latest GPU drivers
- **Fallback:** Code defaults to GPU 0 on error

## Performance

### Typical Performance
- **Single 1080p H.264:** 1-2% CPU, negligible GPU
- **16x 1080p H.264:** 10-15% CPU, 20-30% GPU
- **4x 4K H.264:** 15-20% CPU, 40-50% GPU

### Optimization Tips
- Use DXVA-2 hardware decoding
- Keep videos same resolution
- Use SSD for file I/O
- Close other GPU-intensive apps
- Upgrade GPU for 4K or 16-stream setups

## Files

| File | Purpose |
|------|---------|
| `MFVideoPlayer.h` | Main class definition |
| `MFVideoPlayer.cpp` | Implementation (Media Foundation) |
| `ManagedLogger.h` | Managed logging wrapper |
| `ManagedLogger.cpp` | Logger implementation |
| `LoggerWrapper.cpp` | Native logging bridge |
| `AssemblyInfo.cpp` | Assembly metadata |
| `MediaFoundation.Player.vcxproj` | Project file |

## License

Apache License 2.0 - Inherited from parent project

See [../LICENSE](../LICENSE) for details

## References

- [Media Foundation Documentation](https://learn.microsoft.com/en-us/windows/win32/medfound/)
- [DXGI Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-dxgi)
- [Windows-classic-samples](https://github.com/Microsoft/Windows-classic-samples)
- [C++/CLI Reference](https://learn.microsoft.com/en-us/cpp/dotnet/cpp-cli-language-reference)
