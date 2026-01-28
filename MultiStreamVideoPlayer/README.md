# Multi-Stream Video Player

A professional multi-stream video player WPF application built with .NET 8 that displays and synchronizes up to 16 video streams with a custom timeline, gap detection, and synchronized seeking.

## Key Features

- Play up to 16 synchronized video streams (1x1 to 4x4 grid)
- Custom timeline with segments, gaps visualization, and draggable playhead
- Click-to-seek and timeline-aware seeking that accounts for segments
- Gap detection and auto-skip when no streams have video
- Single audio stream (first video only) with volume and mute controls
- Stream removal and auto layout

## Technology

- .NET 8 WPF (.NET Windows Desktop)
- MVVM pattern with CommunityToolkit.Mvvm
- Video playback via Windows Media Foundation integration

## Build and Run

Prerequisites:
- .NET 8 SDK
- Windows 10/11

Build:
```
dotnet build
```

Run:
```
dotnet run
```

## Usage (quick)

- Add streams: Use the "Add Streams" button and select 1–16 video files.
- Play/Pause: Use the central play/pause control.
- Seek: Click or drag the playhead on the timeline.
- Volume: Only affects the primary (first) stream.
- Remove stream: Click the remove button on a stream card.

## Notes

- Only the first video provides audio by design.
- Ensure required codecs are installed for playback.

## License

Provided as-is for educational and demonstration purposes.
