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
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiStreamVideoPlayer.Models;

namespace MultiStreamVideoPlayer.Controls;

public class ZoomConfig
{
    public ZoomConfig(double ratio, long lineDist, string format)
    {
        ZoomRatio = ratio;
        LineDistance = lineDist;
        DateTimeFormat = format;
    }

    public double ZoomRatio { get; set; }
    public long LineDistance { get; set; }
    public string DateTimeFormat { get; set; }

    public string GetTimeIndicator(DateTime start, long pos)
    {
        return (start + TimeSpan.FromTicks(pos * LineDistance)).ToString(DateTimeFormat);
    }
}

public class TimelineControl : Control
{
    private Canvas? _tracksCanvas;
    private Canvas? _labelsCanvas;
    private Canvas? _timeIndicatorCanvas;
    private Line? _playheadLine;
    private Ellipse? _playheadCircle;
    private ScrollViewer? _scrollViewer;
    private bool _isDragging;
    private bool _wasPlayingBeforeDrag;

    private static readonly ZoomConfig[] ZoomFactors = {
        new ZoomConfig(90.0/(3600.0 * 24 * 7), TimeSpan.TicksPerDay * 7, "dd MMM yyyy"),
        new ZoomConfig(90.0/(3600.0 * 24), TimeSpan.TicksPerDay, "dd MMM yyyy\nhh:mm tt"),
        new ZoomConfig(90.0/3600.0, TimeSpan.TicksPerHour, "dd MMM yyyy\nhh:mm tt"),
        new ZoomConfig(90.0/60.0, TimeSpan.TicksPerMinute, "hh:mm:ss tt"),
        new ZoomConfig(90.0, TimeSpan.TicksPerSecond, "hh:mm:ss tt"),
    };

    private int _currentZoomLevel = 2;

    public static readonly DependencyProperty VideoStreamsProperty =
        DependencyProperty.Register(nameof(VideoStreams), typeof(IEnumerable<VideoStream>), typeof(TimelineControl),
            new PropertyMetadata(null, OnVideoStreamsChanged));

    public static readonly DependencyProperty PlayheadPositionProperty =
        DependencyProperty.Register(nameof(PlayheadPosition), typeof(double), typeof(TimelineControl),
            new PropertyMetadata(0.0, OnPlayheadPositionChanged));

    public static readonly DependencyProperty TimelineStartTimeProperty =
        DependencyProperty.Register(nameof(TimelineStartTime), typeof(DateTime?), typeof(TimelineControl),
            new PropertyMetadata(null, OnTimelineChanged));

    public static readonly DependencyProperty TimelineEndTimeProperty =
        DependencyProperty.Register(nameof(TimelineEndTime), typeof(DateTime?), typeof(TimelineControl),
            new PropertyMetadata(null, OnTimelineChanged));

    public static readonly DependencyProperty IsPlayingProperty =
        DependencyProperty.Register(nameof(IsPlaying), typeof(bool), typeof(TimelineControl),
            new PropertyMetadata(false));

    public static readonly DependencyProperty ZoomProperty =
        DependencyProperty.Register(nameof(Zoom), typeof(int), typeof(TimelineControl),
            new PropertyMetadata(2, OnZoomChanged));

    public IEnumerable<VideoStream>? VideoStreams
    {
        get => (IEnumerable<VideoStream>?)GetValue(VideoStreamsProperty);
        set => SetValue(VideoStreamsProperty, value);
    }

    public double PlayheadPosition
    {
        get => (double)GetValue(PlayheadPositionProperty);
        set => SetValue(PlayheadPositionProperty, value);
    }

    public DateTime? TimelineStartTime
    {
        get => (DateTime?)GetValue(TimelineStartTimeProperty);
        set => SetValue(TimelineStartTimeProperty, value);
    }

    public DateTime? TimelineEndTime
    {
        get => (DateTime?)GetValue(TimelineEndTimeProperty);
        set => SetValue(TimelineEndTimeProperty, value);
    }

    public bool IsPlaying
    {
        get => (bool)GetValue(IsPlayingProperty);
        set => SetValue(IsPlayingProperty, value);
    }

    public int Zoom
    {
        get => (int)GetValue(ZoomProperty);
        set
        {
            var zoomValue = Math.Clamp(value, 0, ZoomFactors.Length - 1);
            SetValue(ZoomProperty, zoomValue);
        }
    }

    public event EventHandler<double>? SeekRequested;

