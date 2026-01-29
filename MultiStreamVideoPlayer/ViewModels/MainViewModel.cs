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
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using System.IO;
using MultiStreamVideoPlayer.Models;
using MediaFoundation.Player;

namespace MultiStreamVideoPlayer.ViewModels;

public partial class MainViewModel : ObservableObject
{
    private readonly DispatcherTimer _playbackTimer;
    private readonly DispatcherTimer _syncTimer;
    private readonly List<MediaElement> _mediaElements = new();
    private readonly List<Controls.MediaFoundationPlayer> _mfPlayers = new();
    private readonly List<Controls.NativeMediaPlayer> _nativePlayers = new();

    [ObservableProperty]
    private ObservableCollection<VideoStream> _videoStreams = new();

    [ObservableProperty]
    private bool _isPlaying;

    [ObservableProperty]
    private double _playheadPosition; // 0-100 percentage

    [ObservableProperty]
    private double _volume = 0.5;

    [ObservableProperty]
    private bool _isMuted;

    [ObservableProperty]
    private DateTime? _timelineStartTime;

    [ObservableProperty]
    private DateTime? _timelineEndTime;

    [ObservableProperty]
    private TimeSpan _currentTime;

    [ObservableProperty]
    private TimeSpan _totalDuration;

    [ObservableProperty]
    private bool _isSearchPanelOpen;

    [ObservableProperty]
    private int _gridColumns = 1;

    [ObservableProperty]
    private int _gridRows = 1;

    public MainViewModel()
    {
        _playbackTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(16) // ~60 FPS
        };
        _playbackTimer.Tick += PlaybackTimer_Tick;

