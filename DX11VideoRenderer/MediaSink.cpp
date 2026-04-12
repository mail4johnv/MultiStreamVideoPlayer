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

#include "MediaSink.h"
#include <stdio.h>

using namespace DX11VideoRenderer;

// CRT Debug Memory Leak Detection - Enable automatic leak reporting at program exit
#ifdef _DEBUG
namespace {
    struct CrtDebugInitializer {
        CrtDebugInitializer() {
            // Enable memory leak detection
            _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
            
            // Optional: Set report mode to output window
            _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        }
    };
    static CrtDebugInitializer s_crtDebugInit;
}
#endif
#define DebugLog LOG_DEBUG

//// Debug output helper
//static void DebugLog(const char* format, ...)
//{
//    char buffer[512];
//    va_list args;
//    va_start(args, format);
//    vsprintf_s(buffer, format, args);
//    va_end(args);
//    
//  //  OutputDebugStringA(buffer);
//    
//    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
//    if (hStdOut != INVALID_HANDLE_VALUE && hStdOut != NULL)
//    {
//        DWORD written;
//        WriteConsoleA(hStdOut, buffer, (DWORD)strlen(buffer), &written, NULL);
//    }
//}

// Stream sink ID
static const DWORD STREAM_ID = 0;

// Per-sink shared critical section is now a member; no static guard or destruction concerns



//-----------------------------------------------------------------------------
// CMediaSink
//-----------------------------------------------------------------------------

HRESULT CMediaSink::CreateInstance(HWND hwndVideo, IMFMediaSink** ppSink, UINT gpuAdapterIndex)
{
    if (!ppSink)
    {
        return E_POINTER;
    }

    *ppSink = nullptr;

    CMediaSink* pSink = DBG_NEW CMediaSink(hwndVideo, gpuAdapterIndex);
    if (!pSink)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pSink->Initialize();
    if (FAILED(hr))
    {
        delete pSink;
        return hr;
    }

    *ppSink = static_cast<IMFMediaSink*>(pSink);
    // Constructor sets refcount to 1, so don't AddRef again

    return S_OK;
}

CMediaSink::CMediaSink(HWND hwndVideo, UINT gpuAdapterIndex) :
    m_nRefCount(1),
    m_bShutdown(FALSE),
    m_hwndVideo(hwndVideo),
    m_gpuAdapterIndex(gpuAdapterIndex),
    m_spPresenter(),
    m_spStreamSink(),
    m_pScheduler(nullptr),
    m_spClock(),
    m_dwAspectRatioMode(MFVideoARMode_PreservePicture),
    m_clrBorder(RGB(0, 0, 0)),
    m_dwRenderingPrefs(0),
    m_bFullscreen(FALSE)
{
    ZeroMemory(&m_rcDest, sizeof(m_rcDest));
    m_nrcSource.left = 0.0f;
    m_nrcSource.top = 0.0f;
    m_nrcSource.right = 1.0f;
    m_nrcSource.bottom = 1.0f;
}

CMediaSink::~CMediaSink()
{
    //DebugLog("[DX11MediaSink] Destructor called\n");
    
    // Ensure Shutdown is called if it hasn't been already
    // Shutdown() handles all proper cleanup including clock removal
    if (!m_bShutdown)
    {
        Shutdown();
    }
    
    // After Shutdown(), these should already be released, but check defensively
    if (m_spStreamSink)
    {
        DebugLog("[DX11MediaSink] Destructor: WARNING - StreamSink not released by Shutdown\n");
        m_spStreamSink.Release();
    }
    
    if (m_pScheduler)
    {
        DebugLog("[DX11MediaSink] Destructor: WARNING - Scheduler not deleted by Shutdown\n");
        delete m_pScheduler;
        m_pScheduler = nullptr;
    }
    
    if (m_spPresenter)
    {
        DebugLog("[DX11MediaSink] Destructor: WARNING - Presenter not released by Shutdown\n");
        m_spPresenter.Release();
    }
    
    // Clock should have been released in Shutdown, but check defensively
    if (m_spClock)
    {
        DebugLog("[DX11MediaSink] Destructor: WARNING - Clock not released by Shutdown, releasing now\n");
        m_spClock.Release();
    }
    
    DebugLog("[DX11MediaSink] Destructor complete\n");
}

