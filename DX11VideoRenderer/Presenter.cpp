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

#include "Presenter.h"
#include <stdio.h>
#include <io.h>
#include <fcntl.h>

using namespace DX11VideoRenderer;
#define DebugLog LOG_DEBUG

// Debug output helper - prints to both debug output and console (if available)
//static void DebugLog(const char* format, ...)
//{
//    char buffer[512];
//    va_list args;
//    va_start(args, format);
//    vsprintf_s(buffer, format, args);
//    va_end(args);
//    
//    // Always output to debug output (visible in VS Output window or DebugView)
//    OutputDebugStringA(buffer);
//    
//    // Try to write to console if attached
//    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
//    if (hStdOut != INVALID_HANDLE_VALUE && hStdOut != NULL)
//    {
//        DWORD written;
//        WriteConsoleA(hStdOut, buffer, (DWORD)strlen(buffer), &written, NULL);
//    }
//}

//-----------------------------------------------------------------------------
// Static Creation
//-----------------------------------------------------------------------------

HRESULT CPresenter::CreateInstance(CPresenter** ppPresenter, UINT gpuAdapterIndex)
{
    if (!ppPresenter)
    {
        return E_POINTER;
    }
    
    *ppPresenter = DBG_NEW CPresenter();
    if (!*ppPresenter)
    {
        return E_OUTOFMEMORY;
    }
    
    // Early initialize the D3D device and DXGI manager
    // This allows hardware decoding to work because the decoder can query for
    // the IMFDXGIDeviceManager during topology resolution, before SetVideoWindow is called.
    HRESULT hr = (*ppPresenter)->InitializeDeviceManager(gpuAdapterIndex);
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] CreateInstance: InitializeDeviceManager failed hr=0x%08X\n", hr);
        // Continue anyway - software fallback will work
    }
    
    // Constructor sets refcount to 1, so don't AddRef again
    return S_OK;
}

//-----------------------------------------------------------------------------
// CPresenter
//-----------------------------------------------------------------------------

CPresenter::CPresenter() :
    m_nRefCount(1),
    m_bShutdown(FALSE),
    m_pDisplayManager(nullptr),
    m_hwndVideo(nullptr),
    m_pVideoDevice(nullptr),
    m_pVideoProcessorEnum(nullptr),
    m_pVideoProcessor(nullptr),
    m_pStagingTexture(nullptr),
    m_stagingFormat(DXGI_FORMAT_NV12),
    m_bFullScreenState(FALSE),
    m_bCanProcessNextSample(TRUE),
    m_bDeviceChanged(FALSE),
    m_imageWidthInPixels(0),
    m_imageHeightInPixels(0),
    m_uiRealDisplayWidth(0),
    m_uiRealDisplayHeight(0)
{
    ZeroMemory(&m_displayRect, sizeof(m_displayRect));
    ZeroMemory(&m_rcSrcApp, sizeof(m_rcSrcApp));
    ZeroMemory(&m_rcDstApp, sizeof(m_rcDstApp));
}

CPresenter::~CPresenter()
{
    //DebugLog("[DX11Presenter] Destructor called\n");
    Shutdown();
    //DebugLog("[DX11Presenter] Destructor complete\n");
}

HRESULT CPresenter::InitializeDeviceManager(UINT gpuAdapterIndex)
{
    // NOTE: This is called during CreateInstance before any locks are involved
    // so we don't need to acquire a lock here
    
    //DebugLog("[DX11Presenter] InitializeDeviceManager called (GPU Adapter: %u)\n", gpuAdapterIndex);
    
    if (m_pDisplayManager)
    {
        DebugLog("[DX11Presenter] InitializeDeviceManager: DisplayManager already exists\n");
        return S_OK;
    }
    
    m_pDisplayManager = DBG_NEW CDisplayManager();
    if (!m_pDisplayManager)
    {
        DebugLog("[DX11Presenter] InitializeDeviceManager: failed to create CDisplayManager\n");
        return E_OUTOFMEMORY;
    }
    
    // Set GPU adapter before initializing device
    if (gpuAdapterIndex > 0)
    {
        HRESULT hrGPU = m_pDisplayManager->SetGPUAdapter(gpuAdapterIndex);
        if (FAILED(hrGPU))
        {
            DebugLog("[DX11Presenter] InitializeDeviceManager: SetGPUAdapter failed, continuing with default\n");
        }
    }
    
    HRESULT hr = m_pDisplayManager->InitializeDevice();
    //DebugLog("[DX11Presenter] InitializeDeviceManager: InitializeDevice hr=0x%08X\n", hr);
    
    return hr;
}
// IUnknown
ULONG CPresenter::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

HRESULT CPresenter::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }
    
    if (riid == IID_IUnknown)
    {
        *ppv = static_cast<IUnknown*>(static_cast<IMFVideoDisplayControl*>(this));
    }
    else if (riid == __uuidof(IMFVideoDisplayControl))
    {
        *ppv = static_cast<IMFVideoDisplayControl*>(this);
    }
    else if (riid == __uuidof(IMFGetService))
    {
        *ppv = static_cast<IMFGetService*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    
    AddRef();
    return S_OK;
}

ULONG CPresenter::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

// IMFVideoDisplayControl
HRESULT CPresenter::GetFullscreen(BOOL* pfFullscreen)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    
    if (!pfFullscreen) return E_POINTER;
    
    *pfFullscreen = m_bFullScreenState;
    return S_OK;
}

HRESULT CPresenter::GetVideoWindow(HWND* phwndVideo)
{
    CAutoLock lock(&m_critSec);
    
    if (!phwndVideo) return E_POINTER;
    
    *phwndVideo = m_hwndVideo;
    return S_OK;
}

