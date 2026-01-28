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
#include <mfapi.h>

namespace DX11VideoRenderer
{
    class CPresenter;

    // Callback interface for scheduled samples
    class ISchedulerCallback
    {
    public:
        virtual void OnSampleReady(IMFSample* pSample) = 0;
        virtual void OnSampleFree(IMFSample* pSample) = 0;
    };

    // Frame scheduler for timing video presentation
    class CScheduler : private CBase
    {
    public:
        CScheduler(CPresenter* pPresenter);
        ~CScheduler();

        // Control
        HRESULT Start(IMFClock* pClock);
        HRESULT Stop();
        HRESULT Flush();
        
        // Sample scheduling
        HRESULT ScheduleSample(IMFSample* pSample, BOOL bPresentNow);
        HRESULT ProcessSamplesInQueue(LONG* plNextSleep);
        
        // State
        BOOL    IsStarted() const { return m_bStarted; }
        DWORD   GetFrameRate() const { return m_dwFrameRate; }
        void    SetFrameRate(DWORD dwFrameRate);
        void    SetPlaybackRate(float fRate) { m_fRate = fRate; }
        void    SetCallback(ISchedulerCallback* pCallback) { m_pCallback = pCallback; }

    private:
        // Thread procedure
        static DWORD WINAPI SchedulerThreadProc(LPVOID lpParameter);
        DWORD SchedulerThreadProcPrivate();
        
        // Sample queue operations
        HRESULT QueueSample(IMFSample* pSample);
        HRESULT DequeueAndPresent(IMFSample** ppSample);
        HRESULT PresentSample(IMFSample* pSample);
        
        // Time helpers
        LONGLONG GetPresentationTime();
        HRESULT  WaitForSampleTime(IMFSample* pSample, LONG* plWait);

        CCritSec                m_critSec;
        CPresenter*             m_pPresenter;
        ISchedulerCallback*     m_pCallback;
        
        // Clock
        IMFClock*               m_pClock;
        
        // Thread
        HANDLE                  m_hThread;
        HANDLE                  m_hThreadReadyEvent;
        HANDLE                  m_hFlushEvent;
        HANDLE                  m_hSampleReadyEvent;  // Signal when sample is queued
        DWORD                   m_dwThreadID;
        
        // State
        BOOL                    m_bStarted;
        volatile BOOL           m_bThreadRunning;  // Volatile - can be set from Stop() without lock
        DWORD                   m_dwFrameRate;
        float                   m_fRate;            // Playback rate (1.0 = normal)
        
        // Frame timing
        LONGLONG                m_PerFrameInterval; // Duration of each frame in 100ns units
        LONGLONG                m_PerFrame_1_4th;   // 1/4th of frame duration
        
        // Sample queue (simple array-based queue)
        static const DWORD      MAX_SAMPLES = 8;
        IMFSample*              m_sampleQueue[MAX_SAMPLES];
        DWORD                   m_dwQueueHead;
        DWORD                   m_dwQueueTail;
        DWORD                   m_dwQueueCount;
    };
}