HRESULT CMediaSink::Initialize()
{
    HRESULT hr = S_OK;

    // Create presenter with GPU adapter index
    hr = CPresenter::CreateInstance(&m_spPresenter, m_gpuAdapterIndex);
    if (FAILED(hr))
    {
        return hr;
    }

    // Initialize presenter with window
    hr = m_spPresenter->Initialize(m_hwndVideo, m_gpuAdapterIndex);
    if (FAILED(hr))
    {
        return hr;
    }

    // Create scheduler (owned by MediaSink)
    m_pScheduler = DBG_NEW CScheduler(m_spPresenter);
    if (!m_pScheduler)
    {
        return E_OUTOFMEMORY;
    }

    // Create stream sink (pass scheduler reference and shared critical section)
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    hr = CStreamSink::CreateInstance(this, STREAM_ID, m_spPresenter, m_pScheduler, *pSharedCritSec, &m_spStreamSink);
    if (FAILED(hr))
    {
        return hr;
    }

    // Set stream sink as scheduler callback
    m_pScheduler->SetCallback(static_cast<ISchedulerCallback*>(m_spStreamSink.p));
    return S_OK;
}

// IUnknown

STDMETHODIMP CMediaSink::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    if (riid == IID_IUnknown)
    {
        DebugLog("[DX11MediaSink] QueryInterface: IID_IUnknown\n");
        *ppv = static_cast<IUnknown*>(static_cast<IMFMediaSink*>(this));
    }
    else if (riid == IID_IMFMediaSink)
    {
        DebugLog("[DX11MediaSink] QueryInterface: IID_IMFMediaSink\n");
        *ppv = static_cast<IMFMediaSink*>(this);
    }
    else if (riid == IID_IMFClockStateSink)
    {
        DebugLog("[DX11MediaSink] QueryInterface: IID_IMFClockStateSink, this=0x%p\n", this);
        *ppv = static_cast<IMFClockStateSink*>(this);
    }
    else if (riid == IID_IMFMediaSinkPreroll)
    {
        DebugLog("[DX11MediaSink] QueryInterface: IID_IMFMediaSinkPreroll\n");
        *ppv = static_cast<IMFMediaSinkPreroll*>(this);
    }
    else if (riid == IID_IMFGetService)
    {
        *ppv = static_cast<IMFGetService*>(this);
    }
    else if (riid == IID_IMFRateSupport)
    {
        *ppv = static_cast<IMFRateSupport*>(this);
    }
    else if (riid == IID_IMFVideoDisplayControl)
    {
        *ppv = static_cast<IMFVideoDisplayControl*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CMediaSink::AddRef()
{
    ULONG count = InterlockedIncrement(&m_nRefCount);
    //DebugLog("[DX11MediaSink] AddRef: refcount=%lu\n", count);
    return count;
}

STDMETHODIMP_(ULONG) CMediaSink::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    //DebugLog("[DX11MediaSink] Release: refcount=%lu\n", uCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

// IMFMediaSink

STDMETHODIMP CMediaSink::GetCharacteristics(DWORD* pdwCharacteristics)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pdwCharacteristics)
    {
        return E_POINTER;
    }

    *pdwCharacteristics = MEDIASINK_FIXED_STREAMS | MEDIASINK_CAN_PREROLL;
    return S_OK;
}

STDMETHODIMP CMediaSink::AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType* pMediaType, IMFStreamSink** ppStreamSink)
{
    // Fixed stream sink - cannot add more
    return MF_E_STREAMSINKS_FIXED;
}

STDMETHODIMP CMediaSink::RemoveStreamSink(DWORD dwStreamSinkIdentifier)
{
    // Fixed stream sink - cannot remove
    return MF_E_STREAMSINKS_FIXED;
}

