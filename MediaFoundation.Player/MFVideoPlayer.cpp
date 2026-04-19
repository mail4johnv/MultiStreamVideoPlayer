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

#include "MFVideoPlayer.h"
#include "ManagedLogger.h"
#include <shlwapi.h>
#include <assert.h>
#include <mfreadwrite.h>
#include <msclr/marshal_cppstd.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

class InitializeConsole
{
public:
    InitializeConsole()
    {
        freopen("CONIN$", "rb", stdin);
        freopen("CONOUT$", "wb", stdout);
        freopen("CONOUT$", "wb", stderr);
    }
};
InitializeConsole consoleInitialize;
// DX11 Video Renderer

// Renderer selection:
// USE_DX11_RENDERER = true: Use our custom DX11 renderer (static lib)
// USE_DX11_RENDERER = false: Use Windows EVR (Enhanced Video Renderer)
// USE_MS_DX11_RENDERER_DLL = true: Use Microsoft sample DX11VideoRenderer DLL for testing
#define USE_DX11_RENDERER true
#define USE_MS_DX11_RENDERER_DLL false
#if USE_DX11_RENDERER
#include "..\DX11VideoRenderer\Activate.h"
#include "..\DX11VideoRenderer\Common.h"
#pragma comment(lib, "DX11VideoRenderer.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#elif USE_MS_DX11_RENDERER_DLL
#include "..\DX11VideoRenderer_in\Activate.h"
#endif
// If using MS DLL, we need to load it dynamically
#if USE_MS_DX11_RENDERER_DLL

// Global handle to the MS DLL
static HMODULE g_hMSDx11RendererDll = nullptr;

namespace {
    using namespace System;
    using namespace System::Runtime::InteropServices;
    
// CLSID from Microsoft sample DX11VideoRenderer.h
// {0743FA5C-DA9E-4760-8187-CCAC3DC15D77}
static const GUID CLSID_MSDx11VideoRendererActivate = 
    { 0x743fa5c, 0xda9e, 0x4760, { 0x81, 0x87, 0xcc, 0xac, 0x3d, 0xc1, 0x5d, 0x77 } };

// Function pointer type for CreateDX11VideoRendererActivate
typedef HRESULT(STDAPICALLTYPE* PFN_CreateDX11VideoRendererActivate)(HWND hwnd, IMFActivate** ppActivate);

// Global handle to the MS DLL
static PFN_CreateDX11VideoRendererActivate g_pfnCreateMSActivate = nullptr;

// Helper function to load MS DX11VideoRenderer DLL
static HRESULT LoadMSDx11RendererDll()
{
    // Initialize logger on first call (thread-safe, idempotent)
    static bool loggerInitialized = false;
    if (!loggerInitialized)
    {
        InitializeLogging("debug.log");
        loggerInitialized = true;
        ManagedLogger::Log("[MFVideoPlayer] Logger initialized to debug.log");
    }

    if (g_hMSDx11RendererDll != nullptr)
    {
        return S_OK; // Already loaded
    }

    // Try to load from the application directory
    g_hMSDx11RendererDll = LoadLibraryW(L"MSDx11VideoRenderer.dll");
    if (g_hMSDx11RendererDll == nullptr)
    {
        DWORD err = GetLastError();
        ManagedLogger::Log(String::Format("[MFVideoPlayer] Failed to load MSDx11VideoRenderer.dll, error=0x{0:X}", err));
        return HRESULT_FROM_WIN32(err);
    }

    ManagedLogger::Log("[MFVideoPlayer] Loaded MSDx11VideoRenderer.dll successfully");

    // Get the CreateDX11VideoRendererActivate function
    g_pfnCreateMSActivate = (PFN_CreateDX11VideoRendererActivate)GetProcAddress(
        g_hMSDx11RendererDll, "CreateDX11VideoRendererActivate");
    
    if (g_pfnCreateMSActivate == nullptr)
    {
        DWORD err = GetLastError();
        ManagedLogger::Log(String::Format("[MFVideoPlayer] Failed to get CreateDX11VideoRendererActivate, error=0x{0:X}", err));
        FreeLibrary(g_hMSDx11RendererDll);
        g_hMSDx11RendererDll = nullptr;
        return HRESULT_FROM_WIN32(err);
    }

    ManagedLogger::Log("[MFVideoPlayer] Got CreateDX11VideoRendererActivate function pointer");
    return S_OK;
}

// Helper function to create MS activate using the DLL
static HRESULT CreateMSDX11VideoRendererActivate(HWND hwnd, IMFActivate** ppActivate)
{
    HRESULT hr = LoadMSDx11RendererDll();
    if (FAILED(hr))
    {
        return hr;
    }

    // Call the function directly
    hr = g_pfnCreateMSActivate(hwnd, ppActivate);
    ManagedLogger::Log(String::Format("[MFVideoPlayer] CreateDX11VideoRendererActivate (MS DLL) result: 0x{0:X}", hr));
    return hr;
}

// Cleanup function - call this when done
static void UnloadMSDx11RendererDll()
{
    if (g_hMSDx11RendererDll != nullptr)
    {
        FreeLibrary(g_hMSDx11RendererDll);
        g_hMSDx11RendererDll = nullptr;
        g_pfnCreateMSActivate = nullptr;
        ManagedLogger::Log("[MFVideoPlayer] Unloaded MSDx11VideoRenderer.dll");
    }
}

} // end anonymous namespace

#endif // USE_MS_DX11_RENDERER_DLL