HRESULT CPresenter::RepaintVideo()
{
    //printf("[CUSTOM_DX11Presenter] *** REPAINT ENTRY *** RepaintVideo() INVOKED\n");
    DebugLog("[CUSTOM_DX11Presenter] RepaintVideo called\n");
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) 
    {
        DebugLog("[CUSTOM_DX11Presenter] RepaintVideo: CheckShutdown failed: 0x%08X\n", hr);
        return hr;
    }
    
    // Get client rect for logging
    if (m_hwndVideo)
    {
        RECT rcClient = {};
        GetClientRect(m_hwndVideo, &rcClient);
        DebugLog("[CUSTOM_DX11Presenter] RepaintVideo: Client rect = %dx%d\n", rcClient.right, rcClient.bottom);
    }
    
    // Present the current frame again
    hr = PresentFrame();
    DebugLog("[CUSTOM_DX11Presenter] RepaintVideo: PresentFrame returned 0x%08X\n", hr);
    return hr;
}

HRESULT CPresenter::SetFullscreen(BOOL fFullscreen)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    
    m_bFullScreenState = fFullscreen;
    
    // Reset video processor when changing fullscreen state
    SafeRelease(m_pVideoProcessor);
    SafeRelease(m_pVideoProcessorEnum);
    SafeRelease(m_pVideoDevice);
    
    return S_OK;
}

HRESULT CPresenter::SetVideoWindow(HWND hwndVideo)
{
    CAutoLock lock(&m_critSec);
    
    DebugLog("[DX11Presenter] SetVideoWindow called, hwnd=0x%p\n", hwndVideo);
    
    if (!IsWindow(hwndVideo))
    {
        DebugLog("[DX11Presenter] SetVideoWindow: invalid window!\n");
        return E_INVALIDARG;
    }
    
    m_hwndVideo = hwndVideo;
    
    HRESULT hr = Initialize(hwndVideo);
    DebugLog("[DX11Presenter] SetVideoWindow: Initialize hr=0x%08X\n", hr);
    return hr;
}

// IMFGetService
HRESULT CPresenter::GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject)
{
    CAutoLock lock(&m_critSec);
    
    //DebugLog("[DX11Presenter] GetService called, guidService=%08X-%04X\n",
    //         guidService.Data1, guidService.Data2);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] GetService: shutdown, hr=0x%08X\n", hr);
        return hr;
    }
    
    if (!ppvObject) return E_POINTER;
    
    *ppvObject = nullptr;
    
    if (guidService == MR_VIDEO_ACCELERATION_SERVICE)
    {
        if (riid == __uuidof(IMFDXGIDeviceManager))
        {
            DebugLog("[DX11Presenter] GetService: MR_VIDEO_ACCELERATION_SERVICE for IMFDXGIDeviceManager\n");
            if (m_pDisplayManager)
            {
                IMFDXGIDeviceManager* pManager = m_pDisplayManager->GetDXGIDeviceManager();
                if (pManager)
                {
                    *ppvObject = pManager;
                    pManager->AddRef();
                    DebugLog("[DX11Presenter] GetService: returning DXGIDeviceManager=%p\n", pManager);
                    return S_OK;
                }
                DebugLog("[DX11Presenter] GetService: DisplayManager has no DXGIDeviceManager!\n");
            }
            else
            {
                DebugLog("[DX11Presenter] GetService: no DisplayManager (not initialized yet?)\n");
            }
            return E_NOINTERFACE;
        }
        DebugLog("[DX11Presenter] GetService: MR_VIDEO_ACCELERATION_SERVICE for unknown riid\n");
    }
    else if (guidService == MR_VIDEO_RENDER_SERVICE)
    {
        DebugLog("[DX11Presenter] GetService: MR_VIDEO_RENDER_SERVICE\n");
        return QueryInterface(riid, ppvObject);
    }
    
    DebugLog("[DX11Presenter] GetService: unsupported service\n");
    return MF_E_UNSUPPORTED_SERVICE;
}

// Presenter methods
HRESULT CPresenter::Initialize(HWND hwndVideo, UINT gpuAdapterIndex)
{
    CAutoLock lock(&m_critSec);
    
    DebugLog("[DX11Presenter] Initialize called, hwnd=0x%p, GPU Adapter=%u\n", hwndVideo, gpuAdapterIndex);
    
    // Clean up video processor resources (but keep the D3D device and DXGI manager)
    SafeRelease(m_pVideoProcessor);
    SafeRelease(m_pVideoProcessorEnum);
    SafeRelease(m_pVideoDevice);
    
    m_hwndVideo = hwndVideo;
    m_bShutdown = FALSE;
    
    // Create display manager if not already created (may have been created in InitializeDeviceManager)
    if (!m_pDisplayManager)
    {
        m_pDisplayManager = DBG_NEW CDisplayManager();
        if (!m_pDisplayManager)
        {
            DebugLog("[DX11Presenter] Initialize: failed to create CDisplayManager\n");
            return E_OUTOFMEMORY;
        }
        
        // Set GPU adapter if specified
        if (gpuAdapterIndex > 0)
        {
            HRESULT hrGPU = m_pDisplayManager->SetGPUAdapter(gpuAdapterIndex);
            if (FAILED(hrGPU))
            {
                DebugLog("[DX11Presenter] Initialize: SetGPUAdapter failed, continuing with default\n");
            }
        }
    }
    
    HRESULT hr = m_pDisplayManager->Initialize(hwndVideo);
    DebugLog("[DX11Presenter] Initialize: CDisplayManager::Initialize hr=0x%08X\n", hr);
    if (FAILED(hr))
    {
        SafeDelete(m_pDisplayManager);
        return hr;
    }
    
    DebugLog("[DX11Presenter] Initialize: SUCCESS\n");
    return S_OK;
}