STDMETHODIMP CMediaSink::GetStreamSinkCount(DWORD* pcStreamSinkCount)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pcStreamSinkCount)
    {
        return E_POINTER;
    }

    *pcStreamSinkCount = 1;
    return S_OK;
}

STDMETHODIMP CMediaSink::GetStreamSinkByIndex(DWORD dwIndex, IMFStreamSink** ppStreamSink)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppStreamSink)
    {
        return E_POINTER;
    }

    if (dwIndex != 0)
    {
        return MF_E_INVALIDINDEX;
    }

    if (!m_spStreamSink)
    {
        return E_UNEXPECTED;
    }

    return m_spStreamSink->QueryInterface(IID_PPV_ARGS(ppStreamSink));
}

STDMETHODIMP CMediaSink::GetStreamSinkById(DWORD dwStreamSinkIdentifier, IMFStreamSink** ppStreamSink)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppStreamSink)
    {
        return E_POINTER;
    }

    if (dwStreamSinkIdentifier != STREAM_ID)
    {
        return MF_E_INVALIDSTREAMNUMBER;
    }

    if (!m_spStreamSink)
    {
        return E_UNEXPECTED;
    }

    return m_spStreamSink->QueryInterface(IID_PPV_ARGS(ppStreamSink));
}

STDMETHODIMP CMediaSink::SetPresentationClock(IMFPresentationClock* pPresentationClock)
{
    DebugLog("[DX11MediaSink] SetPresentationClock called, clock=0x%p\n", pPresentationClock);
    
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        DebugLog("[DX11MediaSink] SetPresentationClock: CheckShutdown failed hr=0x%08X\n", hr);
        return hr;
    }

    // Wrap incoming clock in smart pointer for RAII while we swap
    CComPtr<IMFPresentationClock> spNewClock = pPresentationClock;

    // Remove from old clock
    if (m_spClock)
    {
        DebugLog("[DX11MediaSink] SetPresentationClock: Removing old clock sink\n");
        hr = m_spClock->RemoveClockStateSink(this);
        m_spClock.Release();
    }

    // Set new clock
    if (spNewClock)
    {
        m_spClock = spNewClock;

        hr = m_spClock->AddClockStateSink(this);
        DebugLog("[DX11MediaSink] SetPresentationClock: AddClockStateSink hr=0x%08X, this=0x%p\n", hr, this);

        // Check if the clock is already running - if so, we missed OnClockStart
        // This happens when SetPresentationClock is called after Session->Start()
        if (SUCCEEDED(hr))
        {
            MFCLOCK_STATE clockState = MFCLOCK_STATE_INVALID;
            HRESULT hrState = m_spClock->GetState(0, &clockState);
            DebugLog("[DX11MediaSink] SetPresentationClock: clock state=%d, hr=0x%08X\n", clockState, hrState);
            
            if (SUCCEEDED(hrState) && clockState == MFCLOCK_STATE_RUNNING)
            {
                // Clock is already running, manually trigger OnClockStart
                MFTIME hnsClockTime = 0;
                MFTIME hnsSystemTime = 0;
                m_spClock->GetCorrelatedTime(0, &hnsClockTime, &hnsSystemTime);
                DebugLog("[DX11MediaSink] SetPresentationClock: Clock already running! Calling OnClockStart manually\n");
                OnClockStart(hnsSystemTime, hnsClockTime);
            }
        }
    }
    else
    {
        DebugLog("[DX11MediaSink] SetPresentationClock: clock is NULL\n");
    }

    return hr;
}

STDMETHODIMP CMediaSink::GetPresentationClock(IMFPresentationClock** ppPresentationClock)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppPresentationClock)
    {
        return E_POINTER;
    }

    if (!m_spClock)
    {
        return MF_E_NO_CLOCK;
    }

    return m_spClock.CopyTo(ppPresentationClock);
}

