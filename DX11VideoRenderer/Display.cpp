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

#include "Display.h"
#include <cstdio>
#include <cstdarg>

using namespace DX11VideoRenderer;

// Debug logging function - delegates to thread-safe logger
#define DebugLog LOG_DEBUG
//void DebugLog(const char* format, ...)
//{
//    char buffer[2048];
//    va_list args;
//    va_start(args, format);
//    vsnprintf(buffer, sizeof(buffer), format, args);
//    va_end(args);
//    
//    // Use the new thread-safe logger
//    CThreadSafeLogger::GetInstance().Log("%s", buffer);
//}

//-----------------------------------------------------------------------------
// CMonitorArray
//-----------------------------------------------------------------------------

CMonitorArray::CMonitorArray()
{
}

CMonitorArray::~CMonitorArray()
{
    TerminateDisplaySystem();
}

HRESULT CMonitorArray::InitializeDisplaySystem(HWND hwnd)
{
    m_monitors.clear();
    
    if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(this)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    
    return S_OK;
}

void CMonitorArray::TerminateDisplaySystem()
{
    m_monitors.clear();
}

MonitorInfo* CMonitorArray::FindMonitor(HMONITOR hMon)
{
    for (auto& mon : m_monitors)
    {
        if (mon.hMonitor == hMon)
        {
            return &mon;
        }
    }
    return nullptr;
}

HRESULT CMonitorArray::MatchGUID(UINT uDevID, DWORD* pdwMatchID)
{
    if (!pdwMatchID)
    {
        return E_POINTER;
    }
    
    for (DWORD i = 0; i < m_monitors.size(); i++)
    {
        if (m_monitors[i].uDevID == uDevID)
        {
            *pdwMatchID = i;
            return S_OK;
        }
    }
    
    return S_FALSE;
}

BOOL CALLBACK CMonitorArray::MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    CMonitorArray* pThis = reinterpret_cast<CMonitorArray*>(dwData);
    
    MONITORINFOEXW monInfo = {};
    monInfo.cbSize = sizeof(monInfo);
    
    if (GetMonitorInfoW(hMonitor, &monInfo))
    {
        MonitorInfo info = {};
        info.uDevID = static_cast<UINT>(pThis->m_monitors.size());
        info.hMonitor = hMonitor;
        info.rcMonitor = monInfo.rcMonitor;
        wcscpy_s(info.szDevice, monInfo.szDevice);
        
        // Get refresh rate
        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(devMode);
        if (EnumDisplaySettingsW(monInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
        {
            info.dwRefreshRate = devMode.dmDisplayFrequency;
        }
        else
        {
            info.dwRefreshRate = 60; // Default
        }
        
        pThis->m_monitors.push_back(info);
    }
    
    return TRUE;
}

//-----------------------------------------------------------------------------
// CDisplayManager
//-----------------------------------------------------------------------------

CDisplayManager::CDisplayManager() :
    m_pD3D11Device(nullptr),
    m_pD3DImmediateContext(nullptr),
    m_pDXGIFactory2(nullptr),
    m_pSwapChain1(nullptr),
    m_pDXGIOutput1(nullptr),
    m_pDXGIManager(nullptr),
    m_DeviceResetToken(0),
    m_pDCompDevice(nullptr),
    m_pHwndTarget(nullptr),
    m_pRootVisual(nullptr),
    m_pMonitors(nullptr),
    m_pCurrentMonitor(nullptr),
    m_hwndVideo(nullptr),
    m_bUseDComp(FALSE),
    m_pSelectedAdapter(nullptr),
    m_selectedAdapterIndex(0),
    m_bResizing(FALSE)
{
}

CDisplayManager::~CDisplayManager()
{
    Shutdown();
}

// Early initialization - creates D3D device and DXGI device manager without needing HWND
// This allows hardware decoding to work because the decoder can get the device manager
// before SetVideoWindow is called.
HRESULT CDisplayManager::InitializeDevice()
{
    CAutoLock lock(&m_critSec);
    
    DebugLog("[DX11Display] InitializeDevice called\n");
    
    // If device already created, just return success
    if (m_pD3D11Device != nullptr)
    {
        DebugLog("[DX11Display] InitializeDevice: device already created\n");
        return S_OK;
    }
    
    HRESULT hr = S_OK;
    
    // Create D3D11 device (uses default adapter)
    hr = CreateD3D11Device();
    if (FAILED(hr))
    {
        DebugLog("[DX11Display] InitializeDevice: CreateD3D11Device failed hr=0x%08X\n", hr);
        return hr;
    }
    
    // Create DXGI Device Manager
    hr = CreateDXGIDeviceManager();
    if (FAILED(hr))
    {
        DebugLog("[DX11Display] InitializeDevice: CreateDXGIDeviceManager failed hr=0x%08X\n", hr);
        return hr;
    }
    
    DebugLog("[DX11Display] InitializeDevice: SUCCESS\n");
    return S_OK;
}

HRESULT CDisplayManager::Initialize(HWND hwndVideo)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = S_OK;
    
    DebugLog("[DX11Display] Initialize called hwnd=0x%p\n", hwndVideo);
    
    if (!IsWindow(hwndVideo))
    {
        return E_INVALIDARG;
    }
    
    m_hwndVideo = hwndVideo;
    
    // Initialize monitor array
    if (!m_pMonitors)
    {
        m_pMonitors = DBG_NEW CMonitorArray();
        if (!m_pMonitors)
        {
            return E_OUTOFMEMORY;
        }
    }
    
    hr = SetVideoMonitor(hwndVideo);
    if (FAILED(hr))
    {
        return hr;
    }
    
    // Create D3D11 device if not already created
    if (!m_pD3D11Device)
    {
        hr = CreateD3D11Device();
        if (FAILED(hr))
        {
            return hr;
        }
    }
    
    // Create DXGI Device Manager if not already created
    if (!m_pDXGIManager)
    {
        hr = CreateDXGIDeviceManager();
        if (FAILED(hr))
        {
            return hr;
        }
    }
    
    DebugLog("[DX11Display] Initialize: SUCCESS\n");
    return S_OK;
}

