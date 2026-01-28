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

#include "StreamSink.h"
#include "MediaSink.h"
#include <stdio.h>

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

// Supported video formats
static const GUID* g_VideoFormats[] = {
    &MFVideoFormat_NV12,
    &MFVideoFormat_YUY2,
    &MFVideoFormat_UYVY,
    &MFVideoFormat_RGB32,
    &MFVideoFormat_ARGB32,
    &MFVideoFormat_RGB24,
    &MFVideoFormat_I420,
    &MFVideoFormat_IYUV,
    &MFVideoFormat_YV12,
};

static const DWORD g_NumVideoFormats = ARRAYSIZE(g_VideoFormats);

//-----------------------------------------------------------------------------
// CStreamSink
//-----------------------------------------------------------------------------

HRESULT CStreamSink::CreateInstance(
    CMediaSink* pParent,
    DWORD dwStreamId,
    CPresenter* pPresenter,
    CScheduler* pScheduler,
    CCritSec& critSec,
    CStreamSink** ppStreamSink)
{
    if (!pParent || !pPresenter || !pScheduler || !ppStreamSink)
    {
        return E_POINTER;
    }

    *ppStreamSink = nullptr;

    CStreamSink* pStreamSink = DBG_NEW CStreamSink(pParent, dwStreamId, pPresenter, pScheduler, critSec);
    if (!pStreamSink)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pStreamSink->Initialize();
    if (FAILED(hr))
    {
        delete pStreamSink;
        return hr;
    }

    *ppStreamSink = pStreamSink;
    // Constructor sets refcount to 1, so don't AddRef again

    return S_OK;
}

CStreamSink::CStreamSink(CMediaSink* pParent, DWORD dwStreamId, CPresenter* pPresenter, CScheduler* pScheduler, CCritSec& critSec) :
    m_nRefCount(1),
    m_pParent(pParent),
    m_dwStreamId(dwStreamId),
    m_pPresenter(pPresenter),
    m_pScheduler(pScheduler),
    m_pEventQueue(nullptr),
    m_pCurrentType(nullptr),
    m_state(RENDER_STATE_STOPPED),
    m_bPrerolling(FALSE),
    m_bWaitingForOnClockStart(FALSE),
    m_bStarted(FALSE),
    m_fRate(1.0f),
    m_dwSamplesRequested(0),
    m_dwSamplesOutstanding(0)
{
    // Note: critSec parameter is passed in but NOT used anymore (using own m_critSec instead)
    if (m_pPresenter)
    {
        m_pPresenter->AddRef();
    }
    // Note: m_pScheduler is owned by CMediaSink, not AddRef'd here
}

CStreamSink::~CStreamSink()
{
    //DebugLog("[DX11StreamSink] Destructor called\n");
    SafeRelease(m_pPresenter);
    SafeRelease(m_pEventQueue);
    SafeRelease(m_pCurrentType);
    
    // Note: m_pScheduler is owned by CMediaSink, don't delete here
    m_pScheduler = nullptr;
    //DebugLog("[DX11StreamSink] Destructor complete\n");
}

HRESULT CStreamSink::Initialize()
{
    HRESULT hr = MFCreateEventQueue(&m_pEventQueue);
    if (FAILED(hr))
    {
        return hr;
    }

    // Note: m_pScheduler is set in constructor, callback is set by MediaSink

    return S_OK;
}

// IUnknown

STDMETHODIMP CStreamSink::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    if (riid == IID_IUnknown)
    {
        *ppv = static_cast<IUnknown*>(static_cast<IMFStreamSink*>(this));
    }
    else if (riid == IID_IMFStreamSink)
    {
        *ppv = static_cast<IMFStreamSink*>(this);
    }
    else if (riid == IID_IMFMediaEventGenerator)
    {
        *ppv = static_cast<IMFMediaEventGenerator*>(this);
    }
    else if (riid == IID_IMFMediaTypeHandler)
    {
        *ppv = static_cast<IMFMediaTypeHandler*>(this);
    }
    else if (riid == IID_IMFGetService)
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

STDMETHODIMP_(ULONG) CStreamSink::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CStreamSink::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

// IMFMediaEventGenerator

STDMETHODIMP CStreamSink::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    return m_pEventQueue->GetEvent(dwFlags, ppEvent);
}

STDMETHODIMP CStreamSink::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    return m_pEventQueue->BeginGetEvent(pCallback, punkState);
}

