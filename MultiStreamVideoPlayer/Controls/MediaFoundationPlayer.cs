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

using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MediaFoundation.Player;

namespace MultiStreamVideoPlayer.Controls;

public class MediaFoundationPlayer : Control
{
    private MediaElement? _mediaElement;
    //private bool _isPlaying;

    public static readonly DependencyProperty SourceProperty =
        DependencyProperty.Register(nameof(Source), typeof(string), typeof(MediaFoundationPlayer),
            new PropertyMetadata(null, OnSourceChanged));

    public static readonly DependencyProperty VolumeProperty =
        DependencyProperty.Register(nameof(Volume), typeof(double), typeof(MediaFoundationPlayer),
            new PropertyMetadata(0.5, OnVolumeChanged));

    public static readonly DependencyProperty IsMutedProperty =
        DependencyProperty.Register(nameof(IsMuted), typeof(bool), typeof(MediaFoundationPlayer),
            new PropertyMetadata(false, OnMutedChanged));

    public static new readonly DependencyProperty TagProperty =
        DependencyProperty.Register(nameof(Tag), typeof(object), typeof(MediaFoundationPlayer),
            new PropertyMetadata(null));

    public string? Source
    {
        get => (string?)GetValue(SourceProperty);
        set => SetValue(SourceProperty, value);
    }

    public double Volume
    {
        get => (double)GetValue(VolumeProperty);
        set => SetValue(VolumeProperty, value);
    }

    public bool IsMuted
    {
        get => (bool)GetValue(IsMutedProperty);
        set => SetValue(IsMutedProperty, value);
    }

    public new object? Tag
    {
        get => GetValue(TagProperty);
        set => SetValue(TagProperty, value);
    }

    public TimeSpan Duration => _mediaElement?.NaturalDuration.HasTimeSpan == true 
        ? _mediaElement.NaturalDuration.TimeSpan 
        : TimeSpan.Zero;

    public TimeSpan Position
    {
        get => _mediaElement?.Position ?? TimeSpan.Zero;
        set
        {
            if (_mediaElement != null && _mediaElement.NaturalDuration.HasTimeSpan)
            {
                _mediaElement.Position = value;
            }
        }
    }

    public bool HasAudio => _mediaElement?.HasAudio ?? false;
    public bool HasVideo => _mediaElement?.HasVideo ?? false;
    public bool CanPause => _mediaElement?.CanPause ?? false;

    public event EventHandler? MediaOpened;
    public event EventHandler? MediaEnded;

    static MediaFoundationPlayer()
    {
        DefaultStyleKeyProperty.OverrideMetadata(typeof(MediaFoundationPlayer),
            new FrameworkPropertyMetadata(typeof(MediaFoundationPlayer)));
    }

    public MediaFoundationPlayer()
    {
        Background = Brushes.Black;
        
        // Initialize MediaElement immediately for design-time support
        if (DesignerProperties.GetIsInDesignMode(this))
        {
            // In designer, create a placeholder
            InitializeMediaElement();
        }
        else
        {
            // At runtime, wait for Loaded event
            Loaded += MediaFoundationPlayer_Loaded;
            Unloaded += MediaFoundationPlayer_Unloaded;
        }
    }

    private void InitializeMediaElement()
    {
        if (_mediaElement == null)
        {
            _mediaElement = new MediaElement
            {
                LoadedBehavior = MediaState.Manual,
                UnloadedBehavior = MediaState.Manual,
                Stretch = System.Windows.Media.Stretch.Uniform
            };

            if (!DesignerProperties.GetIsInDesignMode(this))
            {
                _mediaElement.MediaOpened += (s, args) => MediaOpened?.Invoke(this, EventArgs.Empty);
                _mediaElement.MediaEnded += (s, args) =>
                {
                    //_isPlaying = false;
                    MediaEnded?.Invoke(this, EventArgs.Empty);
                };
            }

            AddVisualChild(_mediaElement);
            AddLogicalChild(_mediaElement);

            if (!string.IsNullOrEmpty(Source))
            {
                try
                {
                    _mediaElement.Source = new Uri(Source, UriKind.RelativeOrAbsolute);
                }
                catch
                {
                    // Ignore errors in design mode
                }
            }
        }
    }

    private void MediaFoundationPlayer_Loaded(object sender, RoutedEventArgs e)
    {
        InitializeMediaElement();
    }

    private void MediaFoundationPlayer_Unloaded(object sender, RoutedEventArgs e)
    {
        if (_mediaElement != null)
        {
            _mediaElement.Stop();
            _mediaElement.Close();
        }
    }

    protected override int VisualChildrenCount => _mediaElement != null ? 1 : 0;

    protected override Visual GetVisualChild(int index)
    {
        if (_mediaElement == null || index != 0)
            throw new ArgumentOutOfRangeException(nameof(index));
        return _mediaElement;
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        _mediaElement?.Measure(availableSize);
        return base.MeasureOverride(availableSize);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        _mediaElement?.Arrange(new Rect(finalSize));
        return base.ArrangeOverride(finalSize);
    }

    public void Play()
    {
        if (_mediaElement != null)
        {
            _mediaElement.Play();
            //_isPlaying = true;
        }
    }

    public void Pause()
    {
        if (_mediaElement != null && _mediaElement.CanPause)
        {
            _mediaElement.Pause();
            //_isPlaying = false;
        }
    }

    public void Stop()
    {
        if (_mediaElement != null)
        {
            _mediaElement.Stop();
            //_isPlaying = false;
        }
    }

    private static void OnSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MediaFoundationPlayer player && e.NewValue is string source && player._mediaElement != null)
        {
            try
            {
                player._mediaElement.Source = new Uri(source, UriKind.RelativeOrAbsolute);
            }
            catch (Exception ex)
            {
                //Console.WriteLine($"Failed to set source: {ex.Message}");
            }
        }
    }

    private static void OnVolumeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MediaFoundationPlayer player && e.NewValue is double volume)
        {
            ManagedLogger.LogInfo($"[MediaFoundationPlayer.OnVolumeChanged] New volume: {volume}, Old volume: {e.OldValue}");
            if (player._mediaElement != null)
            {
                try
                {
                    player._mediaElement.Volume = volume;
                    ManagedLogger.LogInfo($"[MediaFoundationPlayer.OnVolumeChanged] Volume set successfully to {volume}");
                }
                catch (Exception ex)
                {
                    ManagedLogger.LogError($"[MediaFoundationPlayer] Error setting volume: {ex.Message}");
                }
            }
            else
            {
                ManagedLogger.LogWarning($"[MediaFoundationPlayer.OnVolumeChanged] _mediaElement is null, cannot set volume");
            }
        }
    }

    private static void OnMutedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MediaFoundationPlayer player && e.NewValue is bool muted)
        {
            ManagedLogger.LogInfo($"[MediaFoundationPlayer.OnMutedChanged] New muted: {muted}, Old muted: {e.OldValue}");
            if (player._mediaElement != null)
            {
                try
                {
                    player._mediaElement.IsMuted = muted;
                    ManagedLogger.LogInfo($"[MediaFoundationPlayer.OnMutedChanged] Muted set successfully to {muted}");
                }
                catch (Exception ex)
                {
                    ManagedLogger.LogError($"[MediaFoundationPlayer] Error setting mute: {ex.Message}");
                }
            }
            else
            {
                ManagedLogger.LogWarning($"[MediaFoundationPlayer.OnMutedChanged] _mediaElement is null, cannot set mute");
            }
        }
    }
}
