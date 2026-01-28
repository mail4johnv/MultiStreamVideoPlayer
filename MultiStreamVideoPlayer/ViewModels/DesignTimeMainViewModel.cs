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

using System.Collections.ObjectModel;
using MultiStreamVideoPlayer.Models;

namespace MultiStreamVideoPlayer.ViewModels;

public class DesignTimeMainViewModel : MainViewModel
{
    public DesignTimeMainViewModel()
    {
        // Create sample video streams for design-time
        VideoStreams = new ObservableCollection<VideoStream>
        {
            new VideoStream
            {
                StreamId = 1,
                CameraName = "Camera 1",
                FilePath = "C:\\Videos\\camera1.mp4",
                Segments = new List<VideoSegment>
                {
                    new VideoSegment
                    {
                        StartTime = DateTime.Now.Date.AddHours(8),
                        EndTime = DateTime.Now.Date.AddHours(8).AddMinutes(30)
                    }
                }
            },
            new VideoStream
            {
                StreamId = 2,
                CameraName = "Camera 2",
                FilePath = "C:\\Videos\\camera2.mp4",
                Segments = new List<VideoSegment>
                {
                    new VideoSegment
                    {
                        StartTime = DateTime.Now.Date.AddHours(8).AddMinutes(5),
                        EndTime = DateTime.Now.Date.AddHours(8).AddMinutes(35)
                    }
                }
            },
            new VideoStream
            {
                StreamId = 3,
                CameraName = "Camera 3",
                FilePath = "C:\\Videos\\camera3.mp4",
                Segments = new List<VideoSegment>
                {
                    new VideoSegment
                    {
                        StartTime = DateTime.Now.Date.AddHours(8).AddMinutes(10),
                        EndTime = DateTime.Now.Date.AddHours(8).AddMinutes(40)
                    }
                }
            },
            new VideoStream
            {
                StreamId = 4,
                CameraName = "Camera 4",
                FilePath = "C:\\Videos\\camera4.mp4",
                Segments = new List<VideoSegment>
                {
                    new VideoSegment
                    {
                        StartTime = DateTime.Now.Date.AddHours(8).AddMinutes(15),
                        EndTime = DateTime.Now.Date.AddHours(8).AddMinutes(45)
                    }
                }
            }
        };

        GridColumns = 2;
        GridRows = 2;
        IsPlaying = false;
        Volume = 0.5;
        IsMuted = false;
        TimelineStartTime = DateTime.Now.Date.AddHours(8);
        TimelineEndTime = DateTime.Now.Date.AddHours(9);
        CurrentTime = TimeSpan.FromMinutes(15);
        TotalDuration = TimeSpan.FromHours(1);
        PlayheadPosition = 25.0;
    }
}