STDMETHODIMP CMediaSink::Shutdown()
{
    DWORD dwThreadId = GetCurrentThreadId();
    DebugLog("[DX11MediaSink] Shutdown: ========== ENTERING SHUTDOWN (Thread 0x%X) ==========\n", dwThreadId);
    
    // Capture pointers and set shutdown flag under lock
    CComPtr<CStreamSink> spStreamSinkToShutdown;
    CScheduler* pSchedulerToFlush = nullptr;
    CComPtr<CPresenter> spPresenterToShutdown;
    CComPtr<IMFPresentationClock> spClockToRelease;
    
    {
        CAutoLock lock(&m_critSec);
        DebugLog("[DX11MediaSink] Shutdown: Critical section lock acquired\n");

        if (m_bShutdown)
        {
            DebugLog("[DX11MediaSink] Shutdown: Already in shutdown state, returning\n");
            return MF_E_SHUTDOWN;
        }

        m_bShutdown = TRUE;
        DebugLog("[DX11MediaSink] Shutdown: Shutdown flag set to TRUE\n");

        // Capture all pointers and NULL out members while holding the lock
        // This prevents any other threads from using these pointers after shutdown
        spStreamSinkToShutdown = m_spStreamSink;
        m_spStreamSink.Release();
        
        pSchedulerToFlush = m_pScheduler;
        m_pScheduler = nullptr;
        
        spPresenterToShutdown = m_spPresenter;
        m_spPresenter.Release();
        
        spClockToRelease = m_spClock;
        m_spClock.Release();
        
        DebugLog("[DX11MediaSink] Shutdown: All member pointers nulled out\n");
    }
    // Lock is released here
    
    // STEP 1: Flush stream sink FIRST to clear any pending samples
    // This is done WITHOUT holding the critical section lock
    // This prevents deadlock if stream sink needs to call back into MediaSink
    if (spStreamSinkToShutdown)
    {
        DebugLog("[DX11MediaSink] Shutdown: Flushing stream sink to clear pending audio/video samples (lock NOT held)\n");
        HRESULT hrFlush = spStreamSinkToShutdown->Flush();
        DebugLog("[DX11MediaSink] Shutdown: Stream sink flush returned 0x%08X\n", hrFlush);
    }
    else
    {
        DebugLog("[DX11MediaSink] Shutdown: WARNING - Stream sink is NULL!\n");
    }

    // STEP 2: Shutdown stream sink to prevent callbacks
    if (spStreamSinkToShutdown)
    {
        DebugLog("[DX11MediaSink] Shutdown: Shutting down stream sink (lock NOT held)\n");
        HRESULT hrShutdown = spStreamSinkToShutdown->Shutdown();
        DebugLog("[DX11MediaSink] Shutdown: Stream sink shutdown returned 0x%08X\n", hrShutdown);
    }

    // STEP 3: Stop and flush scheduler (prevents any remaining audio playback)
    // OnSampleFree callbacks will be ignored since stream sink is shutdown
    if (pSchedulerToFlush)
    {
        DebugLog("[DX11MediaSink] Shutdown: Stopping scheduler (lock NOT held)\n");
        HRESULT hrStop = pSchedulerToFlush->Stop();
        DebugLog("[DX11MediaSink] Shutdown: Scheduler stopped with hr=0x%08X\n", hrStop);
        
        DebugLog("[DX11MediaSink] Shutdown: Flushing scheduler to remove queued samples\n");
        HRESULT hrFlush = pSchedulerToFlush->Flush();
        DebugLog("[DX11MediaSink] Shutdown: Scheduler flush completed with hr=0x%08X\n", hrFlush);
    }
    else
    {
        DebugLog("[DX11MediaSink] Shutdown: WARNING - Scheduler is NULL!\n");
    }

    // STEP 4: Shutdown presenter
    if (spPresenterToShutdown)
    {
        DebugLog("[DX11MediaSink] Shutdown: Shutting down presenter (lock NOT held)\n");
        HRESULT hrPresenter = spPresenterToShutdown->Shutdown();
        DebugLog("[DX11MediaSink] Shutdown: Presenter shutdown returned 0x%08X\n", hrPresenter);
    }
    else
    {
        DebugLog("[DX11MediaSink] Shutdown: WARNING - Presenter is NULL!\n");
    }

    // STEP 5: Remove clock state sink BEFORE releasing clock
    // CRITICAL: Must remove ourselves as clock sink before releasing, otherwise
    // the clock may try to call OnClockStart/Stop on a destroyed object
    if (spClockToRelease)
    {
        DebugLog("[DX11MediaSink] Shutdown: Removing clock state sink\n");
        HRESULT hrRemove = spClockToRelease->RemoveClockStateSink(this);
        DebugLog("[DX11MediaSink] Shutdown: Clock state sink removed hr=0x%08X\n", hrRemove);
        
        DebugLog("[DX11MediaSink] Shutdown: Releasing clock\n");
        spClockToRelease.Release();
        DebugLog("[DX11MediaSink] Shutdown: Clock released\n");
    }
    else
    {
        DebugLog("[DX11MediaSink] Shutdown: Clock is NULL\n");
    }

    // STEP 6: Release all captured objects to prevent memory leaks
    // CRITICAL: These objects were nulled out in the member variables,
    // so the destructor won't clean them up - we MUST release them here
    if (spPresenterToShutdown)
    {
        DebugLog("[DX11MediaSink] Shutdown: Releasing presenter object\n");
        spPresenterToShutdown.Release();
        DebugLog("[DX11MediaSink] Shutdown: Presenter released\n");
    }
    
    if (pSchedulerToFlush)
    {
        DebugLog("[DX11MediaSink] Shutdown: Deleting scheduler object\n");
        delete pSchedulerToFlush;
        DebugLog("[DX11MediaSink] Shutdown: Scheduler deleted\n");
    }
    
    if (spStreamSinkToShutdown)
    {
        DebugLog("[DX11MediaSink] Shutdown: Releasing stream sink object\n");
        spStreamSinkToShutdown.Release();
        DebugLog("[DX11MediaSink] Shutdown: Stream sink released\n");
    }

    DebugLog("[DX11MediaSink] Shutdown: ========== SHUTDOWN COMPLETE (Thread 0x%X) ==========\n", dwThreadId);
    return S_OK;
}

