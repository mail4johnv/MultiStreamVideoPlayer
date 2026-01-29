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

#pragma once

#include "Common.h"
#include <vector>

namespace DX11VideoRenderer
{
    // Monitor information structure
    struct MonitorInfo
    {
        UINT        uDevID;
        HMONITOR    hMonitor;
        RECT        rcMonitor;
        DWORD       dwRefreshRate;
        WCHAR       szDevice[CCHDEVICENAME];
    };

    // Monitor array manager
    class CMonitorArray
    {
    public:
        CMonitorArray();
        ~CMonitorArray();

        HRESULT InitializeDisplaySystem(HWND hwnd);
        void    TerminateDisplaySystem();
        
        MonitorInfo* FindMonitor(HMONITOR hMon);
        HRESULT MatchGUID(UINT uDevID, DWORD* pdwMatchID);
        
        MonitorInfo& operator[](DWORD index) { return m_monitors[index]; }
        DWORD GetCount() const { return static_cast<DWORD>(m_monitors.size()); }

    private:
        static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
        
        std::vector<MonitorInfo> m_monitors;
    };

    // GPU adapter info structure
    struct GPUAdapterInfo
    {
        UINT            adapterIndex;
        WCHAR           description[128];
        DWORD           vendorId;
        DWORD           deviceId;
        UINT64          dedicatedVideoMemory;
        UINT64          sharedSystemMemory;
    };

    // Display manager for swap chain and device management
    class CDisplayManager : private CBase
    {
    public:
        CDisplayManager();
        ~CDisplayManager();

        // Initialization
        HRESULT InitializeDevice();   // Early init - creates D3D device and DXGI manager without HWND
        HRESULT Initialize(HWND hwndVideo);  // Full init with HWND for swap chain
        HRESULT Shutdown();

        // Device management
        ID3D11Device*           GetD3D11Device() const { return m_pD3D11Device; }
        ID3D11DeviceContext*    GetD3D11DeviceContext() const { return m_pD3DImmediateContext; }
        IMFDXGIDeviceManager*   GetDXGIDeviceManager() const { return m_pDXGIManager; }
        IDXGISwapChain1*        GetSwapChain() const { return m_pSwapChain1; }
        
        // Swap chain management
        HRESULT CreateSwapChain(UINT width, UINT height, BOOL stereo = FALSE);
        HRESULT ResizeSwapChain(UINT width, UINT height);
        HRESULT Present();
        
        // Monitor management
        HRESULT SetVideoMonitor(HWND hwndVideo);
        DWORD   GetMonitorRefreshRate() const;
        
        // GPU adapter management
        HRESULT EnumerateAdapters(std::vector<GPUAdapterInfo>* pAdapters);
        HRESULT SetGPUAdapter(UINT adapterIndex);
        
        // State
        BOOL    IsDeviceValid() const { return m_pD3D11Device != nullptr; }

    private:
        HRESULT CreateD3D11Device(IDXGIAdapter* pSpecificAdapter = nullptr);
        HRESULT CreateDXGIDeviceManager();
        
        CCritSec                    m_critSec;
        
        // GPU Adapter selection
        IDXGIAdapter*               m_pSelectedAdapter;
        UINT                        m_selectedAdapterIndex;
        
        // D3D11 objects
        ID3D11Device*               m_pD3D11Device;
        ID3D11DeviceContext*        m_pD3DImmediateContext;
        
        // DXGI objects
        IDXGIFactory2*              m_pDXGIFactory2;
        IDXGISwapChain1*            m_pSwapChain1;
        IDXGIOutput1*               m_pDXGIOutput1;
        
        // DXGI Device Manager
        IMFDXGIDeviceManager*       m_pDXGIManager;
        UINT                        m_DeviceResetToken;
        
        // DirectComposition (optional)
        IDCompositionDevice*        m_pDCompDevice;
        IDCompositionTarget*        m_pHwndTarget;
        IDCompositionVisual*        m_pRootVisual;
        
        // Monitor info
        CMonitorArray*              m_pMonitors;
        MonitorInfo*                m_pCurrentMonitor;
        HWND                        m_hwndVideo;
        
        // Flags
        BOOL                        m_bUseDComp;
        BOOL                        m_bResizing;  // Flag to prevent Present during resize
    };
}