    static TimelineControl()
    {
        DefaultStyleKeyProperty.OverrideMetadata(typeof(TimelineControl),
            new FrameworkPropertyMetadata(typeof(TimelineControl)));
    }

    public override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        _tracksCanvas = GetTemplateChild("PART_TracksCanvas") as Canvas;
        _labelsCanvas = GetTemplateChild("PART_LabelsCanvas") as Canvas;
        _timeIndicatorCanvas = GetTemplateChild("PART_TimeIndicatorCanvas") as Canvas;
        _scrollViewer = GetTemplateChild("PART_ScrollViewer") as ScrollViewer;
        _playheadLine = GetTemplateChild("PART_PlayheadLine") as Line;
        _playheadCircle = GetTemplateChild("PART_PlayheadCircle") as Ellipse;

        if (_scrollViewer != null)
        {
            _scrollViewer.ScrollChanged += ScrollViewer_ScrollChanged;
        }

        if (_tracksCanvas != null)
        {
            _tracksCanvas.MouseLeftButtonDown += TracksCanvas_MouseLeftButtonDown;
        }

        if (_playheadCircle != null)
        {
            _playheadCircle.MouseLeftButtonDown += PlayheadCircle_MouseLeftButtonDown;
            _playheadCircle.MouseLeftButtonUp += PlayheadCircle_MouseLeftButtonUp;
            _playheadCircle.MouseMove += PlayheadCircle_MouseMove;
        }