namespace MediaFoundation {
namespace Player {

using DX11VideoRenderer::SafeRelease;

// Constructor - implementation
VideoPlayer::VideoPlayer() :
    m_pSession(nullptr),
    m_pSource(nullptr),
    m_pVideoDisplay(nullptr),
    m_pPresentationDescriptor(nullptr),
    m_pTopology(nullptr),
    m_pCallback(nullptr),
    m_state(PlayerState::Closed),
    m_hwndVideo(nullptr),
    m_hwndEvent(nullptr),
    m_nrcEventCookie(0),
    m_gpuAdapterIndex(0),
    m_sharpenStrength(0.0),
    m_sharpenThreshold(0.0),
    m_colorBrightness(0),
    m_colorContrast(0),
    m_colorHue(0),
    m_colorSaturation(0),
    m_pCritSec(new CRITICAL_SECTION()),
    m_initialized(false)
{
    InitializeCriticalSection(m_pCritSec);
    m_pCallback = new CPlayerCallback(this);
    
#ifdef _DEBUG
    // Enable CRT debug heap leak reporting for debug builds
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_ALLOC_MEM_DF;      // Track allocations
    flags |= _CRTDBG_LEAK_CHECK_DF;     // Auto-report leaks at process exit
    _CrtSetDbgFlag(flags);

    // Optional: support env var CRT_BREAK_ALLOC="n1,n2" to break on specific alloc ids
    wchar_t buf[256] = {0};
    DWORD cch = GetEnvironmentVariableW(L"CRT_BREAK_ALLOC", buf, ARRAYSIZE(buf));
    if (cch > 0 && cch < ARRAYSIZE(buf))
    {
        wchar_t* ctx = nullptr;
        wchar_t* tok = wcstok_s(buf, L",; ", &ctx);
        while (tok)
        {
            long id = _wtol(tok);
            if (id > 0)
            {
                _CrtSetBreakAlloc(id);
            }
            tok = wcstok_s(nullptr, L",; ", &ctx);
        }
    }
#endif

    // Initialize Media Foundation
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr)) {
        m_initialized = true;
    }
}

// Destructor
VideoPlayer::~VideoPlayer() {
    this->!VideoPlayer();
}

// Finalizer
VideoPlayer::!VideoPlayer() {
    Shutdown();
    
    if (m_pCallback) {
        m_pCallback->Release();
        m_pCallback = nullptr;
    }
    
    if (m_initialized) {
        MFShutdown();
        m_initialized = false;
    }
    
#if USE_MS_DX11_RENDERER_DLL
    // Unload the MS DX11 renderer DLL if it was loaded
    UnloadMSDx11RendererDll();
#endif
    
    if (m_pCritSec) {
        DeleteCriticalSection(m_pCritSec);
        delete m_pCritSec;
        m_pCritSec = nullptr;
    }
}

// Set video window
void VideoPlayer::VideoWindow::set(IntPtr hwnd) {
    m_hwndVideo = static_cast<HWND>(hwnd.ToPointer());
}

// Get duration
TimeSpan VideoPlayer::Duration::get() {
    if (m_pPresentationDescriptor == nullptr) {
        return TimeSpan::Zero;
    }
    
    UINT64 duration = 0;
    HRESULT hr = m_pPresentationDescriptor->GetUINT64(MF_PD_DURATION, &duration);
    
    if (SUCCEEDED(hr)) {
        // Duration is in 100-nanosecond units
        return TimeSpan::FromTicks(duration);
    }
    
    return TimeSpan::Zero;
}

// Get position
TimeSpan VideoPlayer::Position::get() {
    if (m_pSession == nullptr) {
        return TimeSpan::Zero;
    }
    
    IMFClock* pClock = nullptr;
    IMFPresentationClock* pPresentationClock = nullptr;
    HRESULT hr = m_pSession->GetClock(&pClock);
    
    if (SUCCEEDED(hr)) {
        hr = pClock->QueryInterface(IID_PPV_ARGS(&pPresentationClock));
        pClock->Release();
    }
    
    if (SUCCEEDED(hr) && pPresentationClock) {
        MFTIME time = 0;
        hr = pPresentationClock->GetTime(&time);
        pPresentationClock->Release();
        
        if (SUCCEEDED(hr)) {
            return TimeSpan::FromTicks(time);
        }
    }
    
    return TimeSpan::Zero;
}

// Set position (seek)
void VideoPlayer::Position::set(TimeSpan value) {
    if (m_pSession == nullptr) return;
    
    PROPVARIANT varStart;
    PropVariantInit(&varStart);
    
    varStart.vt = VT_I8;
    varStart.hVal.QuadPart = value.Ticks;
    
    m_pSession->Start(&GUID_NULL, &varStart);
    PropVariantClear(&varStart);
}

// Get volume
double VideoPlayer::Volume::get() {
    if (m_pSession == nullptr) return 0.5;
    
    IMFSimpleAudioVolume* pVolume = nullptr;
    HRESULT hr = MFGetService(m_pSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&pVolume));
    
    if (SUCCEEDED(hr)) {
        float level = 0;
        hr = pVolume->GetMasterVolume(&level);
        pVolume->Release();
        
        if (SUCCEEDED(hr)) {
            return level;
        }
    }
    
    return 0.5;
}

// Set volume
void VideoPlayer::Volume::set(double value) {
    if (m_pSession == nullptr) return;
    
    float volume = static_cast<float>(Math::Clamp(value, 0.0, 1.0));
    
    IMFSimpleAudioVolume* pVolume = nullptr;
    HRESULT hr = MFGetService(m_pSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&pVolume));
    
    if (SUCCEEDED(hr)) {
        // Set volume for all channels by using IMFSimpleAudioVolume
        hr = pVolume->SetMasterVolume(volume);
        pVolume->Release();
    }
}

// Get muted
bool VideoPlayer::IsMuted::get() {
    if (m_pSession == nullptr) return false;
    
    IMFSimpleAudioVolume* pVolume = nullptr;
    HRESULT hr = MFGetService(m_pSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&pVolume));
    
    if (SUCCEEDED(hr)) {
        BOOL muted = FALSE;
        hr = pVolume->GetMute(&muted);
        pVolume->Release();
        
        if (SUCCEEDED(hr)) {
            return muted == TRUE;
        }
    }
    
    return false;
}

// Set muted
void VideoPlayer::IsMuted::set(bool value) {
    if (m_pSession == nullptr) return;
    
    IMFSimpleAudioVolume* pVolume = nullptr;
    HRESULT hr = MFGetService(m_pSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&pVolume));
    
    if (SUCCEEDED(hr)) {
        pVolume->SetMute(value ? TRUE : FALSE);
        pVolume->Release();
    }
}

