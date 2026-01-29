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
using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Data;
using MultiStreamVideoPlayer.ViewModels;

namespace MultiStreamVideoPlayer.Views;

public partial class GPUSelectorDialog : Window
{
    public class GPUItemViewModel
    {
        public int AdapterIndex { get; set; }
        public string Description { get; set; }
        public string MemoryInfo { get; set; }
        public ulong VendorId { get; set; }
        public ulong DeviceId { get; set; }
    }

    public int SelectedAdapterIndex { get; private set; } = 0;
    public bool UserCancelled { get; private set; } = false;

    public GPUSelectorDialog()
    {
        InitializeComponent();
        PopulateGPUList();
        
        // Select first GPU by default
        if (GPUListBox.Items.Count > 0)
        {
            GPUListBox.SelectedIndex = 0;
        }
    }

    private void PopulateGPUList()
    {
        try
        {
            var gpuList = new ObservableCollection<GPUItemViewModel>();
            
            // Enumerate GPUs using the VideoPlayer
            var player = new MediaFoundation.Player.VideoPlayer();
            var gpus = player.EnumerateGPUs();
            
            foreach (var gpu in gpus)
            {
                ulong vramMB = gpu.DedicatedVideoMemory / (1024 * 1024);
                ulong sharedMB = gpu.SharedSystemMemory / (1024 * 1024);
                
                var item = new GPUItemViewModel
                {
                    AdapterIndex = (int)gpu.AdapterIndex,
                    Description = gpu.Description,
                    MemoryInfo = $"VRAM: {vramMB} MB | Shared: {sharedMB} MB | Vendor: 0x{gpu.VendorId:X} | Device: 0x{gpu.DeviceId:X}",
                    VendorId = gpu.VendorId,
                    DeviceId = gpu.DeviceId
                };
                
                gpuList.Add(item);
            }
            
            player.Shutdown();
            
            GPUListBox.ItemsSource = gpuList;
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Error enumerating GPUs: {ex.Message}",
                "GPU Detection Error",
                MessageBoxButton.OK,
                MessageBoxImage.Error
            );
        }
    }

    private void OKButton_Click(object sender, RoutedEventArgs e)
    {
        if (GPUListBox.SelectedItem is GPUItemViewModel selectedGPU)
        {
            SelectedAdapterIndex = selectedGPU.AdapterIndex;
            UserCancelled = false;
            DialogResult = true;
            Close();
        }
    }

    private void CancelButton_Click(object sender, RoutedEventArgs e)
    {
        UserCancelled = true;
        SelectedAdapterIndex = 0; // Default to first GPU
        DialogResult = false;
        Close();
    }
}