STDMETHODIMP CStreamSink::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    return m_pEventQueue->EndGetEvent(pResult, ppEvent);
}

STDMETHODIMP CStreamSink::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    return m_pEventQueue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue);
}

// IMFStreamSink

STDMETHODIMP CStreamSink::GetMediaSink(IMFMediaSink** ppMediaSink)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppMediaSink)
    {
        return E_POINTER;
    }

    *ppMediaSink = static_cast<IMFMediaSink*>(m_pParent);
    (*ppMediaSink)->AddRef();

    return S_OK;
}

STDMETHODIMP CStreamSink::GetIdentifier(DWORD* pdwIdentifier)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pdwIdentifier)
    {
        return E_POINTER;
    }

    *pdwIdentifier = m_dwStreamId;
    return S_OK;
}

STDMETHODIMP CStreamSink::GetMediaTypeHandler(IMFMediaTypeHandler** ppHandler)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppHandler)
    {
        return E_POINTER;
    }

    *ppHandler = static_cast<IMFMediaTypeHandler*>(this);
    (*ppHandler)->AddRef();

    return S_OK;
}

STDMETHODIMP CStreamSink::ProcessSample(IMFSample* pSample)
{
    CAutoLock lock(&m_critSec);
    //
    //DebugLog("[DX11Renderer] ProcessSample called, state=%d, prerolling=%d, waitingForClock=%d\n", 
    //         m_state, m_bPrerolling, m_bWaitingForOnClockStart);
    //
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        DebugLog("[DX11Renderer] ProcessSample: shutdown, hr=0x%08X\n", hr);
        return hr;
    }

    if (!pSample)
    {
        DebugLog("[DX11Renderer] ProcessSample: null sample\n");
        return E_POINTER;
    }

    // Skip validation during preroll or when waiting for clock start
    // This matches Microsoft's implementation - samples are queued during preroll
    if (!m_bPrerolling && !m_bWaitingForOnClockStart)
    {
        hr = ValidateOperation(m_state);
        if (FAILED(hr))
        {
            DebugLog("[DX11Renderer] ProcessSample: invalid operation, hr=0x%08X\n", hr);
            return hr;
        }
    }

    if (m_dwSamplesOutstanding > 0)
    {
        m_dwSamplesOutstanding--;
    }

    hr = ProcessSampleInternal(pSample);
    //DebugLog("[DX11Renderer] ProcessSample: ProcessSampleInternal returned 0x%08X\n", hr);
    return hr;
}

STDMETHODIMP CStreamSink::PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT* pvarMarkerValue, const PROPVARIANT* pvarContextValue)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    // Queue marker event
    PROPVARIANT var = {0};
    if (pvarContextValue)
    {
        PropVariantCopy(&var, pvarContextValue);
    }

    hr = m_pEventQueue->QueueEventParamVar(MEStreamSinkMarker, GUID_NULL, S_OK, &var);
    PropVariantClear(&var);

    return hr;
}

STDMETHODIMP CStreamSink::Flush()
{
    DWORD dwThreadId = GetCurrentThreadId();
    //DebugLog("[DX11StreamSink] Flush: ========== START FLUSH (Thread 0x%X) ==========\n", dwThreadId);
    
    CScheduler* pSchedulerToFlush = nullptr;
    
    // Acquire lock only to check state and capture scheduler pointer
    {
        CAutoLock lock(&m_critSec);
        //DebugLog("[DX11StreamSink] Flush: Lock acquired\n");
        
        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            DebugLog("[DX11StreamSink] Flush: Shutdown check failed, hr=0x%08X\n", hr);
            return hr;
        }

        //DebugLog("[DX11StreamSink] Flush: Current state: %d, samples outstanding: %ld\n", m_state, m_dwSamplesOutstanding);

        // Capture scheduler pointer and reset sample counters under lock
        pSchedulerToFlush = m_pScheduler;
        m_dwSamplesRequested = 0;
        m_dwSamplesOutstanding = 0;
        
        //DebugLog("[DX11StreamSink] Flush: Sample counters reset\n");
        //DebugLog("[DX11StreamSink] Flush: Lock will be released before flushing scheduler\n");
    }
    // Lock is released here
    
    // Now flush the scheduler without holding the critical section lock
    // This prevents deadlock if the scheduler tries to call back into StreamSink methods
    if (pSchedulerToFlush)
    {
        //DebugLog("[DX11StreamSink] Flush: Flushing scheduler to clear pending samples (lock NOT held)\n");
        pSchedulerToFlush->Flush();
        //DebugLog("[DX11StreamSink] Flush: Scheduler flushed successfully\n");
    }
    else
    {
        DebugLog("[DX11StreamSink] Flush: WARNING - Scheduler is NULL!\n");
    }
    
    //DebugLog("[DX11StreamSink] Flush: ========== FLUSH COMPLETE (Thread 0x%X) ==========\n", dwThreadId);

    return S_OK;
}