// IMFClockStateSink

STDMETHODIMP CMediaSink::OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset)
{
    DebugLog("[DX11MediaSink] OnClockStart called\n");
    
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    
    // Capture pointers and check shutdown state under lock
    CComPtr<CStreamSink> spStreamSinkToStart;
    CScheduler* pSchedulerToStart = nullptr;
    CComPtr<IMFClock> spClockForScheduler;
    
    {
        CAutoLock lock(pSharedCritSec);
        
        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            DebugLog("[DX11MediaSink] OnClockStart: shutdown, hr=0x%08X\n", hr);
            return hr;
        }
        
        // Capture pointers while holding lock
        spStreamSinkToStart = m_spStreamSink;
        pSchedulerToStart = m_pScheduler;
        
        // Query clock under lock
        if (m_spClock)
        {
            m_spClock->QueryInterface(IID_PPV_ARGS(&spClockForScheduler));
        }
    }
    // Lock released here
    
    // IMPORTANT: Start the stream sink FIRST to set the state to STARTED
    // before the scheduler begins processing samples
    // Do this WITHOUT holding the shared critical section to avoid recursive locks
    HRESULT hr = S_OK;
    if (spStreamSinkToStart)
    {
        DebugLog("[DX11MediaSink] OnClockStart: calling StreamSink->Start() (lock NOT held)\n");
        hr = spStreamSinkToStart->Start(llClockStartOffset);
        DebugLog("[DX11MediaSink] OnClockStart: StreamSink->Start() returned 0x%08X\n", hr);
    }
    else
    {
        DebugLog("[DX11MediaSink] OnClockStart: stream sink is NULL!\n");
    }

    // Now start scheduler with clock - samples can be processed
    if (SUCCEEDED(hr) && pSchedulerToStart && spClockForScheduler)
    {
        DebugLog("[DX11MediaSink] OnClockStart: starting scheduler\n");
        pSchedulerToStart->Start(spClockForScheduler);
    }

    DebugLog("[DX11MediaSink] OnClockStart: completed, hr=0x%08X\n", hr);
    return hr;
}