HRESULT CPresenter::Shutdown()
{
    CAutoLock lock(&m_critSec);
    
    DebugLog("[DX11Presenter] Shutdown: starting\n");
    
    m_bShutdown = TRUE;
    
    // Flush the device context to ensure all pending work is done
    // before releasing video processor objects
    if (m_pDisplayManager)
    {
        ID3D11DeviceContext* pContext = m_pDisplayManager->GetD3D11DeviceContext();
        if (pContext)
        {
            pContext->ClearState();
            pContext->Flush();
        }
    }
    
    // Release video processor objects (must be done before device is released)
    SafeRelease(m_pVideoProcessor);
    SafeRelease(m_pVideoProcessorEnum);
    SafeRelease(m_pVideoDevice);
    SafeRelease(m_pStagingTexture);
    
    // Now shutdown the display manager (releases device)
    if (m_pDisplayManager)
    {
        m_pDisplayManager->Shutdown();
        SafeDelete(m_pDisplayManager);
    }
    
    DebugLog("[DX11Presenter] Shutdown: complete\n");
    
    return S_OK;
}

HRESULT CPresenter::CheckShutdown() const
{
    return m_bShutdown ? MF_E_SHUTDOWN : S_OK;
}

HRESULT CPresenter::Flush()
{
    CAutoLock lock(&m_critSec);
    
    m_bCanProcessNextSample = TRUE;
    return S_OK;
}

HRESULT CPresenter::GetMonitorRefreshRate(DWORD* pdwRefreshRate)
{
    if (!pdwRefreshRate) return E_POINTER;
    
    if (m_pDisplayManager)
    {
        *pdwRefreshRate = m_pDisplayManager->GetMonitorRefreshRate();
        return S_OK;
    }
    
    *pdwRefreshRate = 60;
    return S_OK;
}

HRESULT CPresenter::IsMediaTypeSupported(IMFMediaType* pMediaType, DXGI_FORMAT dxgiFormat)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    
    if (!pMediaType) return E_POINTER;
    
    // Get frame size
    UINT32 width = 0, height = 0;
    hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) return hr;
    
    // Check if we can create a video processor for this format
    if (!m_pDisplayManager || !m_pDisplayManager->GetD3D11Device())
    {
        return E_UNEXPECTED;
    }
    
    ID3D11Device* pDevice = m_pDisplayManager->GetD3D11Device();
    ID3D11VideoDevice* pVideoDevice = nullptr;
    
    hr = pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&pVideoDevice);
    if (FAILED(hr)) return hr;
    
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = width;
    contentDesc.InputHeight = height;
    contentDesc.OutputWidth = width;
    contentDesc.OutputHeight = height;
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    
    ID3D11VideoProcessorEnumerator* pEnum = nullptr;
    hr = pVideoDevice->CreateVideoProcessorEnumerator(&contentDesc, &pEnum);
    
    if (SUCCEEDED(hr))
    {
        UINT flags = 0;
        hr = pEnum->CheckVideoProcessorFormat(dxgiFormat, &flags);
        if (SUCCEEDED(hr) && !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT))
        {
            hr = MF_E_UNSUPPORTED_D3D_TYPE;
        }
        pEnum->Release();
    }
    
    pVideoDevice->Release();
    return hr;
}

HRESULT CPresenter::SetCurrentMediaType(IMFMediaType* pMediaType)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    
    if (!pMediaType) return E_POINTER;
    
    DebugLog("[DX11Presenter] SetCurrentMediaType called\n");
    
    // Get frame size
    hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m_imageWidthInPixels, &m_imageHeightInPixels);
    if (FAILED(hr)) return hr;
    
    DebugLog("[DX11Presenter] SetCurrentMediaType: frame size %dx%d\n", m_imageWidthInPixels, m_imageHeightInPixels);
    
    // Get subtype to determine DXGI format
    GUID subtype = GUID_NULL;
    hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr))
    {
        if (subtype == MFVideoFormat_NV12)
        {
            m_stagingFormat = DXGI_FORMAT_NV12;
            DebugLog("[DX11Presenter] SetCurrentMediaType: format NV12\n");
        }
        else if (subtype == MFVideoFormat_YUY2)
        {
            m_stagingFormat = DXGI_FORMAT_YUY2;
            DebugLog("[DX11Presenter] SetCurrentMediaType: format YUY2\n");
        }
        else if (subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_ARGB32)
        {
            m_stagingFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            DebugLog("[DX11Presenter] SetCurrentMediaType: format BGRA\n");
        }
        else
        {
            m_stagingFormat = DXGI_FORMAT_NV12; // Default
            DebugLog("[DX11Presenter] SetCurrentMediaType: unknown format, defaulting to NV12\n");
        }
    }
    
    // Get display area
    MFVideoArea videoArea = {};
    UINT32 cbBlobSize = 0;
    
    hr = pMediaType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&videoArea, sizeof(videoArea), &cbBlobSize);
    if (FAILED(hr))
    {
        hr = pMediaType->GetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&videoArea, sizeof(videoArea), &cbBlobSize);
    }
    
    if (FAILED(hr))
    {
        // Use full frame
        videoArea = MakeArea(0.0f, 0.0f, m_imageWidthInPixels, m_imageHeightInPixels);
        hr = S_OK;
    }
    
    m_displayRect = MFVideoAreaToRect(videoArea);
    
    // Get pixel aspect ratio
    UINT32 parX = 1, parY = 1;
    MFGetAttributeRatio(pMediaType, MF_MT_PIXEL_ASPECT_RATIO, &parX, &parY);
    
    // Calculate display dimensions
    m_uiRealDisplayWidth = MulDiv(m_displayRect.right - m_displayRect.left, parX, parY);
    m_uiRealDisplayHeight = m_displayRect.bottom - m_displayRect.top;
    
    DebugLog("[DX11Presenter] SetCurrentMediaType: display %dx%d\n", m_uiRealDisplayWidth, m_uiRealDisplayHeight);
    
    // Reset video processor and staging texture
    SafeRelease(m_pVideoProcessor);
    SafeRelease(m_pVideoProcessorEnum);
    SafeRelease(m_pVideoDevice);
    SafeRelease(m_pStagingTexture);
    
    return S_OK;
}

