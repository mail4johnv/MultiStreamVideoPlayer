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

#include "Scheduler.h"
#include "Presenter.h"
#include <stdio.h>
#include <cmath>

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
// CScheduler
//-----------------------------------------------------------------------------

CScheduler::CScheduler(CPresenter* pPresenter) :
    m_pPresenter(pPresenter),
    m_pCallback(nullptr),
    m_pClock(nullptr),
    m_hThread(nullptr),
    m_hThreadReadyEvent(nullptr),
    m_hFlushEvent(nullptr),
    m_hSampleReadyEvent(nullptr),
    m_dwThreadID(0),
    m_bStarted(FALSE),
    m_bThreadRunning(FALSE),
    m_dwFrameRate(30),
    m_fRate(1.0f),
    m_PerFrameInterval(333333),   // Default: 30fps = ~33.3ms = 333333 * 100ns
    m_PerFrame_1_4th(83333),      // 1/4 of frame
    m_dwQueueHead(0),
    m_dwQueueTail(0),
    m_dwQueueCount(0)
{
    ZeroMemory(m_sampleQueue, sizeof(m_sampleQueue));
    
    m_hFlushEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_hThreadReadyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_hSampleReadyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

CScheduler::~CScheduler()
{
    Stop();
    Flush();
    
    if (m_hFlushEvent)
    {
        CloseHandle(m_hFlushEvent);
    }
    if (m_hThreadReadyEvent)
    {
        CloseHandle(m_hThreadReadyEvent);
    }
    if (m_hSampleReadyEvent)
    {
        CloseHandle(m_hSampleReadyEvent);
    }
    
    SafeRelease(m_pClock);
}

HRESULT CScheduler::Start(IMFClock* pClock)
{
    CAutoLock lock(&m_critSec);
    
    //DebugLog("[DX11Scheduler] Start called, clock=0x%p\n", pClock);
    
    // Store clock reference
    SafeRelease(m_pClock);
    m_pClock = pClock;
    if (m_pClock)
    {
        m_pClock->AddRef();
    }
    
    // Start scheduler thread if not running
    if (!m_hThread)
    {
        m_bThreadRunning = TRUE;
        m_hThread = CreateThread(nullptr, 0, SchedulerThreadProc, this, 0, &m_dwThreadID);
        
        if (!m_hThread)
        {
            m_bThreadRunning = FALSE;
            DebugLog("[DX11Scheduler] Start: failed to create thread\n");
            return HRESULT_FROM_WIN32(GetLastError());
        }
        
        // Wait for thread to be ready
        WaitForSingleObject(m_hThreadReadyEvent, 5000);
        DebugLog("[DX11Scheduler] Start: thread started, tid=%d\n", m_dwThreadID);
    }
    
    m_bStarted = TRUE;
    //DebugLog("[DX11Scheduler] Start: SUCCESS\n");
    return S_OK;
}

HRESULT CScheduler::Stop()
{
    //DebugLog("[DX11Scheduler] Stop: entering\n");
    
    // CRITICAL DEADLOCK FIX: Set m_bThreadRunning=FALSE FIRST, without holding lock
    // The scheduler thread checks this flag at the top of its loop
    // This prevents deadlock if the scheduler thread is stuck holding m_critSec
    m_bThreadRunning = FALSE;
    //DebugLog("[DX11Scheduler] Stop: set m_bThreadRunning=FALSE (without lock)\n");
    
    // Signal the thread to wake up and check m_bThreadRunning
    if (m_hFlushEvent)
    {
        SetEvent(m_hFlushEvent);
        //DebugLog("[DX11Scheduler] Stop: signaled flush event\n");
    }
    
    HANDLE hThread = nullptr;
    
    // CRITICAL: Try to acquire lock with timeout to avoid deadlock
    // If the scheduler thread is stuck in deep recursion, we can't wait forever
    //DebugLog("[DX11Scheduler] Stop: attempting to acquire lock (timeout approach)\n");
    
    BOOL lockAcquired = FALSE;
    DWORD attempts = 0;
    const DWORD MAX_ATTEMPTS = 10;
    
    while (!lockAcquired && attempts < MAX_ATTEMPTS)
    {
        // Try to acquire lock briefly
        if (TryEnterCriticalSection(&m_critSec.GetRawCritSec()))
        {
            lockAcquired = TRUE;
            //DebugLog("[DX11Scheduler] Stop: lock acquired on attempt %d\n", attempts + 1);
            
            m_bStarted = FALSE;
            
            // Capture thread handle
            if (m_hThread)
            {
                //DebugLog("[DX11Scheduler] Stop: capturing thread handle for wait\n");
                hThread = m_hThread;
                m_hThread = nullptr;
                m_dwThreadID = 0;
            }
            else
            {
                DebugLog("[DX11Scheduler] Stop: no thread to stop\n");
            }
            
            LeaveCriticalSection(&m_critSec.GetRawCritSec());
            break;
        }
        else
        {
            attempts++;
            DebugLog("[DX11Scheduler] Stop: lock busy (attempt %d/%d), retrying...\n", attempts, MAX_ATTEMPTS);
            Sleep(50); // Wait 50ms before retry
        }
    }
    
    if (!lockAcquired)
    {
        DebugLog("[DX11Scheduler] Stop: CRITICAL - Could not acquire lock after %d attempts!\n", MAX_ATTEMPTS);
        DebugLog("[DX11Scheduler] Stop: Scheduler thread may be stuck in deep recursion\n");
        DebugLog("[DX11Scheduler] Stop: Forcing thread termination\n");
        
        // Last resort: terminate the thread
        if (m_hThread)
        {
            DebugLog("[DX11Scheduler] Stop: Terminating stuck scheduler thread\n");
            TerminateThread(m_hThread, 1);
            CloseHandle(m_hThread);
            m_hThread = nullptr;
            m_dwThreadID = 0;
        }
        return S_OK;
    }
    
    // Wait for thread to exit (outside lock to prevent deadlock)
    if (hThread)
    {
        //DebugLog("[DX11Scheduler] Stop: waiting for thread to exit...\n");
        DWORD waitResult = WaitForSingleObject(hThread, 5000);
        if (waitResult == WAIT_TIMEOUT)
        {
            DebugLog("[DX11Scheduler] Stop: WARNING - thread did not exit within 5 seconds, terminating\n");
            TerminateThread(hThread, 1);
        }
        else
        {
            //DebugLog("[DX11Scheduler] Stop: thread exited normally\n");
        }
        CloseHandle(hThread);
    }
    
    {
        CAutoLock lock(&m_critSec);
        SafeRelease(m_pClock);
    }
    
    //DebugLog("[DX11Scheduler] Stop: complete\n");
    return S_OK;
}

HRESULT CScheduler::Flush()
{
    CAutoLock lock(&m_critSec);
    
    // Clear sample queue
    for (DWORD i = 0; i < MAX_SAMPLES; i++)
    {
        if (m_sampleQueue[i])
        {
            if (m_pCallback)
            {
                m_pCallback->OnSampleFree(m_sampleQueue[i]);
            }
            SafeRelease(m_sampleQueue[i]);
        }
    }
    
    m_dwQueueHead = 0;
    m_dwQueueTail = 0;
    m_dwQueueCount = 0;
    
    // Signal flush event
    SetEvent(m_hFlushEvent);
    
    // Flush presenter
    if (m_pPresenter)
    {
        m_pPresenter->Flush();
    }
    
    return S_OK;
}

void CScheduler::SetFrameRate(DWORD dwFrameRate)
{
    m_dwFrameRate = dwFrameRate > 0 ? dwFrameRate : 30;
    
    // Calculate frame interval in 100ns units
    // 1 second = 10,000,000 100ns units
    m_PerFrameInterval = 10000000 / m_dwFrameRate;
    m_PerFrame_1_4th = m_PerFrameInterval / 4;
    
    DebugLog("[DX11Scheduler] SetFrameRate: %d fps, interval=%lld, 1/4th=%lld\n", 
             m_dwFrameRate, m_PerFrameInterval, m_PerFrame_1_4th);
}

HRESULT CScheduler::ScheduleSample(IMFSample* pSample, BOOL bPresentNow)
{
    //DebugLog("[DX11Scheduler] ScheduleSample called, bPresentNow=%d\n", bPresentNow);
    
    if (!pSample)
    {
        return E_POINTER;
    }
    
    if (bPresentNow)
    {
        // Present immediately
        //DebugLog("[DX11Scheduler] ScheduleSample: presenting immediately\n");
        return PresentSample(pSample);
    }
    else
    {
        // Queue for scheduled presentation
        //DebugLog("[DX11Scheduler] ScheduleSample: queuing sample\n");
        return QueueSample(pSample);
    }
}

HRESULT CScheduler::QueueSample(IMFSample* pSample)
{
    CAutoLock lock(&m_critSec);
    
    if (m_dwQueueCount >= MAX_SAMPLES)
    {
        return MF_E_BUFFERTOOSMALL;
    }
    
    pSample->AddRef();
    m_sampleQueue[m_dwQueueTail] = pSample;
    m_dwQueueTail = (m_dwQueueTail + 1) % MAX_SAMPLES;
    m_dwQueueCount++;
    
    // Signal scheduler thread that a sample is ready
    SetEvent(m_hSampleReadyEvent);
    
    return S_OK;
}

HRESULT CScheduler::DequeueAndPresent(IMFSample** ppSample)
{
    CAutoLock lock(&m_critSec);
    
    if (m_dwQueueCount == 0)
    {
        return S_FALSE;
    }
    
    *ppSample = m_sampleQueue[m_dwQueueHead];
    m_sampleQueue[m_dwQueueHead] = nullptr;
    m_dwQueueHead = (m_dwQueueHead + 1) % MAX_SAMPLES;
    m_dwQueueCount--;
    
    return S_OK;
}

HRESULT CScheduler::ProcessSamplesInQueue(LONG* plNextSleep)
{
    HRESULT hr = S_OK;
    IMFSample* pSample = nullptr;
    LONG lWait = 0;
    
    *plNextSleep = INFINITE;
    
#ifdef ENABLE_DEADLOCK_DEBUGGING
    // Track recursion depth for debugging
    static thread_local int recursionDepth = 0;
    recursionDepth++;
    
    if (recursionDepth > 3)
    {
        DebugLog("[DX11Scheduler] ProcessSamplesInQueue: WARNING - Recursion depth = %d\n", recursionDepth);
    }
    
    if (recursionDepth > 10)
    {
        DebugLog("[DX11Scheduler] ProcessSamplesInQueue: CRITICAL - Recursion depth = %d, ABORTING!\n", recursionDepth);
        recursionDepth--;
        return E_ABORT;
    }
#endif
    
    // Get next sample
    hr = DequeueAndPresent(&pSample);
    if (hr == S_FALSE || !pSample)
    {
#ifdef ENABLE_DEADLOCK_DEBUGGING
        recursionDepth--;
#endif
        return S_OK; // Queue empty
    }
    
    // Check presentation time
    hr = WaitForSampleTime(pSample, &lWait);
    
    if (lWait > 0)
    {
        // Not ready yet, put back in queue
        {
            CAutoLock lock(&m_critSec);
            
            // Re-queue at head
            m_dwQueueHead = (m_dwQueueHead + MAX_SAMPLES - 1) % MAX_SAMPLES;
            m_sampleQueue[m_dwQueueHead] = pSample;
            m_dwQueueCount++;
        }
        
        *plNextSleep = lWait;
#ifdef ENABLE_DEADLOCK_DEBUGGING
        recursionDepth--;
#endif
        return S_OK;
    }
    
    // Present now
    hr = PresentSample(pSample);
    
    if (m_pCallback)
    {
        m_pCallback->OnSampleFree(pSample);
    }
    
    SafeRelease(pSample);
    
    // Check for more samples
    if (m_dwQueueCount > 0)
    {
        *plNextSleep = 0; // Process immediately
    }
    
#ifdef ENABLE_DEADLOCK_DEBUGGING
    recursionDepth--;
#endif
    
    return hr;
}

HRESULT CScheduler::PresentSample(IMFSample* pSample)
{
    //DebugLog("[DX11Scheduler] PresentSample called\n");
    
#ifdef ENABLE_DEADLOCK_DEBUGGING
    static thread_local int presentDepth = 0;
    presentDepth++;
    
    if (presentDepth > 1)
    {
        DebugLog("[DX11Scheduler] PresentSample: WARNING - Recursive call detected (depth=%d)\n", presentDepth);
    }
    
    if (presentDepth > 5)
    {
        DebugLog("[DX11Scheduler] PresentSample: CRITICAL - Excessive recursion (depth=%d), ABORTING!\n", presentDepth);
        presentDepth--;
        return E_ABORT;
    }
#endif
    
    if (!m_pPresenter)
    {
        DebugLog("[DX11Scheduler] PresentSample: no presenter!\n");
        return E_UNEXPECTED;
    }
    
    // Notify callback
    if (m_pCallback)
    {
        //DebugLog("[DX11Scheduler] PresentSample: calling OnSampleReady\n");
        m_pCallback->OnSampleReady(pSample);
    }
    
    // Present
    //DebugLog("[DX11Scheduler] PresentSample: calling PresentFrame\n");
    HRESULT hr = m_pPresenter->PresentFrame();
    //DebugLog("[DX11Scheduler] PresentSample: PresentFrame hr=0x%08X\n", hr);
    
#ifdef ENABLE_DEADLOCK_DEBUGGING
    presentDepth--;
#endif
    
    return hr;
}

LONGLONG CScheduler::GetPresentationTime()
{
    MFTIME clockTime = 0;
    MFTIME systemTime = 0;
    IMFClock* pClockLocal = nullptr;

    // Snapshot m_pClock under lock and AddRef to extend lifetime
    {
        CAutoLock lock(&m_critSec);
        pClockLocal = m_pClock;
        if (pClockLocal)
        {
            pClockLocal->AddRef();
        }
    }

    if (pClockLocal)
    {
        // Safe to call on local reference
        HRESULT hr = pClockLocal->GetCorrelatedTime(0, &clockTime, &systemTime);
        if (FAILED(hr))
        {
            DebugLog("[DX11Scheduler] GetPresentationTime: GetCorrelatedTime failed hr=0x%08X\n", hr);
            clockTime = 0;
        }
        pClockLocal->Release();
    }
    else
    {
        // Use system time as fallback (when no clock is set)
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        
        clockTime = MFllMulDiv(li.QuadPart, 10000000, freq.QuadPart, 0);
    }
    
    return clockTime;
}

HRESULT CScheduler::WaitForSampleTime(IMFSample* pSample, LONG* plWait)
{
    *plWait = 0;
    if (!pSample) return E_POINTER;

    // Snapshot m_pClock under lock and AddRef to prevent concurrent Release
    IMFClock* pClockLocal = nullptr;
    {
        CAutoLock lock(&m_critSec);
        pClockLocal = m_pClock;
        if (pClockLocal)
        {
            pClockLocal->AddRef();
        }
    }

    // If no clock, present immediately
    if (!pClockLocal)
    {
        return S_OK;
    }

    LONGLONG sampleTime = 0;
    HRESULT hr = pSample->GetSampleTime(&sampleTime);
    if (FAILED(hr))
    {
        pClockLocal->Release();
        return S_OK; // No timestamp, present immediately
    }

    LONGLONG currentTime = GetPresentationTime(); // GetPresentationTime now uses a safe snapshot too
    LONGLONG diff = sampleTime - currentTime;

    // Adjust for reverse playback
    if (m_fRate < 0)
    {
        diff = -diff;
    }
    
    // Use same logic as Microsoft sample:
    // - If sample is more than 1/4 frame late, present immediately
    // - If sample is more than 3/4 frame early, wait
    // - Otherwise, present now (within the acceptable window)
    
    if (diff < -m_PerFrame_1_4th)
    {
        // Sample is late - present immediately
        *plWait = 0;
        //DebugLog("[DX11Scheduler] WaitForSampleTime: late by %lld, presenting now\n", -diff);
    }
    else if (diff > (3 * m_PerFrame_1_4th))
    {
        // Sample is too early - calculate wait time
        LONGLONG waitTime = diff - (3 * m_PerFrame_1_4th);
        
        // Adjust for playback rate
        float absRate = fabsf(m_fRate);
        if (absRate > 0.0f && absRate != 1.0f)
        {
            waitTime = static_cast<LONGLONG>(waitTime / absRate);
        }
        
        // Convert to milliseconds (100ns to ms)
        *plWait = static_cast<LONG>(waitTime / 10000);
        
        // Ensure minimum wait of 1ms if there's any wait
        if (*plWait == 0 && waitTime > 0)
        {
            *plWait = 1;
        }
        
        //DebugLog("[DX11Scheduler] WaitForSampleTime: early by %lld, waiting %d ms\n", diff, *plWait);
    }
    else
    {
        // Within acceptable window - present now
        *plWait = 0;
    }

    pClockLocal->Release();
    return S_OK;
}

DWORD WINAPI CScheduler::SchedulerThreadProc(LPVOID lpParameter)
{
    CScheduler* pScheduler = static_cast<CScheduler*>(lpParameter);
    return pScheduler->SchedulerThreadProcPrivate();
}

DWORD CScheduler::SchedulerThreadProcPrivate()
{
    //DebugLog("[DX11Scheduler] Thread: starting\n");
    
    HRESULT hr = S_OK;
    LONG lWait = INFINITE;
    
    // Signal that we're ready
    SetEvent(m_hThreadReadyEvent);
    
    // Events to wait on: [0] = flush, [1] = sample ready
    HANDLE events[2] = { m_hFlushEvent, m_hSampleReadyEvent };
    
    while (m_bThreadRunning)
    {
        // Wait for next event or timeout
        DWORD result = WaitForMultipleObjects(2, events, FALSE, lWait);
        
        if (!m_bThreadRunning)
        {
            DebugLog("[DX11Scheduler] Thread: m_bThreadRunning is FALSE, exiting loop\n");
            break;
        }
        
        if (result == WAIT_OBJECT_0)
        {
            // Flush event signaled - check if we should exit
            DebugLog("[DX11Scheduler] Thread: Flush event received, m_bThreadRunning=%d\n", m_bThreadRunning);
            if (!m_bThreadRunning)
            {
                break;
            }
            lWait = INFINITE;
        }
        else if (result == WAIT_OBJECT_0 + 1 || result == WAIT_TIMEOUT)
        {
            // Sample ready event or timeout - process samples
            if (m_bStarted)
            {
                hr = ProcessSamplesInQueue(&lWait);
                
                if (lWait == INFINITE && m_dwQueueCount > 0)
                {
                    // Still have samples, use frame rate for next check
                    lWait = 1000 / (m_dwFrameRate > 0 ? m_dwFrameRate : 30);
                }
                else if (lWait == INFINITE)
                {
                    // No samples, wait for next sample event
                    lWait = INFINITE;
                }
            }
        }
    }
    
    //DebugLog("[DX11Scheduler] Thread: exiting\n");
    return 0;
}