STDMETHODIMP CMediaSink::OnClockStop(MFTIME hnsSystemTime)
{
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    
    // Capture pointers and check shutdown state under lock
    CComPtr<CStreamSink> spStreamSinkToStop;
    CScheduler* pSchedulerToStop = nullptr;
    
    {
        CAutoLock lock(pSharedCritSec);
        
        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            return hr;
        }
        
        spStreamSinkToStop = m_spStreamSink;
        pSchedulerToStop = m_pScheduler;
    }
    // Lock released here
    
    // Stop scheduler and stream sink WITHOUT holding the shared critical section
    if (pSchedulerToStop)
    {
        pSchedulerToStop->Stop();
    }

    HRESULT hr = S_OK;
    if (spStreamSinkToStop)
    {
        hr = spStreamSinkToStop->Stop();
    }

    return hr;
}

STDMETHODIMP CMediaSink::OnClockPause(MFTIME hnsSystemTime)
{
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    
    // Capture pointers and check shutdown state under lock
    CComPtr<CStreamSink> spStreamSinkToPause;
    
    {
        CAutoLock lock(pSharedCritSec);
        
        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            return hr;
        }
        
        spStreamSinkToPause = m_spStreamSink;
    }
    // Lock released here
    
    // Pause stream sink WITHOUT holding the shared critical section
    HRESULT hr = S_OK;
    if (spStreamSinkToPause)
    {
        hr = spStreamSinkToPause->Pause();
    }

    return hr;
}

STDMETHODIMP CMediaSink::OnClockRestart(MFTIME hnsSystemTime)
{
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    
    // Capture pointers and check shutdown state under lock
    CComPtr<CStreamSink> spStreamSinkToRestart;
    
    {
        CAutoLock lock(pSharedCritSec);
        
        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            return hr;
        }
        
        spStreamSinkToRestart = m_spStreamSink;
    }
    // Lock released here
    
    // Restart stream sink WITHOUT holding the shared critical section
    HRESULT hr = S_OK;
    if (spStreamSinkToRestart)
    {
        hr = spStreamSinkToRestart->Restart();
    }

    return hr;
}

STDMETHODIMP CMediaSink::OnClockSetRate(MFTIME hnsSystemTime, float flRate)
{
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    
    // Capture pointers and check shutdown state under lock
    CComPtr<CStreamSink> spStreamSinkToSetRate;
    
    {
        CAutoLock lock(pSharedCritSec);
        
        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            return hr;
        }
        
        spStreamSinkToSetRate = m_spStreamSink;
    }
    // Lock released here
    
    // Set clock rate on stream sink WITHOUT holding the shared critical section
    HRESULT hr = S_OK;
    if (spStreamSinkToSetRate)
    {
        hr = spStreamSinkToSetRate->SetClockRate(flRate);
    }

    return hr;
}

// IMFMediaSinkPreroll