void VideoPlayer::SharpenStrength::set(double value)
{
    double clamped = Math::Clamp(value, 0.0, 1.0);
    m_sharpenStrength = clamped;

    if (m_pVideoDisplay != nullptr)
    {
        // Encode strength (low 16) and threshold (high 16) as milli-units.
        DWORD sharpenMilli = static_cast<DWORD>(clamped * 1000.0 + 0.5);
        DWORD thresholdMilli = static_cast<DWORD>(Math::Clamp(m_sharpenThreshold / 0.02, 0.0, 1.0) * 1000.0 + 0.5);
        DWORD renderPrefs = (sharpenMilli & 0xFFFFu) | ((thresholdMilli & 0xFFFFu) << 16);
        ManagedLogger::Log(String::Format("[MFVideoPlayer] SharpenStrength set={0:F3} prefs=0x{1:X8}", clamped, renderPrefs));
        m_pVideoDisplay->SetRenderingPrefs(renderPrefs);
    }
}

void VideoPlayer::SharpenThreshold::set(double value)
{
    double clamped = Math::Clamp(value, 0.0, 0.02);
    m_sharpenThreshold = clamped;

    if (m_pVideoDisplay != nullptr)
    {
        DWORD sharpenMilli = static_cast<DWORD>(Math::Clamp(m_sharpenStrength, 0.0, 1.0) * 1000.0 + 0.5);
        DWORD thresholdMilli = static_cast<DWORD>(Math::Clamp(clamped / 0.02, 0.0, 1.0) * 1000.0 + 0.5);
        DWORD renderPrefs = (sharpenMilli & 0xFFFFu) | ((thresholdMilli & 0xFFFFu) << 16);
        ManagedLogger::Log(String::Format("[MFVideoPlayer] SharpenThreshold set={0:F4} prefs=0x{1:X8}", clamped, renderPrefs));
        m_pVideoDisplay->SetRenderingPrefs(renderPrefs);
    }
}

void VideoPlayer::Brightness::set(int value)
{
    m_colorBrightness = Math::Clamp(value, -127, 127);
    ManagedLogger::Log(String::Format("[MFVideoPlayer] Brightness set to {0}", m_colorBrightness));

    if (m_pVideoDisplay != nullptr)
    {
        DX11VideoRenderer::IDX11VideoColorControl* pColorControl = nullptr;
        HRESULT hrColor = m_pVideoDisplay->QueryInterface(__uuidof(DX11VideoRenderer::IDX11VideoColorControl), (void**)&pColorControl);
        if (SUCCEEDED(hrColor) && pColorControl)
        {
            pColorControl->SetColorControls(m_colorBrightness, m_colorContrast, m_colorHue, m_colorSaturation);
            pColorControl->Release();
        }
    }

}

void VideoPlayer::Contrast::set(int value)
{
    m_colorContrast = Math::Clamp(value, -127, 127);
    ManagedLogger::Log(String::Format("[MFVideoPlayer] Contrast set to {0}", m_colorContrast));

    if (m_pVideoDisplay != nullptr)
    {
        DX11VideoRenderer::IDX11VideoColorControl* pColorControl = nullptr;
        HRESULT hrColor = m_pVideoDisplay->QueryInterface(__uuidof(DX11VideoRenderer::IDX11VideoColorControl), (void**)&pColorControl);
        if (SUCCEEDED(hrColor) && pColorControl)
        {
            pColorControl->SetColorControls(m_colorBrightness, m_colorContrast, m_colorHue, m_colorSaturation);
            pColorControl->Release();
        }
    }

}

void VideoPlayer::Hue::set(int value)
{
    m_colorHue = Math::Clamp(value, -127, 127);
    ManagedLogger::Log(String::Format("[MFVideoPlayer] Hue set to {0}", m_colorHue));

    if (m_pVideoDisplay != nullptr)
    {
        DX11VideoRenderer::IDX11VideoColorControl* pColorControl = nullptr;
        HRESULT hrColor = m_pVideoDisplay->QueryInterface(__uuidof(DX11VideoRenderer::IDX11VideoColorControl), (void**)&pColorControl);
        if (SUCCEEDED(hrColor) && pColorControl)
        {
            pColorControl->SetColorControls(m_colorBrightness, m_colorContrast, m_colorHue, m_colorSaturation);
            pColorControl->Release();
        }
    }

}

void VideoPlayer::Saturation::set(int value)
{
    m_colorSaturation = Math::Clamp(value, -127, 127);
    ManagedLogger::Log(String::Format("[MFVideoPlayer] Saturation set to {0}", m_colorSaturation));

    if (m_pVideoDisplay != nullptr)
    {
        DX11VideoRenderer::IDX11VideoColorControl* pColorControl = nullptr;
        HRESULT hrColor = m_pVideoDisplay->QueryInterface(__uuidof(DX11VideoRenderer::IDX11VideoColorControl), (void**)&pColorControl);
        if (SUCCEEDED(hrColor) && pColorControl)
        {
            pColorControl->SetColorControls(m_colorBrightness, m_colorContrast, m_colorHue, m_colorSaturation);
            pColorControl->Release();
        }
    }

}

// Open URL
void VideoPlayer::OpenUrl(String^ url) {
    if (url == nullptr) return;
    
    EnterCriticalSection(m_pCritSec);
    
    HRESULT hr = S_OK;
    
    // Close any existing session
    hr = CloseSession();
    if (FAILED(hr)) {
        auto msg = String::Format("CloseSession failed: 0x{0:X}", hr);
        System::Diagnostics::Debug::WriteLine(msg);
        ManagedLogger::Log(msg);
    }
    
    if (SUCCEEDED(hr)) {
        // Create new session
        hr = CreateSession();
        if (FAILED(hr)) {
            auto msg = String::Format("CreateSession failed: 0x{0:X}", hr);
            System::Diagnostics::Debug::WriteLine(msg);
            ManagedLogger::Log(msg);
        }
    }
    
    if (SUCCEEDED(hr)) {
        ManagedLogger::Log("Opening: " + url);
        hr = CreateMediaSource(url);
        if (FAILED(hr)) {
            auto msg = String::Format("CreateMediaSource failed: 0x{0:X}", hr);
            System::Diagnostics::Debug::WriteLine(msg);
            ManagedLogger::Log(msg);
        }
    }
    
    if (SUCCEEDED(hr)) {
        IMFTopology* pTopology = nullptr;
        ManagedLogger::Log("Creating topology...");
        hr = CreateTopologyFromSource(&pTopology);
        if (FAILED(hr)) {
            auto msg = String::Format("CreateTopologyFromSource failed: 0x{0:X}", hr);
            System::Diagnostics::Debug::WriteLine(msg);
            ManagedLogger::Log(msg);
        } else if (SUCCEEDED(hr)) {
            m_pTopology = pTopology;
            ManagedLogger::Log("Topology created successfully");
        }
    }
    
    if (SUCCEEDED(hr) && m_pTopology != nullptr) {
        // Set topology on session
        ManagedLogger::Log("Setting topology on session...");
        hr = m_pSession->SetTopology(0, m_pTopology);
        if (FAILED(hr)) {
            auto msg = String::Format("SetTopology failed: 0x{0:X}", hr);
            System::Diagnostics::Debug::WriteLine(msg);
            ManagedLogger::Log(msg);
        } else {
            m_state = PlayerState::OpenPending;
            ManagedLogger::Log("Topology set successfully");
        }
    }
    
    LeaveCriticalSection(m_pCritSec);
    
    if (FAILED(hr)) {
        m_state = PlayerState::Closed;
        MediaFailed(this, EventArgs::Empty);
    }
}