HRESULT CPresenter::CheckDeviceState(BOOL* pbDeviceChanged)
{
    if (!pbDeviceChanged) return E_POINTER;
    
    *pbDeviceChanged = m_bDeviceChanged;
    m_bDeviceChanged = FALSE;
    
    return S_OK;
}

HRESULT CPresenter::CreateVideoProcessor()
{
    DebugLog("[DX11Presenter] CreateVideoProcessor: starting\n");
    
    if (!m_pDisplayManager) 
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: no display manager\n");
        return E_UNEXPECTED;
    }
    
    ID3D11Device* pDevice = m_pDisplayManager->GetD3D11Device();
    if (!pDevice) 
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: no D3D11 device\n");
        return E_UNEXPECTED;
    }
    
    HRESULT hr = pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVideoDevice);
    if (FAILED(hr)) 
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: QueryInterface for ID3D11VideoDevice failed hr=0x%08X\n", hr);
        return hr;
    }
    
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = m_imageWidthInPixels;
    contentDesc.InputHeight = m_imageHeightInPixels;
    contentDesc.OutputWidth = m_imageWidthInPixels;
    contentDesc.OutputHeight = m_imageHeightInPixels;
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    
    DebugLog("[DX11Presenter] CreateVideoProcessor: content desc %dx%d\n", m_imageWidthInPixels, m_imageHeightInPixels);
    
    hr = m_pVideoDevice->CreateVideoProcessorEnumerator(&contentDesc, &m_pVideoProcessorEnum);
    if (FAILED(hr)) 
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: CreateVideoProcessorEnumerator failed hr=0x%08X\n", hr);
        return hr;
    }
    
    // Check if NV12 input format is supported
    DXGI_FORMAT inputFormat = m_stagingFormat;
    UINT formatFlags = 0;
    hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(inputFormat, &formatFlags);
    if (SUCCEEDED(hr))
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: input format %d flags=0x%X (input=%d output=%d)\n", 
                 inputFormat, formatFlags, 
                 (formatFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ? 1 : 0,
                 (formatFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) ? 1 : 0);
    }
    else
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: CheckVideoProcessorFormat failed hr=0x%08X\n", hr);
    }
    
    hr = m_pVideoDevice->CreateVideoProcessor(m_pVideoProcessorEnum, 0, &m_pVideoProcessor);
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: CreateVideoProcessor failed hr=0x%08X\n", hr);
    }
    else
    {
        DebugLog("[DX11Presenter] CreateVideoProcessor: success\n");
    }
    return hr;
}

HRESULT CPresenter::ProcessFrameEx(IMFMediaType* pCurrentType, IMFSample* pSample, UINT32* punInterlaceMode, BOOL* pbDeviceChanged, BOOL* pbProcessAgain, IMFSample** ppOutputSample)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    
    if (!pSample || !punInterlaceMode || !pbDeviceChanged || !pbProcessAgain)
    {
        return E_POINTER;
    }
    
    *pbProcessAgain = FALSE;
    *pbDeviceChanged = FALSE;
    
    // Get buffer from sample
    IMFMediaBuffer* pBuffer = nullptr;
    hr = pSample->GetBufferByIndex(0, &pBuffer);
    if (FAILED(hr)) return hr;
    
    // Get DXGI buffer
    IMFDXGIBuffer* pDXGIBuffer = nullptr;
    hr = pBuffer->QueryInterface(__uuidof(IMFDXGIBuffer), (void**)&pDXGIBuffer);
    pBuffer->Release();
    
    if (FAILED(hr)) return hr;
    
    // Get texture
    ID3D11Texture2D* pTexture = nullptr;
    hr = pDXGIBuffer->GetResource(__uuidof(ID3D11Texture2D), (void**)&pTexture);
    
    UINT dwViewIndex = 0;
    pDXGIBuffer->GetSubresourceIndex(&dwViewIndex);
    pDXGIBuffer->Release();
    
    if (FAILED(hr)) return hr;
    
    // Get destination rect
    RECT rcDest = {};
    GetClientRect(m_hwndVideo, &rcDest);
    
    if (IsRectEmpty(&rcDest))
    {
        pTexture->Release();
        return S_OK;
    }
    
    // Ensure swap chain exists and is correctly sized for the window
    IDXGISwapChain1* pSwapChain = m_pDisplayManager->GetSwapChain();
    if (!pSwapChain)
    {
        hr = m_pDisplayManager->CreateSwapChain(rcDest.right, rcDest.bottom);
        if (FAILED(hr))
        {
            pTexture->Release();
            return hr;
        }
    }
    else
    {
        // Check if swap chain needs to be resized
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        pSwapChain->GetDesc1(&swapChainDesc);
        UINT windowWidth = rcDest.right - rcDest.left;
        UINT windowHeight = rcDest.bottom - rcDest.top;
        
        if (swapChainDesc.Width != windowWidth || swapChainDesc.Height != windowHeight)
        {
            DebugLog("[CUSTOM_DX11Presenter] ProcessFrame: Resizing swap chain from %ux%u to %ux%u\n",
                swapChainDesc.Width, swapChainDesc.Height, windowWidth, windowHeight);
            hr = m_pDisplayManager->ResizeSwapChain(windowWidth, windowHeight);
            DebugLog("[CUSTOM_DX11Presenter] ProcessFrame: ResizeSwapChain returned 0x%08X\n", hr);
            // Continue even if resize fails - letterboxing should still work
        }
    }
    
    // Get interlace mode
    *punInterlaceMode = MFGetAttributeUINT32(pCurrentType, MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    
    // Process the frame
    hr = ProcessFrameUsingVideoProcessor(pTexture, dwViewIndex, rcDest, *punInterlaceMode);
    
    pTexture->Release();
    
    if (SUCCEEDED(hr))
    {
        m_bCanProcessNextSample = FALSE;
    }
    
    return hr;
}