STDMETHODIMP CMediaSink::NotifyPreroll(MFTIME hnsUpcomingStartTime)
{
    CCritSec* pSharedCritSec = GetSharedCritSec();
    if (!pSharedCritSec)
    {
        return MF_E_SHUTDOWN;
    }
    CAutoLock lock(pSharedCritSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (m_spStreamSink)
    {
        hr = m_spStreamSink->Preroll();
    }

    return hr;
}

// IMFGetService

STDMETHODIMP CMediaSink::GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject)
{
    //DebugLog("[DX11MediaSink] GetService called, guidService=%08X-%04X, riid=%08X-%04X\n",
    //         guidService.Data1, guidService.Data2, riid.Data1, riid.Data2);
    
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        DebugLog("[DX11MediaSink] GetService: shutdown, hr=0x%08X\n", hr);
        return hr;
    }

    if (!ppvObject)
    {
        return E_POINTER;
    }

    *ppvObject = nullptr;

    if (guidService == MR_VIDEO_RENDER_SERVICE)
    {
        DebugLog("[DX11MediaSink] GetService: MR_VIDEO_RENDER_SERVICE\n");
        // Return the presenter for video display control services
        if (m_spPresenter)
        {
            hr = m_spPresenter->QueryInterface(riid, ppvObject);
            DebugLog("[DX11MediaSink] GetService: returned presenter, hr=0x%08X\n", hr);
            return hr;
        }
        DebugLog("[DX11MediaSink] GetService: no presenter!\n");
        return E_UNEXPECTED;
    }
    else if (guidService == MR_VIDEO_ACCELERATION_SERVICE)
    {
        DebugLog("[DX11MediaSink] GetService: MR_VIDEO_ACCELERATION_SERVICE\n");
        // Return the presenter for acceleration services (DXGI device manager)
        if (m_spPresenter)
        {
            hr = m_spPresenter->GetService(guidService, riid, ppvObject);
            DebugLog("[DX11MediaSink] GetService: forwarded to presenter, hr=0x%08X, ppvObject=%p\n", hr, *ppvObject);
            return hr;
        }
        DebugLog("[DX11MediaSink] GetService: no presenter!\n");
        return E_UNEXPECTED;
    }

    DebugLog("[DX11MediaSink] GetService: unsupported service\n");
    return MF_E_UNSUPPORTED_SERVICE;
}

// IMFRateSupport

STDMETHODIMP CMediaSink::GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float* pflRate)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pflRate)
    {
        return E_POINTER;
    }

    *pflRate = 0.0f;
    return S_OK;
}

STDMETHODIMP CMediaSink::GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float* pflRate)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pflRate)
    {
        return E_POINTER;
    }

    // Support up to 8x playback
    *pflRate = (eDirection == MFRATE_FORWARD) ? 8.0f : -8.0f;
    return S_OK;
}

STDMETHODIMP CMediaSink::IsRateSupported(BOOL fThin, float flRate, float* pflNearestSupportedRate)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (pflNearestSupportedRate)
    {
        *pflNearestSupportedRate = flRate;
    }

    // Support any rate between -8 and 8
    if (flRate < -8.0f || flRate > 8.0f)
    {
        return MF_E_UNSUPPORTED_RATE;
    }

    return S_OK;
}

// IMFVideoDisplayControl

STDMETHODIMP CMediaSink::GetNativeVideoSize(SIZE* pszVideo, SIZE* pszARVideo)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (m_spPresenter)
    {
        UINT32 width = 0, height = 0;
        m_spPresenter->GetVideoSize(&width, &height);

        if (pszVideo)
        {
            pszVideo->cx = static_cast<LONG>(width);
            pszVideo->cy = static_cast<LONG>(height);
        }
        if (pszARVideo)
        {
            // Assume square pixels for now
            pszARVideo->cx = static_cast<LONG>(width);
            pszARVideo->cy = static_cast<LONG>(height);
        }
    }

    return S_OK;
}

STDMETHODIMP CMediaSink::GetIdealVideoSize(SIZE* pszMin, SIZE* pszMax)
{
    // Use native size as ideal
    return GetNativeVideoSize(pszMax, nullptr);
}

STDMETHODIMP CMediaSink::SetVideoPosition(const MFVideoNormalizedRect* pnrcSource, const LPRECT prcDest)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (pnrcSource)
    {
        m_nrcSource = *pnrcSource;
    }

    if (prcDest)
    {
        m_rcDest = *prcDest;

        if (m_spPresenter)
        {
            m_spPresenter->SetDestinationRect(m_rcDest);
        }
    }

    return S_OK;
}

STDMETHODIMP CMediaSink::GetVideoPosition(MFVideoNormalizedRect* pnrcSource, LPRECT prcDest)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (pnrcSource)
    {
        *pnrcSource = m_nrcSource;
    }

    if (prcDest)
    {
        *prcDest = m_rcDest;
    }

    return S_OK;
}

