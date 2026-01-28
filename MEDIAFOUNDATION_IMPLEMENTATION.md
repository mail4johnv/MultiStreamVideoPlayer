# Media Foundation Video Player Implementation

## Overview

A complete Media Foundation video player implementation with C++/CLI wrapper and WPF integration.

## Components

### 1. C++/CLI Native Player (`MediaFoundation.Player` project)

**Files:**
- `MediaFoundation.Player.vcxproj` - C++/CLI project file
- `MFVideoPlayer.h` - Header with VideoPlayer class definition
- `MFVideoPlayer.cpp` - Full Media Foundation implementation
- `AssemblyInfo.cpp` - Assembly metadata
- `README.md` - Documentation

**Key Features:**
- Native IMFMediaSession management
- Topology-based playback pipeline
- EVR (Enhanced Video Renderer) for video
- SAR (Streaming Audio Renderer) for audio
- Full playback control (Play/Pause/Stop/Seek)
- Volume and mute support
- Duration and position tracking
- Events: MediaOpened, MediaEnded, MediaFailed

**Core Methods:**
```cpp
CreateSession()              // Creates IMFMediaSession
CreateMediaSource()          // Opens media file via IMFSourceResolver
CreateTopologyFromSource()   // Builds playback topology
AddBranchToPartialTopology() // Adds audio/video branches
CreateSourceStreamNode()     // Creates source nodes
CreateOutputNode()           // Creates EVR/SAR renderer nodes
HandleEvent()                // Processes Media Foundation events
```

### 2. WPF Wrapper Control (`NativeMediaPlayer.cs`)

**Location:** `MultiStreamVideoPlayer/Controls/NativeMediaPlayer.cs`

**Features:**
- Inherits from `HwndHost` for native window hosting
- Wraps C++/CLI VideoPlayer
- WPF dependency properties (Source, Volume, IsMuted, Tag)
- MediaElement-compatible API
- Thread-safe event handling

**Usage:**
```xaml
<controls:NativeMediaPlayer Source="{Binding FilePath}"
                            Tag="{Binding}"
                            Loaded="MediaElement_Loaded"
                            Unloaded="MediaElement_Unloaded" />
```

### 3. DX11 Video Renderer (`DX11VideoRenderer` project)

**Status:** Built and ready, currently disabled

**Files:**
- `Activate.cpp/h` - IMFActivate factory
- `MediaSink.cpp/h` - IMFMediaSink implementation
- `StreamSink.cpp/h` - IMFStreamSink implementation
- `Presenter.cpp/h` - Video frame presentation
- `Scheduler.cpp/h` - Frame timing
- `Display.cpp/h` - D3D11/DXGI management
- `Common.h` - Utilities and base classes

---

## Building the Solution

### Requirements
1. **Visual Studio 2022** (Community, Professional, or Enterprise)
2. **C++ Desktop Development workload**
3. **.NET desktop development workload**
4. **C++/CLI support component**
5. **Windows SDK 10.0** or later

### Build Commands

#### Visual Studio
1. Open solution in Visual Studio 2022
2. Select Debug/Release and x64 platform
3. Build → Build Solution (F6)

#### MSBuild Command Line
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
    MultiStreamVideoPlayer.sln `
    /p:Configuration=Debug `
    /p:Platform="Any CPU" `
    -verbosity:minimal
```

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────┐
│ WPF Application (MultiStreamVideoPlayer)        │
│                                                 │
│  ┌──────────────────────────────────────────┐  │
│  │ NativeMediaPlayer (HwndHost)             │  │
│  │  - WPF Dependency Properties             │  │
│  │  - Event Handling                        │  │
│  │  - HWND Management                       │  │
│  └────────────┬─────────────────────────────┘  │
└───────────────┼─────────────────────────────────┘
                │ .NET Interop
┌───────────────▼─────────────────────────────────┐
│ MediaFoundation.Player (C++/CLI)                │
│                                                 │
│  ┌──────────────────────────────────────────┐  │
│  │ VideoPlayer Class                        │  │
│  │  - IMFMediaSession                       │  │
│  │  - IMFMediaSource                        │  │
│  │  - IMFTopology                           │  │
│  │  - IMFVideoDisplayControl                │  │
│  └────────────┬─────────────────────────────┘  │
└───────────────┼─────────────────────────────────┘
                │ COM Interfaces