        _syncTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(100)
        };
        _syncTimer.Tick += SyncTimer_Tick;
    }

    [RelayCommand]
    private void SelectGPU()
    {
        App.ShowGPUSelectorDialog();
        // After GPU selection, existing players will continue using their current GPU
        // New players created after this will use the newly selected GPU
    }

        private void AddVideoStream(string filePath)
        {
            var fileName = Path.GetFileNameWithoutExtension(filePath);
            var streamId = VideoStreams.Count + 1;
            var baseTime = DateTime.Now.Date.AddHours(8);
            
            // Create a video stream with sample segments based on file info
            // In a real implementation, you would parse the video file to get actual timestamps
            var stream = new VideoStream
            {
                StreamId = streamId,
                CameraName = fileName,
                FilePath = filePath,
                Segments = new List<VideoSegment>()
            };
            
            // Add sample segments (in real app, parse video metadata)
            var offset = (streamId - 1) * 5;
            stream.Segments.Add(new VideoSegment
            {
                StartTime = baseTime.AddMinutes(offset),
                EndTime = baseTime.AddMinutes(offset + 45)
            });
            
            stream.Segments.Add(new VideoSegment
            {
                StartTime = baseTime.AddMinutes(offset + 60),
                EndTime = baseTime.AddMinutes(offset + 105)
            });
            
            stream.Segments.Add(new VideoSegment
            {
                StartTime = baseTime.AddMinutes(offset + 120),
                EndTime = baseTime.AddMinutes(offset + 165)
            });
            
            VideoStreams.Add(stream);
        }
        
    [RelayCommand]
    private void AddStreams()
    {
        var dialog = new OpenFileDialog
        {
            Multiselect = true,
            Filter = "Video Files|*.mp4;*.avi;*.mkv;*.mov;*.wmv|All Files|*.*",
            Title = "Select Video Files (1-16 streams)"
        };

        if (dialog.ShowDialog() == true)
        {
            var filesToAdd = dialog.FileNames.Take(16 - VideoStreams.Count).ToArray();

            foreach (var file in filesToAdd)
            {
               AddVideoStream(file);
            }

            UpdateGridLayout();
            CalculateTimelineBounds();
        }
    }

    [RelayCommand]
    private void RemoveStream(VideoStream stream)
    {
        // Find and stop the player associated with this stream
        var playerToRemove = _nativePlayers.FirstOrDefault(p => p.Tag == stream);
        
        if (playerToRemove != null)
        {
            //Console.WriteLine($"[MainViewModel] RemoveStream: Found player for stream {stream.StreamId}, stopping playback");
            
            // Stop the player to halt audio/video playback
            try
            {
                playerToRemove.Stop();
                //Console.WriteLine($"[MainViewModel] RemoveStream: Stopped player");
            }
            catch (Exception ex)
            {
                //Console.WriteLine($"[MainViewModel] RemoveStream: Error stopping player - {ex.Message}");
            }
            
            // Dispose the player to release resources
            try
            {
                playerToRemove.Dispose();
                //Console.WriteLine($"[MainViewModel] RemoveStream: Disposed player");
            }
            catch (Exception ex)
            {
                //Console.WriteLine($"[MainViewModel] RemoveStream: Error disposing player - {ex.Message}");
            }
            
            // Remove from our tracking list
            _nativePlayers.Remove(playerToRemove);
            //Console.WriteLine($"[MainViewModel] RemoveStream: Unregistered player");
        }
        else
        {
            //Console.WriteLine($"[MainViewModel] RemoveStream: No player found for stream {stream.StreamId}");
        }
        
        // Remove from collection, which will trigger UI unload
        VideoStreams.Remove(stream);
        UpdateGridLayout();
        CalculateTimelineBounds();

        // Update camera names
        for (int i = 0; i < VideoStreams.Count; i++)
        {
            VideoStreams[i].CameraName = $"Camera {i + 1}";
            VideoStreams[i].StreamId = i + 1;
        }
    }

    [RelayCommand]
    private void TogglePlayPause()
    {
        IsPlaying = !IsPlaying;

        if (IsPlaying)
        {
            _playbackTimer.Start();
            _syncTimer.Start();
            PlayAllVideos();
        }
        else
        {
            _playbackTimer.Stop();
            _syncTimer.Stop();
            PauseAllVideos();
        }
    }

    [RelayCommand]
    private void Previous()
    {
        PlayheadPosition = 0;
        if (!IsPlaying)
        {
            SeekAllVideos();
        }
    }

    [RelayCommand]
    private void Next()
    {
        PlayheadPosition = 100;
        if (!IsPlaying)
        {
            SeekAllVideos();
        }
    }

    [RelayCommand]
    private void ToggleMute()
    {
        IsMuted = !IsMuted;
        UpdateAllVolumes();
    }

    [RelayCommand]
    private void ToggleSearchPanel()
    {
        IsSearchPanelOpen = !IsSearchPanelOpen;
    }

    public void RegisterMediaElement(object mediaControl)
    {
        //Console.WriteLine($"[MainViewModel] RegisterMediaElement called with type: {mediaControl?.GetType().Name}");
        
        if (mediaControl is Controls.MediaFoundationPlayer mfPlayer)
        {
            if (!_mfPlayers.Contains(mfPlayer))
            {
                _mfPlayers.Add(mfPlayer);
                //Console.WriteLine($"[MainViewModel] Added MediaFoundationPlayer - total count: {_mfPlayers.Count}");
                mfPlayer.MediaOpened += (s, e) => MediaElement_MediaOpened(s ?? this, new RoutedEventArgs());
                mfPlayer.MediaEnded += (s, e) => { };
            }
        }
        else if (mediaControl is Controls.NativeMediaPlayer nativePlayer)
        {
            if (!_nativePlayers.Contains(nativePlayer))
            {
                _nativePlayers.Add(nativePlayer);
                //Console.WriteLine($"[MainViewModel] Added NativeMediaPlayer - Source={nativePlayer.Source}, total count: {_nativePlayers.Count}");
                nativePlayer.MediaOpened += (s, e) =>
                {
                    //Console.WriteLine($"[MainViewModel] NativeMediaPlayer MediaOpened - Source={nativePlayer.Source}");
                };
                nativePlayer.MediaEnded += (s, e) =>
                {
                    //Console.WriteLine($"[MainViewModel] NativeMediaPlayer MediaEnded - Source={nativePlayer.Source}");
                };
            }
        }
        else if (mediaControl is MediaElement mediaElement)
        {
            if (!_mediaElements.Contains(mediaElement))
            {
                _mediaElements.Add(mediaElement);
                //Console.WriteLine($"[MainViewModel] Added MediaElement - total count: {_mediaElements.Count}");
                mediaElement.MediaOpened += MediaElement_MediaOpened;
                mediaElement.MediaEnded += MediaElement_MediaEnded;
            }
        }
        else
        {
            //Console.WriteLine($"[MainViewModel] Unknown media control type: {mediaControl?.GetType().FullName}");
        }
    }

    public void UnregisterMediaElement(object mediaControl)
    {
        if (mediaControl is Controls.MediaFoundationPlayer mfPlayer)
        {
            _mfPlayers.Remove(mfPlayer);
        }
        else if (mediaControl is Controls.NativeMediaPlayer nativePlayer)
        {
            _nativePlayers.Remove(nativePlayer);
        }
        else if (mediaControl is MediaElement mediaElement)
        {
            _mediaElements.Remove(mediaElement);
            mediaElement.MediaOpened -= MediaElement_MediaOpened;
            mediaElement.MediaEnded -= MediaElement_MediaEnded;
        }
    }

    private void MediaElement_MediaOpened(object sender, RoutedEventArgs e)
    {
        if (sender is MediaElement mediaElement && mediaElement.Tag is VideoStream stream)
        {
            // Initialize segments based on actual video duration
            if (stream.Segments.Count == 0 && mediaElement.NaturalDuration.HasTimeSpan)
            {
                var duration = mediaElement.NaturalDuration.TimeSpan;
                var baseTime = DateTime.Now.Date.AddHours(stream.StreamId - 1);
                
                stream.Segments.Add(new VideoSegment
                {
                    StartTime = baseTime,
                    EndTime = baseTime.Add(duration)
                });
                
                // Recalculate timeline bounds
                CalculateTimelineBounds();
            }
        }
        
        UpdateAllVolumes();
    }

    private void MediaElement_MediaEnded(object sender, RoutedEventArgs e)
    {
        // Handle individual video end
    }

    private void PlaybackTimer_Tick(object? sender, EventArgs e)
    {
        if (!IsPlaying || TimelineStartTime == null || TimelineEndTime == null) return;

        var totalSeconds = (TimelineEndTime.Value - TimelineStartTime.Value).TotalSeconds;
        var increment = (100.0 / totalSeconds) * 0.016; // 16ms increment

        PlayheadPosition += increment;

        if (PlayheadPosition >= 100)
        {
            PlayheadPosition = 100;
            IsPlaying = false;
            _playbackTimer.Stop();
            _syncTimer.Stop();
            PauseAllVideos();
        }
        else
        {
            // Check for gaps and skip if necessary
            var currentTime = GetCurrentTimelineTime();
            if (IsInGap(currentTime))
            {
                var nextPosition = GetNextSegmentPosition(currentTime);
                if (nextPosition.HasValue)
                {
                    PlayheadPosition = nextPosition.Value;
                }
                else
                {
                    PlayheadPosition = 100;
                    IsPlaying = false;
                    _playbackTimer.Stop();
                    _syncTimer.Stop();
                    PauseAllVideos();
                }
            }
        }

        UpdateCurrentTime();
        UpdateGapStatus();
    }

    private void SyncTimer_Tick(object? sender, EventArgs e)
    {
        if (!IsPlaying) return;
        //SyncVideoPositions();
    }

    private void SyncVideoPositions()
    {
        var currentTime = GetCurrentTimelineTime();

        foreach (var mediaElement in _mediaElements)
        {
            if (mediaElement.Tag is VideoStream stream)
            {
                var shouldPlay = ShouldVideoPlay(stream, currentTime);

                if (shouldPlay && mediaElement.LoadedBehavior == MediaState.Manual)
                {
                    if (mediaElement.CanPause && 
                        (mediaElement.Position == TimeSpan.Zero || 
                         Math.Abs((mediaElement.Position - GetVideoPosition(stream, currentTime)).TotalSeconds) > 0.5))
                    {
                        mediaElement.Position = GetVideoPosition(stream, currentTime);
                    }

                    if (mediaElement.HasAudio || mediaElement.HasVideo)
                    {
                        try
                        {
                            mediaElement.Play();
                        }
                        catch { /* Ignore playback errors */ }
                    }
                }
                else
                {
                    try
                    {
                        if (mediaElement.CanPause)
                        {
                            mediaElement.Pause();
                        }
                    }
                    catch { /* Ignore pause errors */ }
                }
            }
        }
    }

    private bool ShouldVideoPlay(VideoStream stream, DateTime currentTime)
    {
        return stream.Segments.Any(s => currentTime >= s.StartTime && currentTime <= s.EndTime);
    }

    private TimeSpan GetVideoPosition(VideoStream stream, DateTime currentTime)
    {
        TimeSpan totalOffset = TimeSpan.Zero;

        foreach (var segment in stream.Segments.OrderBy(s => s.StartTime))
        {
            if (currentTime >= segment.StartTime && currentTime <= segment.EndTime)
            {
                var offsetInSegment = currentTime - segment.StartTime;
                return totalOffset + offsetInSegment;
            }

            if (currentTime > segment.EndTime)
            {
                totalOffset += segment.Duration;
            }
        }

        return totalOffset;
    }

    private void PlayAllVideos()
    {
        //Console.WriteLine($"[MainViewModel] PlayAllVideos() called - _mediaElements.Count={_mediaElements.Count}, _mfPlayers.Count={_mfPlayers.Count}, _nativePlayers.Count={_nativePlayers.Count}");
        
        foreach (var mediaElement in _mediaElements)
        {
            try
            {
                //Console.WriteLine($"[MainViewModel] Playing MediaElement: LoadedBehavior={mediaElement.LoadedBehavior}, HasAudio={mediaElement.HasAudio}, HasVideo={mediaElement.HasVideo}");
                if (mediaElement.LoadedBehavior == MediaState.Manual && 
                    (mediaElement.HasAudio || mediaElement.HasVideo))
                {
                    mediaElement.Play();
                }
            }
            catch (Exception ex) { 
                //Console.WriteLine($"[MainViewModel] MediaElement.Play() error: {ex.Message}"); 
            }
        }
        
        foreach (var mfPlayer in _mfPlayers)
        {
            try
            {
                //Console.WriteLine($"[MainViewModel] Playing MFPlayer: Source={mfPlayer.Source}, Duration={mfPlayer.Duration}");
                mfPlayer.Play();
            }
            catch (Exception ex) {
                //Console.WriteLine($"[MainViewModel] MFPlayer.Play() error: {ex.Message}");
            }
        }
        
        foreach (var nativePlayer in _nativePlayers)
        {
            try
            {
                //Console.WriteLine($"[MainViewModel] Playing NativePlayer: Source={nativePlayer.Source}, Duration={nativePlayer.Duration}");
                nativePlayer.Play();
            }
            catch (Exception ex) {
                //Console.WriteLine($"[MainViewModel] NativePlayer.Play() error: {ex.Message}");
            }
        }
    }

    private void PauseAllVideos()
    {
        foreach (var mediaElement in _mediaElements)
        {
            try
            {
                if (mediaElement.CanPause)
                {
                    mediaElement.Pause();
                }
            }
            catch { /* Ignore pause errors */ }
        }
        
        foreach (var mfPlayer in _mfPlayers)
        {
            try
            {
                mfPlayer.Pause();
            }
            catch { /* Ignore pause errors */ }
        }
        
        foreach (var nativePlayer in _nativePlayers)
        {
            try
            {
                nativePlayer.Pause();
            }
            catch { /* Ignore pause errors */ }
        }
    }

    /// <summary>
    /// Repaint all video players. Call this when window is restored from minimized state.
    /// </summary>
    public void RepaintAllVideos()
    {
        foreach (var nativePlayer in _nativePlayers)
        {
            try
            {
                nativePlayer.Repaint();
            }
            catch { /* Ignore repaint errors */ }
        }
    }

    public void SeekToPercentage(double percentage)
    {
        PlayheadPosition = Math.Clamp(percentage, 0, 100);
        
        if (!IsPlaying)
        {
            SeekAllVideos();
        }
        
        UpdateCurrentTime();
        UpdateGapStatus();
    }

    private void SeekAllVideos()
    {
        var currentTime = GetCurrentTimelineTime();

        foreach (var mediaElement in _mediaElements)
        {
            if (mediaElement.Tag is VideoStream stream)
            {
                var position = GetVideoPosition(stream, currentTime);
                
                try
                {
                    if (mediaElement.NaturalDuration.HasTimeSpan)
                    {
                        mediaElement.Position = position;
                    }
                }
                catch { /* Ignore seek errors */ }
            }
        }
    }

    private void UpdateAllVolumes()
    {
        ManagedLogger.LogInfo($"[MainViewModel.UpdateAllVolumes] Called. Volume={Volume}, IsMuted={IsMuted}");
        ManagedLogger.LogInfo($"[MainViewModel.UpdateAllVolumes] MediaElements={_mediaElements.Count}, NativePlayers={_nativePlayers.Count}, MFPlayers={_mfPlayers.Count}");
        
        // Update media elements
        for (int i = 0; i < _mediaElements.Count; i++)
        {
            var mediaElement = _mediaElements[i];
            if (mediaElement == null) continue;
            
            if (i == 0)
            {
                // First video has audio
                double finalVolume = IsMuted ? 0 : Volume;
                mediaElement.Volume = finalVolume;
                mediaElement.IsMuted = IsMuted;
                ManagedLogger.LogInfo($"[MainViewModel.UpdateAllVolumes] MediaElement[0] set to Volume={finalVolume}, IsMuted={IsMuted}");
            }
            else
            {
                // All other videos are muted
                mediaElement.Volume = 0;
                mediaElement.IsMuted = true;
            }
        }
        
        // Update native players
        for (int i = 0; i < _nativePlayers.Count; i++)
        {
            var nativePlayer = _nativePlayers[i];
            if (nativePlayer == null)
            {
                ManagedLogger.LogWarning($"[MainViewModel.UpdateAllVolumes] NativePlayer[{i}] is null");
                continue;
            }
            
            if (i == 0 && _mediaElements.Count == 0)
            {
                // First native player has audio if no media elements
                double finalVolume = IsMuted ? 0 : Volume;
                nativePlayer.Volume = finalVolume;
                nativePlayer.IsMuted = IsMuted;
                ManagedLogger.LogInfo($"[MainViewModel.UpdateAllVolumes] NativePlayer[0] set to Volume={finalVolume}, IsMuted={IsMuted}");
            }
            else
            {
                // All other native players are muted
                nativePlayer.Volume = 0;
                nativePlayer.IsMuted = true;
            }
        }
        
        // Update MF players
        for (int i = 0; i < _mfPlayers.Count; i++)
        {
            var mfPlayer = _mfPlayers[i];
            if (mfPlayer == null)
            {
                ManagedLogger.LogWarning($"[MainViewModel.UpdateAllVolumes] MFPlayer[{i}] is null");
                continue;
            }
            
            if (i == 0 && _mediaElements.Count == 0 && _nativePlayers.Count == 0)
            {
                // First MF player has audio if no media elements or native players
                double finalVolume = IsMuted ? 0 : Volume;
                mfPlayer.Volume = finalVolume;
                mfPlayer.IsMuted = IsMuted;
                ManagedLogger.LogInfo($"[MainViewModel.UpdateAllVolumes] MFPlayer[0] set to Volume={finalVolume}, IsMuted={IsMuted}");
            }
            else
            {
                // All other MF players are muted
                mfPlayer.Volume = 0;
                mfPlayer.IsMuted = true;
            }
        }
        
        ManagedLogger.LogInfo($"[MainViewModel.UpdateAllVolumes] Finished");
    }

    private void UpdateGridLayout()
    {
        var count = VideoStreams.Count;

        if (count == 0)
        {
            GridColumns = 1;
            GridRows = 1;
        }
        else if (count <= 1)
        {
            GridColumns = 1;
            GridRows = 1;
        }
        else if (count <= 2)
        {
            GridColumns = 2;
            GridRows = 1;
        }
        else if (count <= 4)
        {
            GridColumns = 2;
            GridRows = 2;
        }
        else if (count <= 6)
        {
            GridColumns = 3;
            GridRows = 2;
        }
        else if (count <= 9)
        {
            GridColumns = 3;
            GridRows = 3;
        }
        else
        {
            GridColumns = 4;
            GridRows = 4;
        }
    }

    private void CalculateTimelineBounds()
    {
        if (VideoStreams.Count == 0)
        {
            TimelineStartTime = null;
            TimelineEndTime = null;
            TotalDuration = TimeSpan.Zero;
            return;
        }

        var streamsWithSegments = VideoStreams.Where(s => s.Segments.Count > 0).ToList();
        if (streamsWithSegments.Count == 0)
        {
            return; // Wait until segments are loaded
        }

        var allStartTimes = streamsWithSegments.Where(s => s.StartTime.HasValue).Select(s => s.StartTime!.Value);
        var allEndTimes = streamsWithSegments.Where(s => s.EndTime.HasValue).Select(s => s.EndTime!.Value);

        if (allStartTimes.Any() && allEndTimes.Any())
        {
            TimelineStartTime = allStartTimes.Min();
            TimelineEndTime = allEndTimes.Max();
            TotalDuration = TimelineEndTime.Value - TimelineStartTime.Value;
        }
    }

    private DateTime GetCurrentTimelineTime()
    {
        if (TimelineStartTime == null || TimelineEndTime == null)
            return DateTime.Now;

        var totalSeconds = (TimelineEndTime.Value - TimelineStartTime.Value).TotalSeconds;
        var currentSeconds = (PlayheadPosition / 100.0) * totalSeconds;
        return TimelineStartTime.Value.AddSeconds(currentSeconds);
    }

    private void UpdateCurrentTime()
    {
        if (TimelineStartTime == null) return;

        var currentTimelineTime = GetCurrentTimelineTime();
        CurrentTime = currentTimelineTime - TimelineStartTime.Value;
    }

    private bool IsInGap(DateTime currentTime)
    {
        if (VideoStreams.Count == 0) return false;

        // Check if ALL streams are in gaps at this time
        bool allInGaps = true;
        foreach (var stream in VideoStreams)
        {
            bool streamHasVideo = stream.Segments.Any(s => 
                currentTime >= s.StartTime && currentTime <= s.EndTime);
            
            if (streamHasVideo)
            {
                allInGaps = false;
                break;
            }
        }

        return allInGaps;
    }

    private void UpdateGapStatus()
    {
        var currentTime = GetCurrentTimelineTime();

        foreach (var stream in VideoStreams)
        {
            bool isInGap = !stream.Segments.Any(s => 
                currentTime >= s.StartTime && currentTime <= s.EndTime);
            stream.IsInGap = isInGap;
        }
    }

    private double? GetNextSegmentPosition(DateTime currentTime)
    {
        if (TimelineStartTime == null || TimelineEndTime == null) return null;

        var nextSegmentStarts = new List<DateTime>();

        foreach (var stream in VideoStreams)
        {
            var nextSegment = stream.Segments
                .Where(s => s.StartTime > currentTime)
                .OrderBy(s => s.StartTime)
                .FirstOrDefault();

            if (nextSegment != null)
            {
                nextSegmentStarts.Add(nextSegment.StartTime);
            }
        }

        if (nextSegmentStarts.Count == 0) return null;

        var nextStart = nextSegmentStarts.Min();
        var totalSeconds = (TimelineEndTime.Value - TimelineStartTime.Value).TotalSeconds;
        var secondsToNext = (nextStart - TimelineStartTime.Value).TotalSeconds;
        return (secondsToNext / totalSeconds) * 100.0;
    }

    partial void OnVolumeChanged(double value)
    {
        ManagedLogger.LogInfo($"[MainViewModel.OnVolumeChanged] Volume changed to: {value}, IsMuted: {IsMuted}");
        UpdateAllVolumes();
    }

    partial void OnIsMutedChanged(bool value)
    {
        ManagedLogger.LogInfo($"[MainViewModel.OnIsMutedChanged] Mute changed to: {value}, Current Volume: {Volume}");
        UpdateAllVolumes();
    }

    /// <summary>
    /// Cleanup all resources when the application is closing.
    /// </summary>
    public void Cleanup()
    {
        //Console.WriteLine($"[MainViewModel] Cleanup: Starting cleanup");
        
        // Stop playback first
        if (IsPlaying)
        {
            PauseAllVideos();
        }
        
        _playbackTimer.Stop();
        _syncTimer.Stop();
        
        // Dispose all native players
        foreach (var player in _nativePlayers.ToList())
        {
            try
            {
                //Console.WriteLine($"[MainViewModel] Cleanup: Disposing NativeMediaPlayer");
                player.Dispose();
            }
            catch (Exception ex)
            {
                //Console.WriteLine($"[MainViewModel] Cleanup: Error disposing NativeMediaPlayer - {ex.Message}");
            }
        }
        _nativePlayers.Clear();
        
        // Clear media elements
        foreach (var element in _mediaElements.ToList())
        {
            try
            {
                element.Close();
            }
            catch (Exception ex)
            {
                //Console.WriteLine($"[MainViewModel] Cleanup: Error closing MediaElement - {ex.Message}");
            }
        }
        _mediaElements.Clear();
        
        // Clear MF players
        _mfPlayers.Clear();
        
        //Console.WriteLine($"[MainViewModel] Cleanup: Complete");
    }
}