STDMETHODIMP CMediaSink::SetAspectRatioMode(DWORD dwAspectRatioMode)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_dwAspectRatioMode = dwAspectRatioMode;
    return S_OK;
}

STDMETHODIMP CMediaSink::GetAspectRatioMode(DWORD* pdwAspectRatioMode)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pdwAspectRatioMode)
    {
        return E_POINTER;
    }

    *pdwAspectRatioMode = m_dwAspectRatioMode;
    return S_OK;
}

STDMETHODIMP CMediaSink::SetVideoWindow(HWND hwndVideo)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_hwndVideo = hwndVideo;

    if (m_spPresenter)
    {
        hr = m_spPresenter->SetVideoWindow(hwndVideo);
    }

    return hr;
}

STDMETHODIMP CMediaSink::GetVideoWindow(HWND* phwndVideo)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!phwndVideo)
    {
        return E_POINTER;
    }

    *phwndVideo = m_hwndVideo;
    return S_OK;
}

STDMETHODIMP CMediaSink::RepaintVideo()
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (m_spPresenter)
    {
        hr = m_spPresenter->PresentFrame();
    }

    return hr;
}

STDMETHODIMP CMediaSink::GetCurrentImage(BITMAPINFOHEADER* pBih, BYTE** pDib, DWORD* pcbDib, LONGLONG* pTimeStamp)
{
    // Not implemented
    return E_NOTIMPL;
}

STDMETHODIMP CMediaSink::SetBorderColor(COLORREF Clr)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_clrBorder = Clr;
    return S_OK;
}

STDMETHODIMP CMediaSink::GetBorderColor(COLORREF* pClr)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pClr)
    {
        return E_POINTER;
    }

    *pClr = m_clrBorder;
    return S_OK;
}

STDMETHODIMP CMediaSink::SetRenderingPrefs(DWORD dwRenderFlags)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_dwRenderingPrefs = dwRenderFlags;

    // Custom extension for this renderer:
    // low 16 bits: sharpen strength milli-units in [0..1000] => [0..1]
    // high 16 bits: threshold milli-units in [0..1000] => [0..0.02]
    float slider = static_cast<float>(dwRenderFlags & 0xFFFFu) / 1000.0f;
    float thresholdNorm = static_cast<float>((dwRenderFlags >> 16) & 0xFFFFu) / 1000.0f;
    if (slider < 0.0f)
    {
        slider = 0.0f;
    }
    if (slider > 1.0f)
    {
        slider = 1.0f;
    }

    if (thresholdNorm < 0.0f)
    {
        thresholdNorm = 0.0f;
    }
    if (thresholdNorm > 1.0f)
    {
        thresholdNorm = 1.0f;
    }
    float threshold = thresholdNorm * 0.02f;
    DebugLog("[DX11MediaSink] SetRenderingPrefs: raw=0x%08X strength=%.3f threshold=%.4f\n", dwRenderFlags, slider, threshold);

    if (m_spPresenter)
    {
        m_spPresenter->SetUserSharpenSliderValue(slider);
        m_spPresenter->SetUserSharpenThreshold(threshold);
    }

    return S_OK;
}

STDMETHODIMP CMediaSink::GetRenderingPrefs(DWORD* pdwRenderFlags)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pdwRenderFlags)
    {
        return E_POINTER;
    }

    *pdwRenderFlags = m_dwRenderingPrefs;
    return S_OK;
}

STDMETHODIMP CMediaSink::SetFullscreen(BOOL fFullscreen)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_bFullscreen = fFullscreen;

    if (m_spPresenter)
    {
        hr = m_spPresenter->SetFullscreen(fFullscreen);
    }

    return hr;
}

STDMETHODIMP CMediaSink::GetFullscreen(BOOL* pfFullscreen)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pfFullscreen)
    {
        return E_POINTER;
    }

    *pfFullscreen = m_bFullscreen;
    return S_OK;
}

// Private

HRESULT CMediaSink::CheckShutdown() const
{
    if (m_bShutdown)
    {
        return MF_E_SHUTDOWN;
    }
    return S_OK;
}