HRESULT CDisplayManager::Shutdown()
{
    CAutoLock lock(&m_critSec);
    
    DebugLog("[DX11Display] Shutdown: starting\n");
    
    // First, release the swap chain (depends on device)
    SafeRelease(m_pSwapChain1);
    
    // Release DComp objects
    SafeRelease(m_pRootVisual);
    SafeRelease(m_pHwndTarget);
    SafeRelease(m_pDCompDevice);
    
    // Release DXGI objects
    SafeRelease(m_pDXGIOutput1);
    SafeRelease(m_pDXGIFactory2);
    SafeRelease(m_pDXGIManager);
    
    // Flush and clear the device context before releasing
    // This ensures all pending GPU work is completed and references are dropped
    if (m_pD3DImmediateContext)
    {
        m_pD3DImmediateContext->ClearState();
        m_pD3DImmediateContext->Flush();
    }
    
    SafeRelease(m_pD3DImmediateContext);
    
    // Release selected adapter
    SafeRelease(m_pSelectedAdapter);

#ifdef _DEBUG
    // Report live D3D11 objects before releasing device
    if (m_pD3D11Device)
    {
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(m_pD3D11Device->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            DebugLog("[DX11Display] Reporting live D3D11 objects:\n");
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
            pDebug->Release();
        }
    }
#endif
    
    SafeRelease(m_pD3D11Device);
    
    SafeDelete(m_pMonitors);
    m_pCurrentMonitor = nullptr;
    
    DebugLog("[DX11Display] Shutdown: complete\n");
    
    return S_OK;
}