// Play
void VideoPlayer::Play() {
    ManagedLogger::Log(String::Format("[MFVideoPlayer] Play() called - Session={0}, State={1}", 
        (m_pSession != nullptr ? "valid" : "null"), (int)m_state));
    
    if (m_pSession == nullptr) {
        ManagedLogger::Log("[MFVideoPlayer] Play() failed - no session");
        return;
    }
    
    PROPVARIANT varStart;
    PropVariantInit(&varStart);
    
    HRESULT hr = m_pSession->Start(&GUID_NULL, &varStart);
    ManagedLogger::Log(String::Format("[MFVideoPlayer] Session->Start() returned: 0x{0:X}", hr));
    
    if (SUCCEEDED(hr)) {
        m_state = PlayerState::Started;
    }
    
    PropVariantClear(&varStart);
}

// Pause
void VideoPlayer::Pause() {
    if (m_pSession == nullptr) return;
    
    HRESULT hr = m_pSession->Pause();
    
    if (SUCCEEDED(hr)) {
        m_state = PlayerState::Paused;
    }
}

// Stop
void VideoPlayer::Stop() {
    if (m_pSession == nullptr) return;
    
    HRESULT hr = m_pSession->Stop();
    
    if (SUCCEEDED(hr)) {
        m_state = PlayerState::Stopped;
    }
}

// Shutdown
void VideoPlayer::Shutdown() {
    CloseSession();
}

// Repaint video
void VideoPlayer::Repaint() {
    //printf("[MFVideoPlayer] Repaint() called, m_pVideoDisplay=%p\n", m_pVideoDisplay);
    if (m_pVideoDisplay != nullptr) {
        HRESULT hr = m_pVideoDisplay->RepaintVideo();
        //printf("[MFVideoPlayer] RepaintVideo() returned hr=0x%08X\n", hr);
    }
}

// Resize video
void VideoPlayer::ResizeVideo(int width, int height) {
    if (m_pVideoDisplay != nullptr) {
        RECT rcDest = { 0, 0, width, height };
        m_pVideoDisplay->SetVideoPosition(nullptr, &rcDest);
        // Repaint to show current frame after resize
        m_pVideoDisplay->RepaintVideo();
    }
}

// Has video
bool VideoPlayer::HasVideo() {
    return m_pVideoDisplay != nullptr;
}

// Has audio  
bool VideoPlayer::HasAudio() {
    if (m_pPresentationDescriptor == nullptr) return false;
    
    DWORD streamCount = 0;
    HRESULT hr = m_pPresentationDescriptor->GetStreamDescriptorCount(&streamCount);
    
    if (FAILED(hr)) return false;
    
    for (DWORD i = 0; i < streamCount; i++) {
        BOOL selected = FALSE;
        IMFStreamDescriptor* pSD = nullptr;
        
        hr = m_pPresentationDescriptor->GetStreamDescriptorByIndex(i, &selected, &pSD);
        
        if (SUCCEEDED(hr) && selected) {
            IMFMediaTypeHandler* pHandler = nullptr;
            hr = pSD->GetMediaTypeHandler(&pHandler);
            
            if (SUCCEEDED(hr)) {
                GUID majorType;
                hr = pHandler->GetMajorType(&majorType);
                
                if (SUCCEEDED(hr) && majorType == MFMediaType_Audio) {
                    pHandler->Release();
                    pSD->Release();
                    return true;
                }
                
                pHandler->Release();
            }
            
            pSD->Release();
        }
    }
    
    return false;
}

// Create session
HRESULT VideoPlayer::CreateSession() {
    IMFMediaSession* pSession = nullptr;
    HRESULT hr = MFCreateMediaSession(nullptr, &pSession);
    
    if (SUCCEEDED(hr)) {
        m_pSession = pSession;
    }
    
    if (SUCCEEDED(hr) && m_pCallback) {
        hr = m_pSession->BeginGetEvent(m_pCallback, nullptr);
    }
    
    return hr;
}