// IMFMediaTypeHandler

STDMETHODIMP CStreamSink::IsMediaTypeSupported(IMFMediaType* pMediaType, IMFMediaType** ppMediaType)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pMediaType)
    {
        return E_POINTER;
    }

    if (ppMediaType)
    {
        *ppMediaType = nullptr;
    }

    return ValidateMediaType(pMediaType);
}

STDMETHODIMP CStreamSink::GetMediaTypeCount(DWORD* pdwTypeCount)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pdwTypeCount)
    {
        return E_POINTER;
    }

    *pdwTypeCount = g_NumVideoFormats;
    return S_OK;
}

STDMETHODIMP CStreamSink::GetMediaTypeByIndex(DWORD dwIndex, IMFMediaType** ppType)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppType)
    {
        return E_POINTER;
    }

    if (dwIndex >= g_NumVideoFormats)
    {
        return MF_E_NO_MORE_TYPES;
    }

    // Create media type for this format
    hr = MFCreateMediaType(ppType);
    if (SUCCEEDED(hr))
    {
        hr = (*ppType)->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    }
    if (SUCCEEDED(hr))
    {
        hr = (*ppType)->SetGUID(MF_MT_SUBTYPE, *g_VideoFormats[dwIndex]);
    }

    return hr;
}

STDMETHODIMP CStreamSink::SetCurrentMediaType(IMFMediaType* pMediaType)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pMediaType)
    {
        return E_POINTER;
    }

    hr = ValidateMediaType(pMediaType);
    if (FAILED(hr))
    {
        return hr;
    }

    SafeRelease(m_pCurrentType);
    m_pCurrentType = pMediaType;
    m_pCurrentType->AddRef();

    // Configure presenter with new type
    if (m_pPresenter)
    {
        hr = m_pPresenter->SetMediaType(pMediaType);
    }

    return hr;
}

STDMETHODIMP CStreamSink::GetCurrentMediaType(IMFMediaType** ppMediaType)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!ppMediaType)
    {
        return E_POINTER;
    }

    if (!m_pCurrentType)
    {
        return MF_E_NOT_INITIALIZED;
    }

    *ppMediaType = m_pCurrentType;
    (*ppMediaType)->AddRef();

    return S_OK;
}

STDMETHODIMP CStreamSink::GetMajorType(GUID* pguidMajorType)
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    if (!pguidMajorType)
    {
        return E_POINTER;
    }

    *pguidMajorType = MFMediaType_Video;
    return S_OK;
}

// IMFGetService

STDMETHODIMP CStreamSink::GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject)
{
    //DebugLog("[DX11StreamSink] GetService called, guidService=%08X-%04X, riid=%08X-%04X\n",
    //         guidService.Data1, guidService.Data2, riid.Data1, riid.Data2);
    
    if (!ppvObject)
    {
        return E_POINTER;
    }
    
    *ppvObject = nullptr;
    
    // Forward to the MediaSink's GetService
    if (m_pParent)
    {
        IMFGetService* pGetService = nullptr;
        HRESULT hr = m_pParent->QueryInterface(IID_PPV_ARGS(&pGetService));
        if (SUCCEEDED(hr))
        {
            hr = pGetService->GetService(guidService, riid, ppvObject);
            pGetService->Release();
            
            //DebugLog("[DX11StreamSink] GetService: forwarded to MediaSink, hr=0x%08X\n", hr);
            return hr;
        }
    }
    
    DebugLog("[DX11StreamSink] GetService: no parent, returning E_NOINTERFACE\n");
    return E_NOINTERFACE;
}

// ISchedulerCallback

void CStreamSink::OnSampleReady(IMFSample* pSample)
{
    // Process sample in presenter
    if (m_pPresenter && pSample)
    {
        m_pPresenter->ProcessFrame(pSample);
    }
}

