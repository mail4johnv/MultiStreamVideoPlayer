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

using Microsoft.Win32.SafeHandles;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using MultiStreamVideoPlayer.Views;
using MediaFoundation.Player;

namespace MultiStreamVideoPlayer;

public partial class App : Application
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AllocConsole();

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AttachConsole(int dwProcessId);
    
    public static int SelectedGPUAdapter { get; set; } = 0;
    
    protected override void OnStartup(StartupEventArgs e)
    {
        // Initialize logging - this must be first
        ManagedLogger.Initialize("Debug.log");
        ManagedLogger.Log("[App] Application startup");

#if DEBUG
        // Allocate a console for debug output only in Debug builds
        //InitializeConsole();
        //Console.WriteLine("[App] Console allocated for debug output");
#endif
        
        // Use default GPU adapter at startup (adapter 0)
        // User can change it via Settings button
        SelectedGPUAdapter = 0;
        ManagedLogger.Log("[App] GPU adapter initialized to default (0)");
        
        base.OnStartup(e);
    }

    public static void ShowGPUSelectorDialog()
    {
        try
        {
            var gpuDialog = new GPUSelectorDialog();
            bool? result = gpuDialog.ShowDialog();
            
            if (result == true)
            {
                SelectedGPUAdapter = gpuDialog.SelectedAdapterIndex;
                Console.WriteLine($"[App] GPU Adapter selected: {SelectedGPUAdapter}");
            }
            else
            {
                // User cancelled - use default (adapter 0)
                SelectedGPUAdapter = 0;
                Console.WriteLine("[App] GPU selection cancelled - using default adapter 0");
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Error displaying GPU selector: {ex.Message}\n\nUsing default GPU.",
                "GPU Selector Error",
                MessageBoxButton.OK,
                MessageBoxImage.Warning
            );
            SelectedGPUAdapter = 0;
        }
    }

    private static void InitializeConsole()
    {
        AllocConsole();
        
        // CRITICAL: Reinitialize Console streams after AllocConsole
        var standardOutput = new StreamWriter(Console.OpenStandardOutput());
        standardOutput.AutoFlush = true;
        Console.SetOut(standardOutput);
        
        var standardError = new StreamWriter(Console.OpenStandardError());
        standardError.AutoFlush = true;
        Console.SetError(standardError);
        
        // Optional: Set input stream if you need Console.ReadLine()
        Console.SetIn(new StreamReader(Console.OpenStandardInput()));
    }

    protected override void OnExit(ExitEventArgs e)
    {
        ManagedLogger.Log("[App] Application shutdown");
        ManagedLogger.Flush();

#if DEBUG
        //Console.WriteLine("[App] pres s any key to exit...");
        //Console.ReadKey();
        //Console.WriteLine("[App] exiting");
#endif  
        base.OnExit(e);
    }
}