// Close session
HRESULT VideoPlayer::CloseSession() {
    HRESULT hr = S_OK;
    
    if (m_pVideoDisplay) {
        m_pVideoDisplay->Release();
        m_pVideoDisplay = nullptr;
    }
    
    if (m_pSession) {
        m_state = PlayerState::Closing;
        
        // STEP 1: STOP the session FIRST to halt audio playback immediately
        // This is CRITICAL - audio continues if we don't stop before closing
        DebugLog("[MFVideoPlayer] CloseSession: STOPPING session to halt audio/video playback\n");
        hr = m_pSession->Stop();
        if (SUCCEEDED(hr)) {
            DebugLog("[MFVideoPlayer] CloseSession: Session stopped successfully\n");
            // Wait for stop operation to complete
            Sleep(200);
        } else {
            DebugLog("[MFVideoPlayer] CloseSession: WARNING - Failed to stop session, hr=0x%08X\n", hr);
        }
        
        // STEP 2: CLOSE the session (initiates shutdown sequence)
        DebugLog("[MFVideoPlayer] CloseSession: Closing session\n");
        hr = m_pSession->Close();
        
        if (SUCCEEDED(hr)) {
            // Wait for close event to complete
            DebugLog("[MFVideoPlayer] CloseSession: Session close initiated, waiting for completion\n");
            Sleep(100);
        } else {
            DebugLog("[MFVideoPlayer] CloseSession: WARNING - Failed to close session, hr=0x%08X\n", hr);
        }
        
        // STEP 3: SHUTDOWN the session to release all sinks and sources
        // This triggers IMFActivate::ShutdownObject() on all activate objects
        // including the audio renderer, which MUST happen to stop audio playback
        DebugLog("[MFVideoPlayer] CloseSession: Shutting down session to release audio/video renderers\n");
        m_pSession->Shutdown();
        DebugLog("[MFVideoPlayer] CloseSession: Session shutdown complete - audio renderer should be stopped\n");
    }
    
    if (m_pSource) {
        // STEP 4: Shutdown and release the source
        DebugLog("[MFVideoPlayer] CloseSession: Shutting down media source\n");
        m_pSource->Shutdown();
        m_pSource->Release();
        m_pSource = nullptr;
        DebugLog("[MFVideoPlayer] CloseSession: Media source shutdown and released\n");
    }
    
    if (m_pSession) {
        DebugLog("[MFVideoPlayer] CloseSession: Releasing session object\n");
        m_pSession->Release();
        m_pSession = nullptr;
        DebugLog("[MFVideoPlayer] CloseSession: Session released\n");
    }
    
    if (m_pPresentationDescriptor) {
        m_pPresentationDescriptor->Release();
        m_pPresentationDescriptor = nullptr;
    }
    
    if (m_pTopology) {
        m_pTopology->Release();
        m_pTopology = nullptr;
    }
    
    m_state = PlayerState::Closed;
    DebugLog("[MFVideoPlayer] CloseSession: Complete - state set to Closed\n");
    
    return hr;
}

// Create media source
HRESULT VideoPlayer::CreateMediaSource(String^ url) {
    IMFSourceResolver* pSourceResolver = nullptr;
    IUnknown* pSource = nullptr;
    
    msclr::interop::marshal_context context;
    const wchar_t* wch = context.marshal_as<const wchar_t*>(url);
    
    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
    
    if (SUCCEEDED(hr)) {
        MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
        
        hr = pSourceResolver->CreateObjectFromURL(
            wch,
            MF_RESOLUTION_MEDIASOURCE,
            nullptr,
            &objectType,
            &pSource
        );
    }
    
    if (SUCCEEDED(hr)) {
        IMFMediaSource* pMediaSource = nullptr;
        hr = pSource->QueryInterface(IID_PPV_ARGS(&pMediaSource));
        if (SUCCEEDED(hr)) {
            m_pSource = pMediaSource;
        }
    }
    
    if (SUCCEEDED(hr)) {
        IMFPresentationDescriptor* pPD = nullptr;
        hr = m_pSource->CreatePresentationDescriptor(&pPD);
        if (SUCCEEDED(hr)) {
            m_pPresentationDescriptor = pPD;
        }
    }
    
    SafeRelease(pSourceResolver);
    SafeRelease(pSource);
    
    return hr;
}

// Create topology from source
HRESULT VideoPlayer::CreateTopologyFromSource(IMFTopology** ppTopology) {
    if (ppTopology == nullptr) {
        return E_POINTER;
    }
    
    if (m_pPresentationDescriptor == nullptr || m_pSource == nullptr) {
        return E_UNEXPECTED;
    }
    
    IMFTopology* pTopology = nullptr;
    DWORD sourceStreams = 0;
    
    HRESULT hr = MFCreateTopology(&pTopology);
    if (FAILED(hr)) {
        auto msg = String::Format("MFCreateTopology failed: 0x{0:X}", hr);
        System::Diagnostics::Debug::WriteLine(msg);
        ManagedLogger::Log(msg);
        return hr;
    }
    
    // Enable hardware acceleration for the topology (as per Microsoft TopoEdit sample)
    // MF_TOPOLOGY_HARDWARE_MODE: Tells topology loader to prefer hardware MFTs for decoding
    //   - MFTOPOLOGY_HWMODE_USE_HARDWARE: Prefer hardware, fall back to software if unavailable
    //   - MFTOPOLOGY_HWMODE_USE_ONLY_HARDWARE: Require hardware, fail if unavailable
    // MF_TOPOLOGY_DXVA_MODE: Enables full DXVA resolution - decoders will receive D3D manager
    //                        and output to DXGI surfaces instead of software buffers
    // Note: If hardware mode is unavailable, Media Foundation automatically falls back to
    //       software decoding. The DX11VideoRenderer also handles software buffers as fallback.
    hr = pTopology->SetUINT32(MF_TOPOLOGY_HARDWARE_MODE, MFTOPOLOGY_HWMODE_USE_HARDWARE);
    if (FAILED(hr)) {
        ManagedLogger::Log(String::Format("[MFVideoPlayer] Warning: Failed to set hardware mode: 0x{0:X} (will use software)", hr));
    } else {
        ManagedLogger::Log("[MFVideoPlayer] Hardware MFT mode enabled (with software fallback)");
    }
    
    hr = pTopology->SetUINT32(MF_TOPOLOGY_DXVA_MODE, MFTOPOLOGY_DXVA_FULL);
    if (FAILED(hr)) {
        ManagedLogger::Log(String::Format("[MFVideoPlayer] Warning: Failed to set DXVA mode: 0x{0:X} (will use software rendering)", hr));
    } else {
        ManagedLogger::Log("[MFVideoPlayer] DXVA full acceleration enabled");
    }
    
    hr = m_pPresentationDescriptor->GetStreamDescriptorCount(&sourceStreams);
    if (FAILED(hr)) {
        auto msg = String::Format("GetStreamDescriptorCount failed: 0x{0:X}", hr);
        System::Diagnostics::Debug::WriteLine(msg);
        ManagedLogger::Log(msg);
        SafeRelease(pTopology);
        return hr;
    }
    
    if (sourceStreams == 0) {
        String^ msg = "No streams found in presentation descriptor";
        System::Diagnostics::Debug::WriteLine(msg);
        ManagedLogger::Log(msg);
        SafeRelease(pTopology);
        return E_UNEXPECTED;
    }
    
    ManagedLogger::Log(String::Format("Found {0} stream(s)", sourceStreams));
    
    // Add each stream to the topology
    for (DWORD i = 0; i < sourceStreams; i++) {
        hr = AddBranchToPartialTopology(pTopology, m_pPresentationDescriptor, i);
        if (FAILED(hr)) {
            if (hr == E_NOINTERFACE) {
                // Stream skipped (unsupported type) - not an error
                continue;
            }
            auto msg = String::Format("AddBranchToPartialTopology failed for stream {0}: 0x{1:X}", i, hr);
            System::Diagnostics::Debug::WriteLine(msg);
            ManagedLogger::Log(msg);
            // Continue with other streams even if one fails
        } else {
            ManagedLogger::Log(String::Format("Stream {0} added successfully", i));
        }
    }
    
    // Return the topology even if some streams failed
    *ppTopology = pTopology;
    (*ppTopology)->AddRef();
    SafeRelease(pTopology);
    
    return S_OK;
}