void CStreamSink::OnSampleFree(IMFSample* pSample)
{
    // Don't request more samples if we're shutting down
    if (m_state == RENDER_STATE_SHUTDOWN)
    {
        return;
    }
    
    // Request more samples if needed
    RequestSample();
}

// State management

HRESULT CStreamSink::Start(MFTIME start)
{
    //DebugLog("[DX11StreamSink] Start() called, current state=%d\n", m_state);
    
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        DebugLog("[DX11StreamSink] Start(): shutdown, hr=0x%08X\n", hr);
        return hr;
    }

    m_state = RENDER_STATE_STARTED;
    m_bStarted = TRUE;
    m_bPrerolling = FALSE;
    m_bWaitingForOnClockStart = FALSE;  // Clock has started, can process samples now

    //DebugLog("[DX11StreamSink] Start(): state set to STARTED (%d)\n", m_state);

    // Note: Scheduler is started by MediaSink::OnClockStart

    // Request initial samples
    for (DWORD i = 0; i < MAX_SAMPLES_REQUESTED; i++)
    {
        RequestSample();
    }

    hr = QueueEvent(MEStreamSinkStarted, GUID_NULL, S_OK, nullptr);
    //DebugLog("[DX11StreamSink] Start(): completed, hr=0x%08X\n", hr);

    return hr;
}

HRESULT CStreamSink::Stop()
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_state = RENDER_STATE_STOPPED;
    m_bStarted = FALSE;

    // Note: Scheduler is stopped by MediaSink::OnClockStop

    hr = QueueEvent(MEStreamSinkStopped, GUID_NULL, S_OK, nullptr);

    return hr;
}

HRESULT CStreamSink::Pause()
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_state = RENDER_STATE_PAUSED;

    hr = QueueEvent(MEStreamSinkPaused, GUID_NULL, S_OK, nullptr);

    return hr;
}

HRESULT CStreamSink::Restart()
{
    CAutoLock lock(&m_critSec);
    
    HRESULT hr = CheckShutdown();
    if (FAILED(hr))
    {
        return hr;
    }

    m_state = RENDER_STATE_STARTED;

    hr = QueueEvent(MEStreamSinkStarted, GUID_NULL, S_OK, nullptr);

    return hr;
}

HRESULT CStreamSink::Shutdown()
{
    DWORD dwThreadId = GetCurrentThreadId();
    //DebugLog("[DX11StreamSink] Shutdown: ============ START SHUTDOWN (Thread 0x%X) ============\n", dwThreadId);
    //DebugLog("[DX11StreamSink] Shutdown: Current state: %d\n", m_state);
    //DebugLog("[DX11StreamSink] Shutdown: Ref count: %ld\n", m_nRefCount);

    // Try to acquire the critical section lock
    //DebugLog("[DX11StreamSink] Shutdown: Attempting to acquire critical section lock...\n");
    
    {
        CAutoLock lock(&m_critSec);
        //DebugLog("[DX11StreamSink] Shutdown: Successfully acquired critical section lock!\n");
        
        // Before modifying state, log current state
        //DebugLog("[DX11StreamSink] Shutdown: Setting state from %d to RENDER_STATE_SHUTDOWN\n", m_state);
        m_state = RENDER_STATE_SHUTDOWN;
        //DebugLog("[DX11StreamSink] Shutdown: State changed to RENDER_STATE_SHUTDOWN\n");

        // Note: Scheduler is stopped/flushed by MediaSink::Shutdown
        //DebugLog("[DX11StreamSink] Shutdown: Note - Scheduler is stopped/flushed by MediaSink::Shutdown\n");

        if (m_pEventQueue)
        {
            //DebugLog("[DX11StreamSink] Shutdown: Shutting down event queue (%p)\n", m_pEventQueue);
            m_pEventQueue->Shutdown();
            //DebugLog("[DX11StreamSink] Shutdown: Event queue shutdown complete\n");
        }
        else
        {
            //DebugLog("[DX11StreamSink] Shutdown: Event queue is NULL\n");
        }

        //DebugLog("[DX11StreamSink] Shutdown: Releasing event queue (%p)\n", m_pEventQueue);
        SafeRelease(m_pEventQueue);
        //DebugLog("[DX11StreamSink] Shutdown: Event queue released\n");
        
        //DebugLog("[DX11StreamSink] Shutdown: Releasing current media type (%p)\n", m_pCurrentType);
        SafeRelease(m_pCurrentType);
        //DebugLog("[DX11StreamSink] Shutdown: Current media type released\n");
        
        //DebugLog("[DX11StreamSink] Shutdown: Lock will be released when exiting scope\n");
    }
    
    //DebugLog("[DX11StreamSink] Shutdown: Critical section lock released\n");
    //DebugLog("[DX11StreamSink] Shutdown: ============ SHUTDOWN COMPLETE (Thread 0x%X) ============\n", dwThreadId);

    return S_OK;
}

