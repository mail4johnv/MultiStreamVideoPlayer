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
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using MediaFoundation.Player;

namespace MultiStreamVideoPlayer.Controls;

public class NativeMediaPlayer : HwndHost
{
    private MediaFoundation.Player.VideoPlayer? _player;
    private IntPtr _hwndHost;
    //private bool _isPlaying;
    private DispatcherTimer? _repaintTimer;
    
    // Detect RDP/Remote Desktop session using GetSystemMetrics
    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int nIndex);
    private const int SM_REMOTESESSION = 0x1000;
    private static readonly bool IsRemoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;

    public static readonly DependencyProperty SourceProperty =
        DependencyProperty.Register(nameof(Source), typeof(string), typeof(NativeMediaPlayer),
            new PropertyMetadata(null, OnSourceChanged));

    public static readonly DependencyProperty VolumeProperty =
        DependencyProperty.Register(nameof(Volume), typeof(double), typeof(NativeMediaPlayer),
            new PropertyMetadata(0.5, OnVolumeChanged));

    public static readonly DependencyProperty IsMutedProperty =
        DependencyProperty.Register(nameof(IsMuted), typeof(bool), typeof(NativeMediaPlayer),
            new PropertyMetadata(false, OnMutedChanged));

    public static readonly DependencyProperty SharpenStrengthProperty =
        DependencyProperty.Register(nameof(SharpenStrength), typeof(double), typeof(NativeMediaPlayer),
            new PropertyMetadata(0.0, OnSharpenStrengthChanged));

    public static readonly DependencyProperty SharpenThresholdProperty =
        DependencyProperty.Register(nameof(SharpenThreshold), typeof(double), typeof(NativeMediaPlayer),
            new PropertyMetadata(0.0, OnSharpenThresholdChanged));

    //public static readonly DependencyProperty TagProperty =
    //    DependencyProperty.Register(nameof(Tag), typeof(object), typeof(NativeMediaPlayer),
    //        new PropertyMetadata(null));

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

    public double SharpenStrength
    {
        get => (double)GetValue(SharpenStrengthProperty);
        set => SetValue(SharpenStrengthProperty, value);
    }

    public double SharpenThreshold
    {
        get => (double)GetValue(SharpenThresholdProperty);
        set => SetValue(SharpenThresholdProperty, value);
    }

    //public object? Tag
    //{
    //    get => GetValue(TagProperty);
    //    set => SetValue(TagProperty, value);
    //}

    public TimeSpan Duration => _player?.Duration ?? TimeSpan.Zero;

    public TimeSpan Position
    {
        get => _player?.Position ?? TimeSpan.Zero;
        set
        {
            if (_player != null)
            {
                _player.Position = value;
            }
        }
    }

    public bool HasAudio => _player?.HasAudio() ?? false;
    public bool HasVideo => _player?.HasVideo() ?? false;
    public bool CanPause => _player?.State == MediaFoundation.Player.PlayerState.Started;

    public event EventHandler? MediaOpened;
    public event EventHandler? MediaEnded;

    public NativeMediaPlayer()
    {
        //Console.WriteLine($"[NativeMediaPlayer] Constructor called");
        _player = new MediaFoundation.Player.VideoPlayer();
        
        // Set GPU adapter from App settings
        _player.SetGPUAdapter((uint)App.SelectedGPUAdapter);
        _player.SharpenStrength = SharpenStrength;
        _player.SharpenThreshold = SharpenThreshold;
        
        //Console.WriteLine($"[NativeMediaPlayer] VideoPlayer created - State={_player.State}");
        _player.MediaOpened += (s, e) =>
        {
            //Console.WriteLine($"[NativeMediaPlayer] MediaOpened event received (on worker thread)");
            Dispatcher.BeginInvoke(() =>
            {
                //Console.WriteLine($"[NativeMediaPlayer] MediaOpened - dispatched to UI thread");
                MediaOpened?.Invoke(this, EventArgs.Empty);
            });
        };
        _player.MediaEnded += (s, e) =>
        {
            //Console.WriteLine($"[NativeMediaPlayer] MediaEnded event received (on worker thread)");
            Dispatcher.BeginInvoke(() =>
            {
                //Console.WriteLine($"[NativeMediaPlayer] MediaEnded - dispatched to UI thread");
                //_isPlaying = false;
                MediaEnded?.Invoke(this, EventArgs.Empty);
            });
        };
        _player.MediaFailed += (s, e) =>
        {
            //Console.WriteLine($"[NativeMediaPlayer] MediaFailed event received (on worker thread)");
            Dispatcher.BeginInvoke(() =>
            {
                //Console.WriteLine($"[NativeMediaPlayer] MediaFailed - dispatched to UI thread");
            });
        };
    }

    protected override HandleRef BuildWindowCore(HandleRef hwndParent)
    {
        //Console.WriteLine($"[NativeMediaPlayer] BuildWindowCore called - hwndParent={hwndParent.Handle}");
        _hwndHost = CreateHostWindow(hwndParent.Handle);
        //Console.WriteLine($"[NativeMediaPlayer] Host window created - _hwndHost={_hwndHost}");
        
        if (_player != null)
        {
            _player.VideoWindow = _hwndHost;
            //Console.WriteLine($"[NativeMediaPlayer] VideoWindow set to {_hwndHost}");
            
            // If source was set before window was ready, open it now
            if (!string.IsNullOrEmpty(Source))
            {
                //Console.WriteLine($"[NativeMediaPlayer] Opening deferred source: {Source}");
                try
                {
                    _player.OpenUrl(Source);
                }
                catch (Exception ex)
                {
                    //Console.WriteLine($"[NativeMediaPlayer] Failed to open deferred source: {ex.Message}");
                }
            }
        }

        return new HandleRef(this, _hwndHost);
    }

    protected override void DestroyWindowCore(HandleRef hwnd)
    {
        //Console.WriteLine($"[NativeMediaPlayer] DestroyWindowCore called - hwnd={hwnd.Handle}");
        
        _repaintTimer?.Stop();
        _repaintTimer = null;
        
        // Shutdown the player before destroying the window
        if (_player != null)
        {
            //Console.WriteLine($"[NativeMediaPlayer] Shutting down player");
            _player.Shutdown();
            _player = null;
        }
        
        DestroyWindow(hwnd.Handle);
        //Console.WriteLine($"[NativeMediaPlayer] DestroyWindowCore complete");
    }

    protected override void OnRenderSizeChanged(SizeChangedInfo sizeInfo)
    {
        base.OnRenderSizeChanged(sizeInfo);
        UpdateVideoSize();
        
        // Schedule a repaint after resize settles (debounce)
        ScheduleRepaint();
    }

    private void ScheduleRepaint()
    {
        // Cancel any pending repaint
        _repaintTimer?.Stop();
        
        // Create timer if needed
        if (_repaintTimer == null)
        {
            _repaintTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(50)
            };
            _repaintTimer.Tick += (s, e) =>
            {
                _repaintTimer?.Stop();
                Repaint();
            };
        }
        
        // Start the timer
        _repaintTimer.Start();
    }

    protected override IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        const int WM_PAINT = 0x000F;
        const int WM_SIZE = 0x0005;
        const int WM_ERASEBKGND = 0x0014;

        try
        {
            switch (msg)
            {
                case WM_PAINT:
                    // Repaint the video when the window needs to be painted
                    // In RDP sessions, this may not work but won't cause issues
                    _player?.Repaint();
                    break;
                    
                case WM_SIZE:
                    // Update video size and repaint
                    if (_player != null && _hwndHost != IntPtr.Zero)
                    {
                        int width = (int)(lParam.ToInt64() & 0xFFFF);
                        int height = (int)((lParam.ToInt64() >> 16) & 0xFFFF);
                        if (width > 0 && height > 0)
                        {
                            _player.ResizeVideo(width, height);
                        }
                    }
                    break;
                    
                case WM_ERASEBKGND:
                    // Prevent background erase to avoid flicker
                    // In RDP sessions, allow normal erase to prevent black screen
                    if (!IsRemoteSession && _player?.HasVideo() == true)
                    {
                        handled = true;
                        return new IntPtr(1);
                    }
                    break;
            }
        }
        catch (Exception ex)
        {
            // Log but don't crash - RDP sessions may have rendering issues
            //Console.WriteLine($"[NativeMediaPlayer] WndProc error: {ex.Message}");
        }

        return base.WndProc(hwnd, msg, wParam, lParam, ref handled);
    }

    private void UpdateVideoSize()
    {
        if (_player != null)
        {
            _player.ResizeVideo((int)ActualWidth, (int)ActualHeight);
        }
    }

    /// <summary>
    /// Repaint the video frame. Call this when the window is restored or becomes visible.
    /// </summary>
    public void Repaint()
    {
        _player?.Repaint();
    }

    private IntPtr CreateHostWindow(IntPtr hwndParent)
    {
        int x = 0;
        int y = 0;
        int width = (int)ActualWidth;
        int height = (int)ActualHeight;
        
        // Use reasonable defaults if size is 0
        if (width <= 0) width = 640;
        if (height <= 0) height = 480;
        
        //Console.WriteLine($"[NativeMediaPlayer] CreateHostWindow - parent={hwndParent}, size={width}x{height}");

        IntPtr hwndHost = CreateWindowEx(
            0,
            "static",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            x, y, width, height,
            hwndParent,
            IntPtr.Zero,
            IntPtr.Zero,
            IntPtr.Zero);
        
        //Console.WriteLine($"[NativeMediaPlayer] CreateHostWindow - hwndHost={hwndHost}");

        return hwndHost;
    }

    public void Play()
    {
        //Console.WriteLine($"[NativeMediaPlayer] Play() called. _player={_player != null}, State={_player?.State}, Source={Source}");
        if (_player != null)
        {
            //Console.WriteLine($"[NativeMediaPlayer] Calling _player.Play()");
            _player.Play();
            //_isPlaying = true;
            //Console.WriteLine($"[NativeMediaPlayer] After Play() - State={_player.State}");
        }
        else
        {
            //Console.WriteLine($"[NativeMediaPlayer] Play() skipped - _player is null");
        }
    }

    public void Pause()
    {
        if (_player != null && CanPause)
        {
            _player.Pause();
            //_isPlaying = false;
        }
    }

    public void Stop()
    {
        if (_player != null)
        {
            _player.Stop();
            //_isPlaying = false;
        }
    }

    protected override void Dispose(bool disposing)
    {
        //Console.WriteLine($"[NativeMediaPlayer] Dispose({disposing}) called");
        if (disposing)
        {
            // Shutdown player if not already done by DestroyWindowCore
            if (_player != null)
            {
                //Console.WriteLine($"[NativeMediaPlayer] Dispose: Shutting down player");
                _player.Shutdown();
                _player = null;
            }
        }
        base.Dispose(disposing);
        //Console.WriteLine($"[NativeMediaPlayer] Dispose complete");
    }

    private static void OnSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        //Console.WriteLine($"[NativeMediaPlayer] OnSourceChanged - NewValue={e.NewValue}");
        if (d is NativeMediaPlayer player && e.NewValue is string source && player._player != null)
        {
            // Only open if the window handle is already set
            // If not set yet, BuildWindowCore will open it when the window is ready
            if (player._hwndHost != IntPtr.Zero)
            {
                try
                {
                    //Console.WriteLine($"[NativeMediaPlayer] Calling OpenUrl({source})");
                    player._player.OpenUrl(source);
                    //Console.WriteLine($"[NativeMediaPlayer] OpenUrl completed - State={player._player.State}");
                }
                catch (Exception ex)
                {
                    //Console.WriteLine($"[NativeMediaPlayer] Failed to open: {ex.Message}");
                }
            }
            else
            {
                //Console.WriteLine($"[NativeMediaPlayer] OnSourceChanged deferred - window handle not ready yet");
            }
        }
        else
        {
            //Console.WriteLine($"[NativeMediaPlayer] OnSourceChanged skipped - player={d is NativeMediaPlayer}, source={e.NewValue is string}, _player={((d as NativeMediaPlayer)?._player != null)}");
        }
    }

    private static void OnVolumeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NativeMediaPlayer player && e.NewValue is double volume)
        {
            ManagedLogger.LogInfo($"[NativeMediaPlayer.OnVolumeChanged] New volume: {volume}, Old volume: {e.OldValue}");
            if (player._player != null)
            {
                try
                {
                    player._player.Volume = volume;
                    ManagedLogger.LogInfo($"[NativeMediaPlayer.OnVolumeChanged] Volume set successfully to {volume}");
                }
                catch (Exception ex)
                {
                    ManagedLogger.LogError($"[NativeMediaPlayer] Error setting volume: {ex.Message}");
                }
            }
            else
            {
                ManagedLogger.LogWarning($"[NativeMediaPlayer.OnVolumeChanged] _player is null, cannot set volume");
            }
        }
    }

    private static void OnMutedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NativeMediaPlayer player && e.NewValue is bool muted)
        {
            ManagedLogger.LogInfo($"[NativeMediaPlayer.OnMutedChanged] New muted: {muted}, Old muted: {e.OldValue}");
            if (player._player != null)
            {
                try
                {
                    player._player.IsMuted = muted;
                    ManagedLogger.LogInfo($"[NativeMediaPlayer.OnMutedChanged] Muted set successfully to {muted}");
                }
                catch (Exception ex)
                {
                    ManagedLogger.LogError($"[NativeMediaPlayer] Error setting mute: {ex.Message}");
                }
            }
            else
            {
                ManagedLogger.LogWarning($"[NativeMediaPlayer.OnMutedChanged] _player is null, cannot set mute");
            }
        }
    }

    private static void OnSharpenStrengthChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NativeMediaPlayer player && e.NewValue is double sharpenStrength)
        {
            if (player._player != null)
            {
                try
                {
                    player._player.SharpenStrength = sharpenStrength;
                    player.Repaint();
                }
                catch (Exception ex)
                {
                    ManagedLogger.LogError($"[NativeMediaPlayer] Error setting sharpen strength: {ex.Message}");
                }
            }
        }
    }

    private static void OnSharpenThresholdChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NativeMediaPlayer player && e.NewValue is double sharpenThreshold)
        {
            if (player._player != null)
            {
                try
                {
                    player._player.SharpenThreshold = sharpenThreshold;
                    player.Repaint();
                }
                catch (Exception ex)
                {
                    ManagedLogger.LogError($"[NativeMediaPlayer] Error setting sharpen threshold: {ex.Message}");
                }
            }
        }
    }

    // P/Invoke declarations
    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr CreateWindowEx(
        int dwExStyle,
        string lpClassName,
        string lpWindowName,
        int dwStyle,
        int x, int y,
        int nWidth, int nHeight,
        IntPtr hwndParent,
        IntPtr hMenu,
        IntPtr hInstance,
        IntPtr lpParam);

    [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(IntPtr hwnd);

    private const int WS_CHILD = 0x40000000;
    private const int WS_VISIBLE = 0x10000000;
    private const int WS_CLIPCHILDREN = 0x02000000;
}