// Add branch to topology
HRESULT VideoPlayer::AddBranchToPartialTopology(IMFTopology* pTopology, IMFPresentationDescriptor* pPD, DWORD iStream) {
    IMFStreamDescriptor* pSD = nullptr;
    IMFTopologyNode* pSourceNode = nullptr;
    IMFTopologyNode* pOutputNode = nullptr;
    
    BOOL selected = FALSE;
    
    HRESULT hr = pPD->GetStreamDescriptorByIndex(iStream, &selected, &pSD);
    if (FAILED(hr)) {
        System::Diagnostics::Debug::WriteLine(String::Format("GetStreamDescriptorByIndex failed: 0x{0:X}", hr));
        ManagedLogger::Log(String::Format("GetStreamDescriptorByIndex failed: 0x{0:X}", hr));
        return hr;
    }
    
    // Only process selected streams
    if (!selected) {
        SafeRelease(pSD);
        return S_OK;
    }
    
    hr = CreateSourceStreamNode(pPD, pSD, &pSourceNode);
    if (FAILED(hr)) {
        System::Diagnostics::Debug::WriteLine(String::Format("CreateSourceStreamNode failed: 0x{0:X}", hr));
        ManagedLogger::Log(String::Format("CreateSourceStreamNode failed: 0x{0:X}", hr));
        SafeRelease(pSD);
        return hr;
    }
    
    hr = CreateOutputNode(pSD, &pOutputNode);
    if (FAILED(hr)) {
        if (hr == E_NOINTERFACE) {
            // Stream type not supported (e.g., subtitles) - skip silently
            ManagedLogger::Log(String::Format("Stream {0} skipped (unsupported type)", iStream));
        } else {
            System::Diagnostics::Debug::WriteLine(String::Format("CreateOutputNode failed: 0x{0:X}", hr));
            ManagedLogger::Log(String::Format("CreateOutputNode failed: 0x{0:X}", hr));
        }
        SafeRelease(pSD);
        SafeRelease(pSourceNode);
        return hr;
    }

    hr = pTopology->AddNode(pSourceNode);
    if (FAILED(hr)) {
        System::Diagnostics::Debug::WriteLine(String::Format("AddNode (source) failed: 0x{0:X}", hr));
        ManagedLogger::Log(String::Format("AddNode (source) failed: 0x{0:X}", hr));
        SafeRelease(pSD);
        SafeRelease(pSourceNode);
        SafeRelease(pOutputNode);
        return hr;
    }
    
    hr = pTopology->AddNode(pOutputNode);
    if (FAILED(hr)) {
        System::Diagnostics::Debug::WriteLine(String::Format("AddNode (output) failed: 0x{0:X}", hr));
        ManagedLogger::Log(String::Format("AddNode (output) failed: 0x{0:X}", hr));
        SafeRelease(pSD);
        SafeRelease(pSourceNode);
        SafeRelease(pOutputNode);
        return hr;
    }

    hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);

    if (FAILED(hr)) {
        System::Diagnostics::Debug::WriteLine(String::Format("ConnectOutput failed: 0x{0:X}", hr));
        ManagedLogger::Log(String::Format("ConnectOutput failed: 0x{0:X}", hr));
    }
    
    SafeRelease(pSD);
    SafeRelease(pSourceNode);
    SafeRelease(pOutputNode);
    
    return hr;
}

// Create source stream node
HRESULT VideoPlayer::CreateSourceStreamNode(IMFPresentationDescriptor* pPD, IMFStreamDescriptor* pSD, IMFTopologyNode** ppNode) {
    IMFTopologyNode* pNode = nullptr;
    
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
    
    if (SUCCEEDED(hr)) {
        hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, m_pSource);
    }
    
    if (SUCCEEDED(hr)) {
        hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
    }
    
    if (SUCCEEDED(hr)) {
        hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
    }
    
    if (SUCCEEDED(hr)) {
        *ppNode = pNode;
        (*ppNode)->AddRef();
    }
    
    SafeRelease(pNode);
    
    return hr;
}

