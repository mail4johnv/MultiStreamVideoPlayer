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

using System.Windows;
using System.Windows.Controls;
using MultiStreamVideoPlayer.ViewModels;
using System.ComponentModel;
using System.Diagnostics;
using System.Windows.Threading;

namespace MultiStreamVideoPlayer.Views;

public partial class MainWindow : Window
{
    private MainViewModel ViewModel => (MainViewModel)DataContext;
    private readonly Stopwatch _stopwatch = new();
    private readonly DispatcherTimer _repaintDebounce;

    public MainWindow()
    {
        InitializeComponent();
        _stopwatch.Start();
        _repaintDebounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(150) };
        _repaintDebounce.Tick += (s, e) =>
        {
            _repaintDebounce.Stop();
            ViewModel?.RepaintAllVideos();
        };
        StateChanged += MainWindow_StateChanged;
        Closing += MainWindow_Closing;
    }

    private void MainWindow_Closing(object? sender, CancelEventArgs e)
    {
        //Console.WriteLine($"[MainWindow] [{_stopwatch.Elapsed:mm\\:ss\\.fff}] Closing event - cleaning up");
        ViewModel?.Cleanup();
        //Console.WriteLine($"[MainWindow] [{_stopwatch.Elapsed:mm\\:ss\\.fff}] Cleanup complete");
    }

    private void MainWindow_StateChanged(object? sender, System.EventArgs e)
    {
        //Console.WriteLine($"[MainWindow] [{_stopwatch.Elapsed:mm\\:ss\\.fff}] StateChanged: WindowState={WindowState}");
        
        // DX11 Flip Model: Video continues rendering even when minimized (like Films & TV)
        // Only repaint when window is restored to ensure proper display
        if (WindowState == WindowState.Minimized)
        {
            //Console.WriteLine($"[MainWindow] [{_stopwatch.Elapsed:mm\\:ss\\.fff}] Window MINIMIZED - flip model should continue rendering");
        }
        else if (WindowState == WindowState.Normal)
        {
            //Console.WriteLine($"[MainWindow] [{_stopwatch.Elapsed:mm\\:ss\\.fff}] Window RESTORED to normal - repainting videos");
            _repaintDebounce.Stop();
            _repaintDebounce.Start();
        }
        else if (WindowState == WindowState.Maximized)
        {
            //Console.WriteLine($"[MainWindow] [{_stopwatch.Elapsed:mm\\:ss\\.fff}] Window MAXIMIZED - repainting videos");
            _repaintDebounce.Stop();
            _repaintDebounce.Start();
        }
        // Note: Do NOT pause playback when minimized - flip model handles background rendering
    }

    private void MediaElement_Loaded(object sender, RoutedEventArgs e)
    {
        ViewModel.RegisterMediaElement(sender);
    }

    private void MediaElement_Unloaded(object sender, RoutedEventArgs e)
    {
        ViewModel.UnregisterMediaElement(sender);
    }

    private void TimelineControl_SeekRequested(object? sender, double percentage)
    {
        ViewModel.SeekToPercentage(percentage);
    }
}