HRESULT CPresenter::ProcessFrameUsingVideoProcessor(ID3D11Texture2D* pTexture, UINT dwViewIndex, RECT rcDest, UINT32 unInterlaceMode)
{
    HRESULT hr = S_OK;
    
    //DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: starting, dest=(%d,%d,%d,%d)\n", 
             //rcDest.left, rcDest.top, rcDest.right, rcDest.bottom);
    
    if (!m_pDisplayManager) 
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: no display manager\n");
        return E_UNEXPECTED;
    }
    
    // Create video processor if needed
    if (!m_pVideoProcessor)
    {
        //DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: creating video processor\n");
        hr = CreateVideoProcessor();
        if (FAILED(hr)) 
        {
            DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: CreateVideoProcessor failed hr=0x%08X\n", hr);
            return hr;
        }
    }
    
    // Get video context
    ID3D11DeviceContext* pContext = m_pDisplayManager->GetD3D11DeviceContext();
    if (!pContext) 
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: no device context\n");
        return E_UNEXPECTED;
    }
    
    ID3D11VideoContext* pVideoContext = nullptr;
    hr = pContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&pVideoContext);
    if (FAILED(hr)) 
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: QueryInterface for ID3D11VideoContext failed hr=0x%08X\n", hr);
        return hr;
    }
    
    // Resize swap chain if needed
    IDXGISwapChain1* pSwapChain = m_pDisplayManager->GetSwapChain();
    if (!pSwapChain)
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: no swap chain\n");
        pVideoContext->Release();
        return E_UNEXPECTED;
    }
    
    // Update destination rect
    m_rcDstApp = rcDest;
    
    // Get back buffer
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: GetBuffer failed hr=0x%08X\n", hr);
        pVideoContext->Release();
        return hr;
    }
    
    // Log texture details before creating input view
    D3D11_TEXTURE2D_DESC texDesc = {};
    pTexture->GetDesc(&texDesc);
    //DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: input texture %dx%d format=%d usage=%d bind=%d\n",
    //         texDesc.Width, texDesc.Height, texDesc.Format, texDesc.Usage, texDesc.BindFlags);
    
    // Create input view
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
    inputViewDesc.FourCC = 0;
    inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputViewDesc.Texture2D.MipSlice = 0;
    inputViewDesc.Texture2D.ArraySlice = dwViewIndex;
    
    ID3D11VideoProcessorInputView* pInputView = nullptr;
    hr = m_pVideoDevice->CreateVideoProcessorInputView(pTexture, m_pVideoProcessorEnum, &inputViewDesc, &pInputView);
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: CreateVideoProcessorInputView failed hr=0x%08X dwViewIndex=%u\n", hr, dwViewIndex);
        pBackBuffer->Release();
        pVideoContext->Release();
        return hr;
    }
    
    // Create output view
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {};
    outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputViewDesc.Texture2D.MipSlice = 0;
    
    ID3D11VideoProcessorOutputView* pOutputView = nullptr;
    hr = m_pVideoDevice->CreateVideoProcessorOutputView(pBackBuffer, m_pVideoProcessorEnum, &outputViewDesc, &pOutputView);
    pBackBuffer->Release();
    
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: CreateVideoProcessorOutputView failed hr=0x%08X\n", hr);
        pInputView->Release();
        pVideoContext->Release();
        return hr;
    }
    
    //DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: input/output views created\n");
    
    // Set video context parameters
    m_rcSrcApp.left = 0;
    m_rcSrcApp.top = 0;
    m_rcSrcApp.right = m_uiRealDisplayWidth;
    m_rcSrcApp.bottom = m_uiRealDisplayHeight;
    
    RECT srcRect = m_rcSrcApp;
    RECT dstRect = m_rcDstApp;
    UpdateRectangles(&dstRect, &srcRect);
    
    //DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: src=(%d,%d,%d,%d) dst=(%d,%d,%d,%d)\n",
    //         srcRect.left, srcRect.top, srcRect.right, srcRect.bottom,
    //         dstRect.left, dstRect.top, dstRect.right, dstRect.bottom);
    
    SetVideoContextParameters(pVideoContext, &srcRect, &dstRect, unInterlaceMode);
    
    // Process
    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.PastFrames = 0;
    stream.FutureFrames = 0;
    stream.pInputSurface = pInputView;
    
    hr = pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, 0, 1, &stream);
    //DebugLog("[DX11Presenter] ProcessFrameUsingVideoProcessor: VideoProcessorBlt hr=0x%08X\n", hr);
    
    pOutputView->Release();
    pInputView->Release();
    pVideoContext->Release();
    
    return hr;
}