// Create output node
HRESULT VideoPlayer::CreateOutputNode(IMFStreamDescriptor* pSD, IMFTopologyNode** ppNode) {
    IMFTopologyNode* pNode = nullptr;
    IMFMediaTypeHandler* pHandler = nullptr;
    IMFActivate* pActivate = nullptr;
    IMFMediaSink* pSink = nullptr;
    IMFStreamSink* pStreamSink = nullptr;
    
    GUID guidMajorType = GUID_NULL;
    
    HRESULT hr = pSD->GetMediaTypeHandler(&pHandler);
    
    if (SUCCEEDED(hr)) {
        hr = pHandler->GetMajorType(&guidMajorType);
    }
    
    // Create appropriate renderer based on stream type
    if (SUCCEEDED(hr)) {
        if (guidMajorType == MFMediaType_Audio) {
            hr = MFCreateAudioRendererActivate(&pActivate);
            ManagedLogger::Log(String::Format("[MFVideoPlayer] MFCreateAudioRendererActivate result: 0x{0:X}", hr));
        }
        else if (guidMajorType == MFMediaType_Video) {
            // Check if video window is valid before creating video renderer
            if (m_hwndVideo == nullptr || !IsWindow(m_hwndVideo)) {
                ManagedLogger::Log(String::Format("[MFVideoPlayer] WARNING: Invalid video window hwnd={0}", (IntPtr)m_hwndVideo));
                SafeRelease(pHandler);
                return E_FAIL;  // Cannot create video renderer without valid window
            }
            
#if USE_MS_DX11_RENDERER_DLL
            // Use Microsoft sample DX11VideoRenderer DLL for testing
            // Following the TopoEdit pattern: activate, get sink, get stream sink
            hr = CreateMSDX11VideoRendererActivate(m_hwndVideo, &pActivate);
            ManagedLogger::Log(String::Format("[MFVideoPlayer] Using MS DX11VideoRenderer DLL, result: 0x{0:X}, hwnd={1}", hr, (IntPtr)m_hwndVideo));
            
            if (SUCCEEDED(hr)) {
                // Activate the sink and get the stream sink (like TopoEdit does)
                hr = pActivate->ActivateObject(IID_IMFMediaSink, (void**)&pSink);
                ManagedLogger::Log(String::Format("[MFVideoPlayer] ActivateObject(IMFMediaSink) result: 0x{0:X}", hr));
            }
            
            if (SUCCEEDED(hr)) {
                // MS sample uses stream ID 1, not 0
                hr = pSink->GetStreamSinkById(1, &pStreamSink);
                ManagedLogger::Log(String::Format("[MFVideoPlayer] GetStreamSinkById(1) result: 0x{0:X}", hr));
            }
#elif USE_DX11_RENDERER
            // Use custom DX11 Video Renderer with stored GPU adapter index
            hr = DX11VideoRenderer::CreateDX11VideoRendererActivate(m_hwndVideo, &pActivate, m_gpuAdapterIndex);
            ManagedLogger::Log(String::Format("[MFVideoPlayer] CreateDX11VideoRendererActivate result: 0x{0:X}, hwnd={1}, GPU={2}", hr, (IntPtr)m_hwndVideo, m_gpuAdapterIndex));
#else
            // Use EVR (Enhanced Video Renderer)
            hr = MFCreateVideoRendererActivate(m_hwndVideo, &pActivate);
            ManagedLogger::Log(String::Format("[MFVideoPlayer] MFCreateVideoRendererActivate result: 0x{0:X}, hwnd={1}", hr, (IntPtr)m_hwndVideo));
#endif
        }
        else {
            // Unsupported stream type (subtitles, metadata, etc.)
            // Return E_NOINTERFACE to skip this stream gracefully
            SafeRelease(pHandler);
            return E_NOINTERFACE;
        }
    }
    
    if (SUCCEEDED(hr)) {
        hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
    }
    
    if (SUCCEEDED(hr)) {
#if USE_MS_DX11_RENDERER_DLL
        // For MS DX11 renderer, set the stream sink (not the activate)
        if (pStreamSink != nullptr) {
            hr = pNode->SetObject(pStreamSink);
            ManagedLogger::Log(String::Format("[MFVideoPlayer] SetObject(StreamSink) result: 0x{0:X}", hr));
        } else {
            hr = pNode->SetObject(pActivate);
        }
#else
        hr = pNode->SetObject(pActivate);
#endif
    }
    
    if (SUCCEEDED(hr)) {
        *ppNode = pNode;
        (*ppNode)->AddRef();
    }
    
    SafeRelease(pNode);
    SafeRelease(pHandler);
    SafeRelease(pActivate);
    SafeRelease(pSink);
    SafeRelease(pStreamSink);
    
    return hr;
}

// Handle Media Foundation events
HRESULT VideoPlayer::HandleEvent(IMFMediaEvent* pEvent) {
    if (pEvent == nullptr) {
        return E_POINTER;
    }
    
    MediaEventType meType = MEUnknown;
    HRESULT hr = pEvent->GetType(&meType);
    
    ManagedLogger::Log(String::Format("[MFVideoPlayer] HandleEvent - Type={0}", (int)meType));
    
    if (SUCCEEDED(hr)) {
        switch (meType) {
            case MESessionTopologyStatus:
                {
                    UINT32 status = 0;
                    hr = pEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);
                    ManagedLogger::Log(String::Format("[MFVideoPlayer] TopologyStatus={0}, hr=0x{1:X}", status, hr));
                    if (SUCCEEDED(hr) && status == MF_TOPOSTATUS_READY) {
                        ManagedLogger::Log("[MFVideoPlayer] Topology is READY - getting video display control");
                        if (m_pVideoDisplay) {
                            m_pVideoDisplay->Release();
                            m_pVideoDisplay = nullptr;
                        }
                        
                        // Use local pointer to avoid interior_ptr issues with IID_PPV_ARGS
                        IMFVideoDisplayControl* pDisplay = nullptr;
                        hr = MFGetService(m_pSession, MR_VIDEO_RENDER_SERVICE, IID_PPV_ARGS(&pDisplay));
                        ManagedLogger::Log(String::Format("[MFVideoPlayer] MFGetService result: hr=0x{0:X}, pDisplay={1}", hr, (pDisplay != nullptr ? "valid" : "null")));
                        
                        if (SUCCEEDED(hr)) {
                            m_pVideoDisplay = pDisplay;
                            
                            // Set rendering preferences for flip model background rendering
                            // MFVideoRenderPrefs_DoNotClipToDevice (0x2) prevents clipping across monitors
                            // This may help with minimized window rendering
                            DWORD renderPrefs = 0x2; // MFVideoRenderPrefs_DoNotClipToDevice
                            HRESULT hrPrefs = m_pVideoDisplay->SetRenderingPrefs(renderPrefs);
                            ManagedLogger::Log(String::Format("[MFVideoPlayer] SetRenderingPrefs(0x{0:X}) result: 0x{1:X}", renderPrefs, hrPrefs));

                            // Re-apply user sharpen settings after topology/render service becomes ready.
                            DWORD sharpenMilli = static_cast<DWORD>(Math::Clamp(m_sharpenStrength, 0.0, 1.0) * 1000.0 + 0.5);
                            DWORD thresholdMilli = static_cast<DWORD>(Math::Clamp(m_sharpenThreshold / 0.02, 0.0, 1.0) * 1000.0 + 0.5);
                            DWORD sharpenPrefs = (sharpenMilli & 0xFFFFu) | ((thresholdMilli & 0xFFFFu) << 16);
                            HRESULT hrSharpen = m_pVideoDisplay->SetRenderingPrefs(sharpenPrefs);
                            ManagedLogger::Log(String::Format("[MFVideoPlayer] SetRenderingPrefs(sharpen=0x{0:X}) result: 0x{1:X}", sharpenPrefs, hrSharpen));
                            
                            // Update video position
                            if (m_hwndVideo) {
                                RECT rc;
                                GetClientRect(m_hwndVideo, &rc);
                                ManagedLogger::Log(String::Format("[MFVideoPlayer] Setting video position: {0}x{1}", rc.right, rc.bottom));
                                m_pVideoDisplay->SetVideoPosition(nullptr, &rc);
                            }
                            
                            m_state = PlayerState::Ready;
                            ManagedLogger::Log("[MFVideoPlayer] State set to Ready, firing MediaOpened");
                            MediaOpened(this, EventArgs::Empty);
                        }
                    }
                }
                break;
                
            case MESessionStarted:
                ManagedLogger::Log("[MFVideoPlayer] MESessionStarted received");
                m_state = PlayerState::Started;
                break;
                
            case MESessionPaused:
                ManagedLogger::Log("[MFVideoPlayer] MESessionPaused received");
                m_state = PlayerState::Paused;
                break;
                
            case MESessionStopped:
                ManagedLogger::Log("[MFVideoPlayer] MESessionStopped received");
                m_state = PlayerState::Stopped;
                break;
                
            case MESessionEnded:
                ManagedLogger::Log("[MFVideoPlayer] MESessionEnded received");
                m_state = PlayerState::Stopped;
                MediaEnded(this, EventArgs::Empty);
                break;
                
            case MEError:
                {
                    HRESULT hrStatus = S_OK;
                    pEvent->GetStatus(&hrStatus);
                    ManagedLogger::Log(String::Format("[MFVideoPlayer] MEError received: 0x{0:X}", hrStatus));
                }
                MediaFailed(this, EventArgs::Empty);
                break;
        }
    }
    
    return hr;
}