HRESULT CDisplayManager::CreateD3D11Device(IDXGIAdapter* pSpecificAdapter)
{
    HRESULT hr = S_OK;
    
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    
    D3D_FEATURE_LEVEL featureLevel;
    UINT createFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    // Determine which adapter to use
    IDXGIAdapter* pAdapter = pSpecificAdapter ? pSpecificAdapter : m_pSelectedAdapter;
    D3D_DRIVER_TYPE driverType = pAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    
    // Try hardware first with specified adapter
    hr = D3D11CreateDevice(
        pAdapter,
        driverType,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_pD3D11Device,
        &featureLevel,
        &m_pD3DImmediateContext
    );
    
    if (FAILED(hr))
    {
        DebugLog("[DX11Display] CreateD3D11Device: Hardware device creation failed, trying WARP\n");
        // Fallback to WARP
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &m_pD3D11Device,
            &featureLevel,
            &m_pD3DImmediateContext
        );
    }
    
    if (FAILED(hr))
    {
        DebugLog("[DX11Display] CreateD3D11Device: WARP device creation failed\n");
        return hr;
    }
    
    // Enable multithreaded mode
    ID3D10Multithread* pMultiThread = nullptr;
    hr = m_pD3DImmediateContext->QueryInterface(__uuidof(ID3D10Multithread), (void**)&pMultiThread);
    if (SUCCEEDED(hr))
    {
        pMultiThread->SetMultithreadProtected(TRUE);
        pMultiThread->Release();
    }
    
    // Get DXGI factory
    IDXGIDevice1* pDXGIDevice = nullptr;
    hr = m_pD3D11Device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&pDXGIDevice);
    if (FAILED(hr))
    {
        return hr;
    }
    
    IDXGIAdapter* pDeviceAdapter = nullptr;
    hr = pDXGIDevice->GetAdapter(&pDeviceAdapter);
    pDXGIDevice->Release();
    
    if (FAILED(hr))
    {
        return hr;
    }
    
    hr = pDeviceAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&m_pDXGIFactory2);
    
    // Get output
    IDXGIOutput* pOutput = nullptr;
    if (SUCCEEDED(pDeviceAdapter->EnumOutputs(0, &pOutput)))
    {
        pOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&m_pDXGIOutput1);
        pOutput->Release();
    }
    
    pDeviceAdapter->Release();
    
    return hr;
}

HRESULT CDisplayManager::CreateDXGIDeviceManager()
{
    HRESULT hr = MFCreateDXGIDeviceManager(&m_DeviceResetToken, &m_pDXGIManager);
    if (FAILED(hr))
    {
        return hr;
    }
    
    hr = m_pDXGIManager->ResetDevice(m_pD3D11Device, m_DeviceResetToken);
    return hr;
}

HRESULT CDisplayManager::CreateSwapChain(UINT width, UINT height, BOOL stereo)
{
    CAutoLock lock(&m_critSec);
    
    if (!m_pDXGIFactory2 || !m_pD3D11Device)
    {
        return E_UNEXPECTED;
    }
    
    // Release existing swap chain
    SafeRelease(m_pSwapChain1);
    
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = stereo;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 4;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = stereo ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;
    
    HRESULT hr;
    
    if (m_bUseDComp && m_pRootVisual)
    {
        hr = m_pDXGIFactory2->CreateSwapChainForComposition(
            m_pD3D11Device,
            &swapChainDesc,
            nullptr,
            &m_pSwapChain1
        );
        
        if (SUCCEEDED(hr))
        {
            m_pRootVisual->SetContent(m_pSwapChain1);
            m_pDCompDevice->Commit();
        }
    }
    else
    {
        hr = m_pDXGIFactory2->CreateSwapChainForHwnd(
            m_pD3D11Device,
            m_hwndVideo,
            &swapChainDesc,
            nullptr,
            nullptr,
            &m_pSwapChain1
        );
    }
    
    return hr;
}