        RenderTracks();
        UpdatePlayheadPosition();
    }

    private static void OnVideoStreamsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is TimelineControl control)
        {
            control.RenderTracks();
        }
    }

    private static void OnPlayheadPositionChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is TimelineControl control)
        {
            control.UpdatePlayheadPosition();
        }
    }

    private static void OnTimelineChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is TimelineControl control)
        {
            control.RenderTracks();
        }
    }

    private static void OnZoomChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is TimelineControl control)
        {
            control._currentZoomLevel = (int)e.NewValue;
            control.RenderTracks();
            control.RenderTimeIndicators();
        }
    }

    private void RenderTracks()
    {
        if (_tracksCanvas == null || _labelsCanvas == null || VideoStreams == null) return;

        _tracksCanvas.Children.Clear();
        _labelsCanvas.Children.Clear();

        const double trackHeight = 8;
        const double trackSpacing = 10;

        var streams = VideoStreams.ToList();
        if (streams.Count == 0) return;
        
        double currentY = 6;

        for (int i = 0; i < streams.Count; i++)
        {
            var stream = streams[i];

            // Add label to left panel
            var label = new TextBlock
            {
                Text = stream.CameraName,
                FontSize = 10,
                Opacity = 0.8,
                Foreground = new SolidColorBrush(Color.FromRgb(0x21, 0x25, 0x29)),
                VerticalAlignment = VerticalAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis,
                Width = 110,
                Padding = new Thickness(5, 0, 5, 0)
            };
            Canvas.SetLeft(label, 0);
            Canvas.SetTop(label, currentY - 1);
            _labelsCanvas.Children.Add(label);

            // Draw segments in right panel (timeline)
            if (TimelineStartTime.HasValue && TimelineEndTime.HasValue && stream.Segments != null && stream.Segments.Count > 0)
            {
                var totalDuration = (TimelineEndTime.Value - TimelineStartTime.Value).TotalSeconds;

                if (totalDuration > 0)
                {
                    foreach (var segment in stream.Segments)
                    {
                        var startPercent = (segment.StartTime - TimelineStartTime.Value).TotalSeconds / totalDuration;
                        var endPercent = (segment.EndTime - TimelineStartTime.Value).TotalSeconds / totalDuration;

                        var startX = startPercent * _tracksCanvas.ActualWidth;
                        var width = (endPercent - startPercent) * _tracksCanvas.ActualWidth;

                        if (width > 0)
                        {
                            var rect = new Rectangle
                            {
                                Width = width,
                                Height = trackHeight,
                                RadiusX = 2,
                                RadiusY = 2,
                                Fill = new LinearGradientBrush(
                                    Color.FromRgb(0x2e, 0xcc, 0x71),
                                    Color.FromRgb(0x27, 0xae, 0x60),
                                    45)
                            };
                            Canvas.SetLeft(rect, startX);
                            Canvas.SetTop(rect, currentY);
                            _tracksCanvas.Children.Add(rect);
                        }
                    }
                }
            }

            currentY += trackHeight + trackSpacing;
        }

        _tracksCanvas.Height = currentY;
        _labelsCanvas.Height = currentY;
        RenderTimeIndicators();
    }

    private void UpdatePlayheadPosition()
    {
        if (_tracksCanvas == null || _playheadLine == null || _playheadCircle == null) return;

        var xPosition = (PlayheadPosition / 100.0) * _tracksCanvas.ActualWidth;

        _playheadLine.X1 = xPosition;
        _playheadLine.X2 = xPosition;
        _playheadLine.Y1 = 0;
        _playheadLine.Y2 = _tracksCanvas.Height;

        _playheadCircle.Margin = new Thickness(xPosition - 6, -6, 0, 0);
    }

    private void TracksCanvas_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (_tracksCanvas == null) return;

        var point = e.GetPosition(_tracksCanvas);
        var percentage = (point.X / _tracksCanvas.ActualWidth) * 100.0;
        percentage = Math.Clamp(percentage, 0, 100);

        SeekRequested?.Invoke(this, percentage);
    }

    private void PlayheadCircle_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (_playheadCircle == null) return;

        _isDragging = true;
        _wasPlayingBeforeDrag = IsPlaying;
        if (_wasPlayingBeforeDrag)
        {
            // Pause playback during drag
            SeekRequested?.Invoke(this, PlayheadPosition);
        }
        _playheadCircle.CaptureMouse();
        e.Handled = true;
    }

    private void PlayheadCircle_MouseMove(object sender, MouseEventArgs e)
    {
        if (!_isDragging || _tracksCanvas == null) return;

        var point = e.GetPosition(_tracksCanvas);
        var percentage = (point.X / _tracksCanvas.ActualWidth) * 100.0;
        percentage = Math.Clamp(percentage, 0, 100);

        SeekRequested?.Invoke(this, percentage);
    }

    private void PlayheadCircle_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (!_isDragging) return;

        _isDragging = false;
        _playheadCircle?.ReleaseMouseCapture();

        if (_wasPlayingBeforeDrag)
        {
            // Resume playback after drag
            SeekRequested?.Invoke(this, PlayheadPosition);
        }
    }

    private void RenderTimeIndicators()
    {
        if (_timeIndicatorCanvas == null || !TimelineStartTime.HasValue || !TimelineEndTime.HasValue)
            return;

        _timeIndicatorCanvas.Children.Clear();

        var zoomConfig = ZoomFactors[_currentZoomLevel];
        var totalDuration = TimelineEndTime.Value - TimelineStartTime.Value;
        var totalLines = (long)(totalDuration.Ticks / zoomConfig.LineDistance);
        
        var viewportWidth = _scrollViewer?.ViewportWidth ?? ActualWidth;
        var horizontalOffset = _scrollViewer?.HorizontalOffset ?? 0;

        for (long i = 0; i <= totalLines; i++)
        {
            var xPos = i * 90;
            
            // Only render visible time indicators
            if (xPos < horizontalOffset - 100 || xPos > horizontalOffset + viewportWidth + 100)
                continue;

            // Vertical line
            var line = new Line
            {
                Stroke = new SolidColorBrush(Color.FromRgb(0xde, 0xe2, 0xe6)),
                StrokeThickness = 0.5,
                X1 = xPos,
                X2 = xPos,
                Y1 = 0,
                Y2 = 24
            };
            _timeIndicatorCanvas.Children.Add(line);

            // Time label
            var label = new TextBlock
            {
                Text = zoomConfig.GetTimeIndicator(TimelineStartTime.Value, i),
                FontSize = 8,
                Foreground = new SolidColorBrush(Color.FromRgb(0x21, 0x25, 0x29)),
                Opacity = 0.7
            };
            Canvas.SetLeft(label, xPos + 2);
            Canvas.SetTop(label, 2);
            _timeIndicatorCanvas.Children.Add(label);
        }

        _timeIndicatorCanvas.Width = (totalLines * 90) + 100;
    }

    private void ScrollViewer_ScrollChanged(object? sender, ScrollChangedEventArgs e)
    {
        RenderTimeIndicators();
    }

    protected override void OnRenderSizeChanged(SizeChangedInfo sizeInfo)
    {
        base.OnRenderSizeChanged(sizeInfo);
        RenderTracks();
        RenderTimeIndicators();
        UpdatePlayheadPosition();
    }
}
