// Copyright 2025 John Varghese
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

using System;
using CommunityToolkit.Mvvm.ComponentModel;

namespace MultiStreamVideoPlayer.Models;

public partial class VideoStream : ObservableObject
{
    public int StreamId { get; set; }
    public string CameraName { get; set; } = string.Empty;
    public string FilePath { get; set; } = string.Empty;
    public List<VideoSegment> Segments { get; set; } = new();

    [ObservableProperty]
    private bool _isInGap;

    public DateTime? StartTime => Segments.Count > 0 ? Segments.Min(s => s.StartTime) : null;
    public DateTime? EndTime => Segments.Count > 0 ? Segments.Max(s => s.EndTime) : null;
    public TimeSpan TotalDuration => Segments.Count > 0 
        ? TimeSpan.FromTicks(Segments.Sum(s => s.Duration.Ticks)) 
        : TimeSpan.Zero;
}