HRESULT CDisplayManager::ResizeSwapChain(UINT width, UINT height)
{
    CAutoLock lock(&m_critSec);
    
    if (!m_pSwapChain1)
    {
        return CreateSwapChain(width, height);
    }
    
    // Set resizing flag to prevent Present calls during resize
    m_bResizing = TRUE;
    
    // Small delay to ensure any in-flight Present calls complete
    Sleep(16); // ~1 frame at 60fps
    
    HRESULT hr = m_pSwapChain1->ResizeBuffers(4, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    
    m_bResizing = FALSE;
    
    return hr;
}

HRESULT CDisplayManager::Present()
{
    CAutoLock lock(&m_critSec);
    
    if (!m_pSwapChain1)
    {
        return E_UNEXPECTED;
    }
    
    // Don't present if resizing is in progress
    if (m_bResizing)
    {
        return S_OK; // Skip this frame
    }
    
    return m_pSwapChain1->Present(1, 0);
}

HRESULT CDisplayManager::SetVideoMonitor(HWND hwndVideo)
{
    if (!m_pMonitors)
    {
        return E_UNEXPECTED;
    }
    
    HRESULT hr = m_pMonitors->InitializeDisplaySystem(hwndVideo);
    if (FAILED(hr))
    {
        return hr;
    }
    
    HMONITOR hMon = MonitorFromWindow(hwndVideo, MONITOR_DEFAULTTONEAREST);
    m_pCurrentMonitor = m_pMonitors->FindMonitor(hMon);
    
    return S_OK;
}

DWORD CDisplayManager::GetMonitorRefreshRate() const
{
    if (m_pCurrentMonitor)
    {
        return m_pCurrentMonitor->dwRefreshRate;
    }
    return 60; // Default
}

HRESULT CDisplayManager::EnumerateAdapters(std::vector<GPUAdapterInfo>* pAdapters)
{
    if (!pAdapters)
    {
        return E_POINTER;
    }

    IDXGIFactory1* pFactory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory);
    if (FAILED(hr))
    {
        DebugLog("[DX11Display] EnumerateAdapters: CreateDXGIFactory1 failed hr=0x%08X\n", hr);
        return hr;
    }

    UINT adapterIndex = 0;
    IDXGIAdapter* pAdapter = nullptr;

    // Enumerate all adapters
    while (SUCCEEDED(pFactory->EnumAdapters(adapterIndex, &pAdapter)))
    {
        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);

        GPUAdapterInfo info = {};
        info.adapterIndex = adapterIndex;
        wcscpy_s(info.description, ARRAYSIZE(info.description), desc.Description);
        info.vendorId = desc.VendorId;
        info.deviceId = desc.DeviceId;
        info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
        info.sharedSystemMemory = desc.SharedSystemMemory;

        pAdapters->push_back(info);

        DebugLog("[DX11Display] Adapter %u: %ws\n", adapterIndex,
                 info.description);
        DebugLog("              VendorID=0x%X, DeviceID=0x%X\n",
                 info.vendorId, info.deviceId);
        DebugLog("              VRAM=%llu MB, Shared Memory=%llu MB\n",
                 info.dedicatedVideoMemory / (1024 * 1024),
                 info.sharedSystemMemory / (1024 * 1024));

        pAdapter->Release();
        adapterIndex++;
    }

    pFactory->Release();

    if (pAdapters->empty())
    {
        DebugLog("[DX11Display] EnumerateAdapters: No adapters found\n");
        return E_FAIL;
    }

    DebugLog("[DX11Display] EnumerateAdapters: Found %zu adapters\n", pAdapters->size());
    return S_OK;
}

HRESULT CDisplayManager::SetGPUAdapter(UINT adapterIndex)
{
    CAutoLock lock(&m_critSec);

    // Don't allow changing adapter after device is created
    if (m_pD3D11Device)
    {
        DebugLog("[DX11Display] SetGPUAdapter: Cannot change adapter after device creation\n");
        return E_FAIL;
    }

    IDXGIFactory1* pFactory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory);
    if (FAILED(hr))
    {
        DebugLog("[DX11Display] SetGPUAdapter: CreateDXGIFactory1 failed hr=0x%08X\n", hr);
        return hr;
    }

    // Release previously selected adapter
    if (m_pSelectedAdapter)
    {
        m_pSelectedAdapter->Release();
        m_pSelectedAdapter = nullptr;
    }

    // Get the requested adapter
    hr = pFactory->EnumAdapters(adapterIndex, &m_pSelectedAdapter);
    pFactory->Release();

    if (FAILED(hr))
    {
        DebugLog("[DX11Display] SetGPUAdapter: EnumAdapters failed for index %u, hr=0x%08X\n", adapterIndex, hr);
        return hr;
    }

    m_selectedAdapterIndex = adapterIndex;

    DXGI_ADAPTER_DESC desc;
    m_pSelectedAdapter->GetDesc(&desc);
    DebugLog("[DX11Display] SetGPUAdapter: Selected adapter %u - %ws\n", adapterIndex, desc.Description);

    return S_OK;
}