void CPresenter::SetVideoContextParameters(ID3D11VideoContext* pVideoContext, const RECT* pSrcRect, const RECT* pDstRect, UINT32 unInterlaceMode)
{
    // Set frame format
    D3D11_VIDEO_FRAME_FORMAT frameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    if (unInterlaceMode == MFVideoInterlace_FieldInterleavedUpperFirst || unInterlaceMode == MFVideoInterlace_FieldSingleUpper)
    {
        frameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
    }
    else if (unInterlaceMode == MFVideoInterlace_FieldInterleavedLowerFirst || unInterlaceMode == MFVideoInterlace_FieldSingleLower)
    {
        frameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST;
    }
    
    pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, frameFormat);
    pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);
    pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, TRUE, pSrcRect);
    pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, TRUE, pDstRect);
    pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, TRUE, &m_rcDstApp);
    
    // Color space
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
    colorSpace.YCbCr_xvYCC = 1;
    pVideoContext->VideoProcessorSetStreamColorSpace(m_pVideoProcessor, 0, &colorSpace);
    pVideoContext->VideoProcessorSetOutputColorSpace(m_pVideoProcessor, &colorSpace);
    
    // Background color
    D3D11_VIDEO_COLOR bgColor = {};
    bgColor.RGBA.A = 1.0f;
    bgColor.RGBA.R = 0.0f;
    bgColor.RGBA.G = 0.0f;
    bgColor.RGBA.B = 0.0f;
    pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &bgColor);
}

HRESULT CPresenter::PresentFrame()
{
    CAutoLock lock(&m_critSec);
    
    //DebugLog("[CUSTOM_DX11Presenter] PresentFrame called\n");
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) 
    {
        DebugLog("[CUSTOM_DX11Presenter] PresentFrame: shutdown, hr=0x%08X\n", hr);
        return hr;
    }
    
    if (m_pDisplayManager)
    {
        IDXGISwapChain1* pSwapChain = m_pDisplayManager->GetSwapChain();
        //DebugLog("[CUSTOM_DX11Presenter] PresentFrame: swap chain = %p\n", pSwapChain);
        
        hr = m_pDisplayManager->Present();
        //DebugLog("[CUSTOM_DX11Presenter] PresentFrame: Present hr=0x%08X\n", hr);
        if (SUCCEEDED(hr))
        {
            m_bCanProcessNextSample = TRUE;
        }
    }
    else
    {
        DebugLog("[CUSTOM_DX11Presenter] PresentFrame: no DisplayManager!\n");
    }
    
    return hr;
}

void CPresenter::UpdateRectangles(RECT* pDst, RECT* pSrc)
{
    // Map source rect to display rect
    pSrc->left = m_displayRect.left + MulDiv(pSrc->left, m_displayRect.right - m_displayRect.left, m_uiRealDisplayWidth);
    pSrc->right = m_displayRect.left + MulDiv(pSrc->right, m_displayRect.right - m_displayRect.left, m_uiRealDisplayWidth);
    pSrc->top = m_displayRect.top + MulDiv(pSrc->top, m_displayRect.bottom - m_displayRect.top, m_uiRealDisplayHeight);
    pSrc->bottom = m_displayRect.top + MulDiv(pSrc->bottom, m_displayRect.bottom - m_displayRect.top, m_uiRealDisplayHeight);
    
    // Letterbox
    RECT srcRect = *pSrc;
    srcRect.left = 0;
    srcRect.top = 0;
    srcRect.right = m_uiRealDisplayWidth;
    srcRect.bottom = m_uiRealDisplayHeight;
    
    LetterBoxDstRect(pDst, srcRect, m_rcDstApp);
}

void CPresenter::LetterBoxDstRect(LPRECT lprcLBDst, const RECT& rcSrc, const RECT& rcDst)
{
    int srcWidth = rcSrc.right - rcSrc.left;
    int srcHeight = rcSrc.bottom - rcSrc.top;
    int dstWidth = rcDst.right - rcDst.left;
    int dstHeight = rcDst.bottom - rcDst.top;
    
    int lbWidth, lbHeight;
    
    if (MulDiv(srcWidth, dstHeight, srcHeight) <= dstWidth)
    {
        // Column letterboxing
        lbWidth = MulDiv(dstHeight, srcWidth, srcHeight);
        lbHeight = dstHeight;
    }
    else
    {
        // Row letterboxing
        lbWidth = dstWidth;
        lbHeight = MulDiv(dstWidth, srcHeight, srcWidth);
    }
    
    lprcLBDst->left = rcDst.left + (dstWidth - lbWidth) / 2;
    lprcLBDst->right = lprcLBDst->left + lbWidth;
    lprcLBDst->top = rcDst.top + (dstHeight - lbHeight) / 2;
    lprcLBDst->bottom = lprcLBDst->top + lbHeight;
}

HRESULT CPresenter::SetMediaType(IMFMediaType* pMediaType)
{
    return SetCurrentMediaType(pMediaType);
}

HRESULT CPresenter::GetVideoSize(UINT32* pWidth, UINT32* pHeight)
{
    CAutoLock lock(&m_critSec);
    
    if (!pWidth || !pHeight)
    {
        return E_POINTER;
    }
    
    *pWidth = m_imageWidthInPixels;
    *pHeight = m_imageHeightInPixels;
    
    return S_OK;
}

HRESULT CPresenter::SetDestinationRect(const RECT& rcDest)
{
    CAutoLock lock(&m_critSec);
    
    m_rcDstApp = rcDest;
    m_displayRect = rcDest;
    
    return S_OK;
}

