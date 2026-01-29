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
#include "StreamSink.h"
#include "Scheduler.h"

namespace DX11VideoRenderer
{
    // CMediaSink: Implements IMFMediaSink, the main video renderer sink
    class CMediaSink :
        public IMFMediaSink,
        public IMFClockStateSink,
        public IMFMediaSinkPreroll,
        public IMFGetService,
        public IMFRateSupport,
        public IMFVideoDisplayControl
    {
    public:
        // Static creation method
        static HRESULT CreateInstance(
            HWND hwndVideo,
            IMFMediaSink** ppSink,
            UINT gpuAdapterIndex = 0);

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

        // IMFMediaSink
        STDMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics);
        STDMETHODIMP AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType* pMediaType, IMFStreamSink** ppStreamSink);
        STDMETHODIMP RemoveStreamSink(DWORD dwStreamSinkIdentifier);
        STDMETHODIMP GetStreamSinkCount(DWORD* pcStreamSinkCount);
        STDMETHODIMP GetStreamSinkByIndex(DWORD dwIndex, IMFStreamSink** ppStreamSink);
        STDMETHODIMP GetStreamSinkById(DWORD dwStreamSinkIdentifier, IMFStreamSink** ppStreamSink);
        STDMETHODIMP SetPresentationClock(IMFPresentationClock* pPresentationClock);
        STDMETHODIMP GetPresentationClock(IMFPresentationClock** ppPresentationClock);
        STDMETHODIMP Shutdown();

        // IMFClockStateSink
        STDMETHODIMP OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset);
        STDMETHODIMP OnClockStop(MFTIME hnsSystemTime);
        STDMETHODIMP OnClockPause(MFTIME hnsSystemTime);
        STDMETHODIMP OnClockRestart(MFTIME hnsSystemTime);
        STDMETHODIMP OnClockSetRate(MFTIME hnsSystemTime, float flRate);

        // IMFMediaSinkPreroll
        STDMETHODIMP NotifyPreroll(MFTIME hnsUpcomingStartTime);

        // IMFGetService
        STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject);

        // IMFRateSupport
        STDMETHODIMP GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float* pflRate);
        STDMETHODIMP GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float* pflRate);
        STDMETHODIMP IsRateSupported(BOOL fThin, float flRate, float* pflNearestSupportedRate);

        // IMFVideoDisplayControl
        STDMETHODIMP GetNativeVideoSize(SIZE* pszVideo, SIZE* pszARVideo);
        STDMETHODIMP GetIdealVideoSize(SIZE* pszMin, SIZE* pszMax);
        STDMETHODIMP SetVideoPosition(const MFVideoNormalizedRect* pnrcSource, const LPRECT prcDest);
        STDMETHODIMP GetVideoPosition(MFVideoNormalizedRect* pnrcSource, LPRECT prcDest);
        STDMETHODIMP SetAspectRatioMode(DWORD dwAspectRatioMode);
        STDMETHODIMP GetAspectRatioMode(DWORD* pdwAspectRatioMode);
        STDMETHODIMP SetVideoWindow(HWND hwndVideo);
        STDMETHODIMP GetVideoWindow(HWND* phwndVideo);
        STDMETHODIMP RepaintVideo();
        STDMETHODIMP GetCurrentImage(BITMAPINFOHEADER* pBih, BYTE** pDib, DWORD* pcbDib, LONGLONG* pTimeStamp);
        STDMETHODIMP SetBorderColor(COLORREF Clr);
        STDMETHODIMP GetBorderColor(COLORREF* pClr);
        STDMETHODIMP SetRenderingPrefs(DWORD dwRenderFlags);
        STDMETHODIMP GetRenderingPrefs(DWORD* pdwRenderFlags);
        STDMETHODIMP SetFullscreen(BOOL fFullscreen);
        STDMETHODIMP GetFullscreen(BOOL* pfFullscreen);

    protected:
        CMediaSink(HWND hwndVideo, UINT gpuAdapterIndex = 0);
        virtual ~CMediaSink();

        HRESULT Initialize();
        HRESULT CheckShutdown() const;
        
        // Per-sink shared critical section used by stream sink and scheduler
        CCritSec* GetSharedCritSec() { return &m_sharedCritSec; }

    private:
        long m_nRefCount;
        CCritSec m_critSec;
        CCritSec m_sharedCritSec;
        
        BOOL m_bShutdown;
        HWND m_hwndVideo;
        UINT m_gpuAdapterIndex;
        
        CComPtr<CPresenter> m_spPresenter;
        CComPtr<CStreamSink> m_spStreamSink;
        CScheduler* m_pScheduler;
        
        CComPtr<IMFPresentationClock> m_spClock;
        
        // Video display state
        RECT m_rcDest;
        MFVideoNormalizedRect m_nrcSource;
        DWORD m_dwAspectRatioMode;
        COLORREF m_clrBorder;
        DWORD m_dwRenderingPrefs;
        BOOL m_bFullscreen;
    };

} // namespace DX11VideoRenderer