// Callback invoke implementation
STDMETHODIMP CPlayerCallback::Invoke(IMFAsyncResult* pResult) {
    IMFMediaEvent* pEvent = nullptr;
    VideoPlayer^ player = m_pPlayer;
    
    if (player == nullptr) {
        return E_POINTER;
    }
    
    // Get the session from the player
    IMFMediaSession* pSession = player->GetSession();
    
    if (pSession == nullptr) {
        return E_POINTER;
    }
    
    HRESULT hr = pSession->EndGetEvent(pResult, &pEvent);
    
    if (FAILED(hr)) {
        ManagedLogger::Log(String::Format("[CPlayerCallback] EndGetEvent failed: 0x{0:X}", hr));
        return hr;
    }
    
    if (pEvent == nullptr) {
        return E_POINTER;
    }
    
    // Get the event type
    MediaEventType meType = MEUnknown;
    hr = pEvent->GetType(&meType);
    
    if (SUCCEEDED(hr)) {
        ManagedLogger::Log(String::Format("[CPlayerCallback] Invoke - Event type: {0}", (int)meType));
        
        // For all events except MESessionClosed, request the next event
        if (meType != MESessionClosed) {
            hr = pSession->BeginGetEvent(this, nullptr);
            if (FAILED(hr)) {
                ManagedLogger::Log(String::Format("[CPlayerCallback] BeginGetEvent failed: 0x{0:X}", hr));
            }
        }
        
        // Now handle the event
        player->HandleEvent(pEvent);
    }
    
    pEvent->Release();
    return S_OK;
}
// GPU adapter info property implementations
unsigned int GPUAdapterInfo::AdapterIndex::get() { return m_index; }
String^ GPUAdapterInfo::Description::get() { return m_description; }
unsigned int GPUAdapterInfo::VendorId::get() { return m_vendorId; }
unsigned int GPUAdapterInfo::DeviceId::get() { return m_deviceId; }
unsigned long long GPUAdapterInfo::DedicatedVideoMemory::get() { return m_vram; }
unsigned long long GPUAdapterInfo::SharedSystemMemory::get() { return m_shared; }

// Enumerate available GPU adapters
List<GPUAdapterInfo^>^ VideoPlayer::EnumerateGPUs()
{
    auto gpuList = gcnew List<GPUAdapterInfo^>();
    
    IDXGIFactory1* pFactory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory);
    if (FAILED(hr))
    {
        ManagedLogger::Log(String::Format("[VideoPlayer] EnumerateGPUs: CreateDXGIFactory1 failed 0x{0:X}", hr));
        return gpuList;
    }

    UINT adapterIndex = 0;
    IDXGIAdapter1* pAdapter = nullptr;
    while ((hr = pFactory->EnumAdapters1(adapterIndex, &pAdapter)) != DXGI_ERROR_NOT_FOUND)
    {
        if (FAILED(hr))
        {
            ManagedLogger::Log(String::Format("[VideoPlayer] EnumAdapters1 failed for index {0}, hr=0x{1:X}", adapterIndex, hr));
            break;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        HRESULT hrDesc = pAdapter->GetDesc1(&desc);
        if (SUCCEEDED(hrDesc))
        {
            String^ description = gcnew String(desc.Description);
            auto gpuInfo = gcnew GPUAdapterInfo(
                adapterIndex,
                description,
                desc.VendorId,
                desc.DeviceId,
                desc.DedicatedVideoMemory,
                desc.SharedSystemMemory);

            gpuList->Add(gpuInfo);

            ManagedLogger::Log(String::Format(
                "[VideoPlayer] GPU {0}: {1} - VRAM: {2} MB, Shared: {3} MB (Vendor: 0x{4:X}, Device: 0x{5:X})",
                adapterIndex,
                description,
                desc.DedicatedVideoMemory / (1024 * 1024),
                desc.SharedSystemMemory / (1024 * 1024),
                desc.VendorId,
                desc.DeviceId));
        }

        pAdapter->Release();
        pAdapter = nullptr;
        adapterIndex++;
    }

    if (pAdapter) pAdapter->Release();
    if (pFactory) pFactory->Release();

    ManagedLogger::Log(String::Format("[VideoPlayer] Found {0} GPU adapters", gpuList->Count));
    return gpuList;
}

// Set GPU adapter for decoding and rendering
void VideoPlayer::SetGPUAdapter(unsigned int adapterIndex)
{
    m_gpuAdapterIndex = adapterIndex;
    ManagedLogger::Log(String::Format("[VideoPlayer] SetGPUAdapter: adapter index {0} stored for use during topology creation", adapterIndex));
}

} // end namespace Player
} // end namespace MediaFoundation