HRESULT CPresenter::ProcessFrame(IMFSample* pSample)
{
    CAutoLock lock(&m_critSec);
    
    //DebugLog("[DX11Presenter] ProcessFrame called\n");
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        DebugLog("[DX11Presenter] ProcessFrame: shutdown, hr=0x%08X\n", hr);
        return hr;
    }
    
    if (!pSample)
    {
        DebugLog("[DX11Presenter] ProcessFrame: null sample\n");
        return E_POINTER;
    }
    
    // Get the buffer from the sample
    IMFMediaBuffer* pBuffer = nullptr;
    hr = pSample->GetBufferByIndex(0, &pBuffer);
    //DebugLog("[DX11Presenter] ProcessFrame: GetBufferByIndex hr=0x%08X\n", hr);
    
    if (SUCCEEDED(hr))
    {
        // Check if it's a DXGI buffer
        IMFDXGIBuffer* pDXGIBuffer = nullptr;
        hr = pBuffer->QueryInterface(IID_PPV_ARGS(&pDXGIBuffer));
        //DebugLog("[DX11Presenter] ProcessFrame: QueryInterface IMFDXGIBuffer hr=0x%08X\n", hr);
        
        if (SUCCEEDED(hr))
        {
            ID3D11Texture2D* pTexture = nullptr;
            UINT subresource = 0;
            
            hr = pDXGIBuffer->GetResource(IID_PPV_ARGS(&pTexture));
            //DebugLog("[DX11Presenter] ProcessFrame: GetResource hr=0x%08X\n", hr);
            if (SUCCEEDED(hr))
            {
                pDXGIBuffer->GetSubresourceIndex(&subresource);
                
                // Get the actual window client rect for proper aspect ratio handling
                RECT rcDest = {};
                if (m_hwndVideo)
                {
                    GetClientRect(m_hwndVideo, &rcDest);
                }
                
                // If we still don't have a valid rect, use stored destination or video dimensions
                if (IsRectEmpty(&rcDest))
                {
                    rcDest = m_rcDstApp;
                    if (IsRectEmpty(&rcDest))
                    {
                        rcDest.left = 0;
                        rcDest.top = 0;
                        rcDest.right = m_uiRealDisplayWidth > 0 ? m_uiRealDisplayWidth : m_imageWidthInPixels;
                        rcDest.bottom = m_uiRealDisplayHeight > 0 ? m_uiRealDisplayHeight : m_imageHeightInPixels;
                    }
                }
                
                //DebugLog("[DX11Presenter] ProcessFrame: HARDWARE DXGI buffer, window rect=(%d,%d,%d,%d)\n", 
                    //rcDest.left, rcDest.top, rcDest.right, rcDest.bottom);
                
                // Ensure swap chain exists and is correctly sized for the window
                if (m_pDisplayManager)
                {
                    IDXGISwapChain1* pSwapChain = m_pDisplayManager->GetSwapChain();
                    if (!pSwapChain)
                    {
                        //DebugLog("[DX11Presenter] ProcessFrame: Creating swap chain %dx%d\n", rcDest.right, rcDest.bottom);
                        hr = m_pDisplayManager->CreateSwapChain(rcDest.right, rcDest.bottom);
                        if (FAILED(hr))
                        {
                            DebugLog("[DX11Presenter] ProcessFrame: CreateSwapChain failed hr=0x%08X\n", hr);
                            SafeRelease(pTexture);
                            SafeRelease(pDXGIBuffer);
                            SafeRelease(pBuffer);
                            return hr;
                        }
                    }
                    else
                    {
                        // Check if swap chain needs to be resized
                        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
                        pSwapChain->GetDesc1(&swapChainDesc);
                        UINT windowWidth = rcDest.right - rcDest.left;
                        UINT windowHeight = rcDest.bottom - rcDest.top;
                        
                        if (swapChainDesc.Width != windowWidth || swapChainDesc.Height != windowHeight)
                        {
                            //DebugLog("[DX11Presenter] ProcessFrame: Resizing swap chain from %dx%d to %dx%d\n", 
                            //    swapChainDesc.Width, swapChainDesc.Height, windowWidth, windowHeight);
                            hr = m_pDisplayManager->ResizeSwapChain(windowWidth, windowHeight);
                            if (FAILED(hr))
                            {
                                DebugLog("[DX11Presenter] ProcessFrame: ResizeSwapChain failed hr=0x%08X\n", hr);
                                // Continue anyway - letterboxing should still work
                            }
                        }
                    }
                }
                
                // Store destination rect for UpdateRectangles/LetterBoxDstRect to use
                m_rcDstApp = rcDest;
                
                //DebugLog("[DX11Presenter] ProcessFrame: calling ProcessFrameUsingVideoProcessor, dest=(%d,%d,%d,%d)\n", 
                //    rcDest.left, rcDest.top, rcDest.right, rcDest.bottom);
                hr = ProcessFrameUsingVideoProcessor(pTexture, subresource, rcDest, 0);
                //DebugLog("[DX11Presenter] ProcessFrame: ProcessFrameUsingVideoProcessor hr=0x%08X\n", hr);
                
                SafeRelease(pTexture);
            }
            
            SafeRelease(pDXGIBuffer);
        }
        else
        {
            // Software buffer - need to copy to a D3D11 texture
            DebugLog("[DX11Presenter] ProcessFrame: SOFTWARE BUFFER - copying to staging texture\n");
            hr = ProcessSoftwareBuffer(pBuffer);
            //DebugLog("[DX11Presenter] ProcessFrame: ProcessSoftwareBuffer hr=0x%08X\n", hr);
        }
        
        SafeRelease(pBuffer);
    }
    
    return hr;
}

HRESULT CPresenter::CreateStagingTexture(UINT width, UINT height, DXGI_FORMAT format)
{
    if (!m_pDisplayManager) return E_UNEXPECTED;
    
    ID3D11Device* pDevice = m_pDisplayManager->GetD3D11Device();
    if (!pDevice) return E_UNEXPECTED;
    
    // Release existing staging texture if format/size changed
    if (m_pStagingTexture)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        m_pStagingTexture->GetDesc(&desc);
        if (desc.Width != width || desc.Height != height || desc.Format != format)
        {
            SafeRelease(m_pStagingTexture);
        }
    }
    
    if (!m_pStagingTexture)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        // For video processor input, we should NOT use D3D11_BIND_SHADER_RESOURCE with NV12
        // as it's not compatible on most hardware. Use 0 for bind flags.
        desc.BindFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        
        //DebugLog("[DX11Presenter] CreateStagingTexture: creating %dx%d format=%d\n", width, height, format);
        
        HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &m_pStagingTexture);
        if (FAILED(hr))
        {
            DebugLog("[DX11Presenter] CreateStagingTexture: CreateTexture2D failed hr=0x%08X\n", hr);
            return hr;
        }
        
        m_stagingFormat = format;
        //DebugLog("[DX11Presenter] CreateStagingTexture: created successfully\n");
    }
    
    return S_OK;
}