┌───────────────▼─────────────────────────────────┐
│ Windows Media Foundation (Native)               │
│                                                 │
│  - Source Resolver                              │
│  - Topology Builder                             │
│  - EVR (Enhanced Video Renderer)                │
│  - SAR (Streaming Audio Renderer)               │
│  - Presentation Clock                           │
│  - Event Queue                                  │
└─────────────────────────────────────────────────┘
```

---

## Development History & Bug Fixes

### December 2025 - Initial Implementation

#### Bug #1: NativeMediaPlayer Not Tracked in MainViewModel
**Symptom:** Clicking Play did nothing  
**Cause:** `_nativePlayers` list was empty because `RegisterMediaElement` didn't handle `NativeMediaPlayer` type  
**Fix:** Added pattern matching for `Controls.NativeMediaPlayer` in `RegisterMediaElement`

#### Bug #2: IMFVideoDisplayControl Activation Failed
**Symptom:** Video window was black  
**Cause:** Attempted to get `IMFVideoDisplayControl` before topology was ready  
**Fix:** Get control in `MESessionTopologyStatus` handler when `status == MF_TOPOSTATUS_READY`

#### Bug #3: interior_ptr Compilation Error
**Symptom:** Build failed with C++/CLI error  
**Cause:** `IID_PPV_ARGS(&m_pVideoDisplay)` doesn't work with managed class members  
**Fix:** Use local pointer variable, then assign to member

#### Bug #4: Missing strmiids.lib
**Symptom:** Linker error for DirectShow GUIDs  
**Cause:** Missing library reference  
**Fix:** Added `strmiids.lib` to additional dependencies

#### Bug #5: Media Foundation Events Stopped
**Symptom:** Only received first event, then silence  
**Cause:** `BeginGetEvent` not called after handling event  
**Fix:** Call `m_pSession->BeginGetEvent(this, nullptr)` at end of `Invoke`

#### Bug #6: Threading Exception on MediaOpened
**Symptom:** Application crash when video opened  
**Cause:** Fired managed event from Media Foundation callback thread  
**Fix:** Use `Dispatcher.BeginInvoke` in C# to marshal to UI thread

#### Bug #7: OpenUrl Called Before Window Ready
**Symptom:** Video failed to render, HWND was null  
**Cause:** `OnSourceChanged` called `OpenUrl` before `BuildWindowCore` created HWND  
**Fix:** Check `_hwndHost != IntPtr.Zero` in `OnSourceChanged`; defer to `BuildWindowCore`

#### Bug #8: Window Size Was NaN
**Symptom:** Host window had 0x0 size  
**Cause:** `Width` and `Height` return NaN before WPF layout pass  
**Fix:** Use `ActualWidth`/`ActualHeight` with fallback to 640x480

#### Bug #9: Invalid Window Handle
**Symptom:** `MFCreateVideoRendererActivate` failed silently  
**Cause:** `m_hwndVideo` was null or invalid  
**Fix:** Added `IsWindow(m_hwndVideo)` validation before creating renderer

---

## DX11VideoRenderer Implementation

### Status: Complete, Disabled

The DX11 renderer is built as a static library and linked into MediaFoundation.Player, but currently disabled.

### Enable DX11 Renderer
In `MFVideoPlayer.cpp`:
```cpp
#define USE_DX11_RENDERER true  // Change from false
```

### DX11-Specific Bugs Fixed

#### GUID Format in Solution File
**Symptom:** Solution wouldn't load  
**Fix:** Corrected GUID format (removed extra braces)

#### Runtime Library Mismatch
**Symptom:** Linker errors about conflicting runtime libraries  
**Cause:** DX11VideoRenderer used `/MTd`, others used `/MDd`  
**Fix:** Changed to `/MDd` for consistency

---

## Why C++/CLI?

1. **Direct COM Access**: Media Foundation uses COM interfaces that are difficult to P/Invoke from C#
2. **Memory Management**: C++ handles COM reference counting naturally
3. **Performance**: No marshaling overhead for complex structures
4. **Complete API Access**: Full access to all Media Foundation features
5. **Native HWND Rendering**: EVR requires native window handle

---

## Comparison: EVR vs DX11VideoRenderer

| Feature | EVR (Current) | DX11VideoRenderer |
|---------|---------------|-------------------|
| Setup Complexity | Low | High |
| Hardware Acceleration | ✅ | ✅ |
| Custom Shaders | ❌ | ✅ |
| Direct Texture Access | Limited | Full |
| Frame Manipulation | ❌ | ✅ |
| Stability | Proven | Needs testing |
| Maintenance | Microsoft | Custom |

**Recommendation:** Use EVR for standard playback. Use DX11 only if you need custom rendering effects.

---

## Future Enhancements

- [ ] Test and validate DX11VideoRenderer
- [ ] Add custom pixel shaders for video effects
- [ ] Implement frame capture/snapshot
- [ ] Add HDR support
- [ ] Implement smooth slow-motion
- [ ] Add stereoscopic 3D support
- [ ] Performance optimization for 4K

---

## References

- [Media Foundation Programming Guide](https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-programming-guide)
- [EVR Media Sink](https://docs.microsoft.com/en-us/windows/win32/medfound/enhanced-video-renderer)
- [Microsoft DX11VideoRenderer Sample](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/DX11VideoRenderer)
- [C++/CLI Programming](https://docs.microsoft.com/en-us/cpp/dotnet/dotnet-programming-with-cpp-cli-visual-cpp)