HRESULT CStreamSink::SetClockRate(float fRate)
{
    m_fRate = fRate;
    return S_OK;
}

HRESULT CStreamSink::Preroll()
{
    CAutoLock lock(&m_critSec);
    
    //DebugLog("[DX11StreamSink] Preroll() called\n");
    
    m_bPrerolling = TRUE;
    m_bWaitingForOnClockStart = TRUE;  // Wait for OnClockStart before processing samples

    // Request samples for preroll
    for (DWORD i = 0; i < MAX_SAMPLES_REQUESTED; i++)
    {
        RequestSample();
    }

    //DebugLog("[DX11StreamSink] Preroll(): requested %d samples\n", MAX_SAMPLES_REQUESTED);
    return S_OK;
}

BOOL CStreamSink::NeedMoreSamples()
{
    CAutoLock lock(&m_critSec);
    return (m_dwSamplesOutstanding < MAX_SAMPLES_REQUESTED);
}

// Private methods

HRESULT CStreamSink::CheckShutdown() const
{
    if (m_state == RENDER_STATE_SHUTDOWN)
    {
        return MF_E_SHUTDOWN;
    }
    return S_OK;
}

HRESULT CStreamSink::ValidateOperation(RENDER_STATE state)
{
    switch (state)
    {
    case RENDER_STATE_STARTED:
    case RENDER_STATE_PAUSED:
        return S_OK;
    case RENDER_STATE_STOPPED:
        return MF_E_INVALIDREQUEST;
    case RENDER_STATE_SHUTDOWN:
        return MF_E_SHUTDOWN;
    }
    return E_UNEXPECTED;
}

HRESULT CStreamSink::ProcessSampleInternal(IMFSample* pSample)
{
    HRESULT hr = S_OK;

    if (!pSample)
    {
        return E_POINTER;
    }

    // If prerolling, signal completion
    if (m_bPrerolling)
    {
        m_bPrerolling = FALSE;
        hr = QueueEvent(MEStreamSinkPrerolled, GUID_NULL, S_OK, nullptr);
    }

    // Schedule sample for presentation
    if (m_pScheduler)
    {
        BOOL bPresentNow = (m_fRate < 0.0f) || (m_state == RENDER_STATE_PAUSED);
        hr = m_pScheduler->ScheduleSample(pSample, bPresentNow);
    }

    return hr;
}

HRESULT CStreamSink::RequestSample()
{
    CAutoLock lock(&m_critSec);
    
    // Check shutdown state under lock
    if (m_state == RENDER_STATE_SHUTDOWN)
    {
        return MF_E_SHUTDOWN;
    }
    
    // Make sure event queue is still valid
    if (!m_pEventQueue)
    {
        return MF_E_SHUTDOWN;
    }

    HRESULT hr = S_OK;
    if (m_dwSamplesOutstanding < MAX_SAMPLES_REQUESTED)
    {
        hr = m_pEventQueue->QueueEventParamVar(MEStreamSinkRequestSample, GUID_NULL, S_OK, nullptr);
        if (SUCCEEDED(hr))
        {
            m_dwSamplesOutstanding++;
        }
    }

    return hr;
}

HRESULT CStreamSink::ValidateMediaType(IMFMediaType* pMediaType)
{
    HRESULT hr = S_OK;
    GUID majorType = GUID_NULL;
    GUID subType = GUID_NULL;

    hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
    if (FAILED(hr))
    {
        return hr;
    }

    if (majorType != MFMediaType_Video)
    {
        return MF_E_INVALIDMEDIATYPE;
    }

    hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subType);
    if (FAILED(hr))
    {
        return hr;
    }

    // Check if subtype is supported
    for (DWORD i = 0; i < g_NumVideoFormats; i++)
    {
        if (subType == *g_VideoFormats[i])
        {
            return S_OK;
        }
    }

    return MF_E_INVALIDMEDIATYPE;
}

BOOL CStreamSink::IsValidVideoType(IMFMediaType* pMediaType)
{
    return SUCCEEDED(ValidateMediaType(pMediaType));
}