HRESULT CPresenter::ProcessSoftwareBuffer(IMFMediaBuffer* pBuffer)
{
    HRESULT hr = S_OK;
    
    if (!m_pDisplayManager) return E_UNEXPECTED;
    if (!pBuffer) return E_POINTER;
    
    // Get the 2D buffer interface for stride information
    IMF2DBuffer* p2DBuffer = nullptr;
    IMF2DBuffer2* p2DBuffer2 = nullptr;
    BYTE* pScanline0 = nullptr;
    LONG lPitch = 0;
    BOOL bLocked = FALSE;
    
    // Try IMF2DBuffer2 first (more flexible), then IMF2DBuffer
    hr = pBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer2));
    if (SUCCEEDED(hr))
    {
        BYTE* pBufferStart = nullptr;
        DWORD cbBufferLength = 0;
        hr = p2DBuffer2->Lock2DSize(MF2DBuffer_LockFlags_Read, &pScanline0, &lPitch, &pBufferStart, &cbBufferLength);
        if (SUCCEEDED(hr)) bLocked = TRUE;
    }
    else
    {
        hr = pBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
        if (SUCCEEDED(hr))
        {
            hr = p2DBuffer->Lock2D(&pScanline0, &lPitch);
            if (SUCCEEDED(hr)) bLocked = TRUE;
        }
    }
    
    if (!bLocked)
    {
        // Fall back to regular buffer lock
        BYTE* pData = nullptr;
        DWORD cbMaxLength = 0;
        DWORD cbCurrentLength = 0;
        hr = pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength);
        if (SUCCEEDED(hr))
        {
            pScanline0 = pData;
            lPitch = m_imageWidthInPixels; // Assume packed format
            bLocked = TRUE;
        }
    }
    
    if (!bLocked || !pScanline0)
    {
        DebugLog("[DX11Presenter] ProcessSoftwareBuffer: failed to lock buffer\n");
        SafeRelease(p2DBuffer);
        SafeRelease(p2DBuffer2);
        return hr;
    }
    
    DebugLog("[DX11Presenter] ProcessSoftwareBuffer: locked buffer, pitch=%d\n", lPitch);
    
    // Create staging texture if needed (using stored format from media type)
    hr = CreateStagingTexture(m_imageWidthInPixels, m_imageHeightInPixels, m_stagingFormat);
    if (FAILED(hr))
    {
        // Unlock and return
        if (p2DBuffer2) p2DBuffer2->Unlock2D();
        else if (p2DBuffer) p2DBuffer->Unlock2D();
        else pBuffer->Unlock();
        SafeRelease(p2DBuffer);
        SafeRelease(p2DBuffer2);
        return hr;
    }
    
    // Copy data to staging texture
    ID3D11DeviceContext* pContext = m_pDisplayManager->GetD3D11DeviceContext();
    if (pContext && m_pStagingTexture)
    {
        // For NV12, we need to handle Y and UV planes separately
        // Y plane: full resolution
        // UV plane: half resolution, interleaved
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        
        // Use UpdateSubresource for default usage texture
        UINT rowPitch = m_imageWidthInPixels;
        UINT depthPitch = m_imageWidthInPixels * m_imageHeightInPixels * 3 / 2; // NV12 size
        
        pContext->UpdateSubresource(m_pStagingTexture, 0, nullptr, pScanline0, rowPitch, depthPitch);
        
        //DebugLog("[DX11Presenter] ProcessSoftwareBuffer: copied to staging texture\n");
        
        // Process using video processor
        RECT rcDest = m_rcDstApp;
        if (IsRectEmpty(&rcDest))
        {
            GetClientRect(m_hwndVideo, &rcDest);
        }
        
        // Ensure swap chain exists before processing
        if (!m_pDisplayManager->GetSwapChain())
        {
            //DebugLog("[DX11Presenter] ProcessSoftwareBuffer: creating swap chain %dx%d\n", rcDest.right, rcDest.bottom);
            hr = m_pDisplayManager->CreateSwapChain(rcDest.right, rcDest.bottom);
            if (FAILED(hr))
            {
                DebugLog("[DX11Presenter] ProcessSoftwareBuffer: CreateSwapChain failed hr=0x%08X\n", hr);
                // Unlock and return
                if (p2DBuffer2) p2DBuffer2->Unlock2D();
                else if (p2DBuffer) p2DBuffer->Unlock2D();
                else pBuffer->Unlock();
                SafeRelease(p2DBuffer);
                SafeRelease(p2DBuffer2);
                return hr;
            }
        }
        
        hr = ProcessFrameUsingVideoProcessor(m_pStagingTexture, 0, rcDest, 0);
        //DebugLog("[DX11Presenter] ProcessSoftwareBuffer: ProcessFrameUsingVideoProcessor hr=0x%08X\n", hr);
    }
    
    // Unlock buffer
    if (p2DBuffer2)
    {
        p2DBuffer2->Unlock2D();
    }
    else if (p2DBuffer)
    {
        p2DBuffer->Unlock2D();
    }
    else
    {
        pBuffer->Unlock();
    }
    
    SafeRelease(p2DBuffer);
    SafeRelease(p2DBuffer2);
    
    if (SUCCEEDED(hr))
    {
        m_bCanProcessNextSample = FALSE;
    }
    
    return hr;
}



