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
#include "Presenter.h"
#include "Scheduler.h"

namespace DX11VideoRenderer
{
    class CMediaSink;

    enum RENDER_STATE
    {
        RENDER_STATE_STOPPED = 0,
        RENDER_STATE_PAUSED,
        RENDER_STATE_STARTED,
        RENDER_STATE_SHUTDOWN
    };

    // CStreamSink: Implements IMFStreamSink for video stream handling
    class CStreamSink :
        public IMFStreamSink,
        public IMFMediaTypeHandler,
        public IMFGetService,
        public ISchedulerCallback
    {
    public:
        // Static creation method
        static HRESULT CreateInstance(
            CMediaSink* pParent,
            DWORD dwStreamId,
            CPresenter* pPresenter,
            CScheduler* pScheduler,
            CCritSec& critSec,
            CStreamSink** ppStreamSink);

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

        // IMFStreamSink
        STDMETHODIMP GetMediaSink(IMFMediaSink** ppMediaSink);
        STDMETHODIMP GetIdentifier(DWORD* pdwIdentifier);
        STDMETHODIMP GetMediaTypeHandler(IMFMediaTypeHandler** ppHandler);
        STDMETHODIMP ProcessSample(IMFSample* pSample);
        STDMETHODIMP PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT* pvarMarkerValue, const PROPVARIANT* pvarContextValue);
        STDMETHODIMP Flush();

        // IMFMediaEventGenerator (inherited through IMFStreamSink)
        STDMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent);
        STDMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState);
        STDMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent);
        STDMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue);

        // IMFMediaTypeHandler
        STDMETHODIMP IsMediaTypeSupported(IMFMediaType* pMediaType, IMFMediaType** ppMediaType);
        STDMETHODIMP GetMediaTypeCount(DWORD* pdwTypeCount);
        STDMETHODIMP GetMediaTypeByIndex(DWORD dwIndex, IMFMediaType** ppType);
        STDMETHODIMP SetCurrentMediaType(IMFMediaType* pMediaType);
        STDMETHODIMP GetCurrentMediaType(IMFMediaType** ppMediaType);
        STDMETHODIMP GetMajorType(GUID* pguidMajorType);

        // IMFGetService
        STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject);

        // ISchedulerCallback
        void OnSampleReady(IMFSample* pSample) override;
        void OnSampleFree(IMFSample* pSample) override;

        // State management
        HRESULT Start(MFTIME start);
        HRESULT Stop();
        HRESULT Pause();
        HRESULT Restart();
        HRESULT Shutdown();

        // Preroll
        HRESULT SetClockRate(float fRate);
        HRESULT Preroll();
        BOOL IsPrerolling() const { return m_bPrerolling; }
        BOOL NeedMoreSamples();

    protected:
        CStreamSink(CMediaSink* pParent, DWORD dwStreamId, CPresenter* pPresenter, CScheduler* pScheduler, CCritSec& critSec);
        virtual ~CStreamSink();

        HRESULT Initialize();
        HRESULT CheckShutdown() const;
        HRESULT ValidateOperation(RENDER_STATE state);

        // Sample processing
        HRESULT ProcessSampleInternal(IMFSample* pSample);
        HRESULT ProcessOutputSample(IMFSample* pSample);
        HRESULT RequestSample();

        // Media type validation
        HRESULT ValidateMediaType(IMFMediaType* pMediaType);
        BOOL IsValidVideoType(IMFMediaType* pMediaType);

    private:
        long m_nRefCount;
        CCritSec m_critSec;  // Own critical section for stream-level synchronization
        
        CMediaSink* m_pParent;
        DWORD m_dwStreamId;
        
        CPresenter* m_pPresenter;
        CScheduler* m_pScheduler;
        
        IMFMediaEventQueue* m_pEventQueue;
        IMFMediaType* m_pCurrentType;
        
        RENDER_STATE m_state;
        BOOL m_bPrerolling;
        BOOL m_bWaitingForOnClockStart;
        BOOL m_bStarted;
        float m_fRate;
        
        DWORD m_dwSamplesRequested;
        DWORD m_dwSamplesOutstanding;
        
        static const DWORD MAX_SAMPLES_REQUESTED = 4;
    };

} // namespace DX11VideoRenderer
