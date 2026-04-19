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
#include "Display.h"

namespace DX11VideoRenderer
{
    class CPresenter :
        public IMFVideoDisplayControl,
        public IMFGetService,
        public IDX11VideoColorControl,
        private CBase
    {
    public:
        // Static creation
        static HRESULT CreateInstance(CPresenter** ppPresenter, UINT gpuAdapterIndex = 0);
        
        CPresenter();
        virtual ~CPresenter();

        // IUnknown
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
        STDMETHODIMP_(ULONG) Release();

        // IMFVideoDisplayControl
        STDMETHODIMP GetAspectRatioMode(DWORD* pdwAspectRatioMode) { return E_NOTIMPL; }
        STDMETHODIMP GetBorderColor(COLORREF* pClr) { return E_NOTIMPL; }
        STDMETHODIMP GetCurrentImage(BITMAPINFOHEADER* pBih, BYTE** pDib, DWORD* pcbDib, LONGLONG* pTimestamp) { return E_NOTIMPL; }
        STDMETHODIMP GetFullscreen(BOOL* pfFullscreen);
        STDMETHODIMP GetIdealVideoSize(SIZE* pszMin, SIZE* pszMax) { return E_NOTIMPL; }
        STDMETHODIMP GetNativeVideoSize(SIZE* pszVideo, SIZE* pszARVideo) { return E_NOTIMPL; }
        STDMETHODIMP GetRenderingPrefs(DWORD* pdwRenderFlags);
        STDMETHODIMP GetVideoPosition(MFVideoNormalizedRect* pnrcSource, LPRECT prcDest) { return E_NOTIMPL; }
        STDMETHODIMP GetVideoWindow(HWND* phwndVideo);
        STDMETHODIMP RepaintVideo();
        STDMETHODIMP SetAspectRatioMode(DWORD dwAspectRatioMode) { return E_NOTIMPL; }
        STDMETHODIMP SetBorderColor(COLORREF Clr) { return E_NOTIMPL; }
        STDMETHODIMP SetFullscreen(BOOL fFullscreen);
        STDMETHODIMP SetRenderingPrefs(DWORD dwRenderingPrefs);
        STDMETHODIMP SetVideoPosition(const MFVideoNormalizedRect* pnrcSource, const LPRECT prcDest) { return E_NOTIMPL; }
        STDMETHODIMP SetVideoWindow(HWND hwndVideo);

        // IMFGetService
        STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject);

        // Presenter methods
        HRESULT InitializeDeviceManager(UINT gpuAdapterIndex = 0);  // Early init for hardware decoding support
        HRESULT Initialize(HWND hwndVideo, UINT gpuAdapterIndex = 0);
        HRESULT Shutdown();
        
        BOOL    CanProcessNextSample() const { return m_bCanProcessNextSample; }
        HRESULT Flush();
        HRESULT GetMonitorRefreshRate(DWORD* pdwRefreshRate);
        HRESULT IsMediaTypeSupported(IMFMediaType* pMediaType, DXGI_FORMAT dxgiFormat);
        HRESULT ProcessFrame(IMFSample* pSample);
        HRESULT ProcessFrameEx(IMFMediaType* pCurrentType, IMFSample* pSample, UINT32* punInterlaceMode, BOOL* pbDeviceChanged, BOOL* pbProcessAgain, IMFSample** ppOutputSample);
        HRESULT PresentFrame();
        HRESULT SetCurrentMediaType(IMFMediaType* pMediaType);
        HRESULT SetMediaType(IMFMediaType* pMediaType);
        HRESULT GetVideoSize(UINT32* pWidth, UINT32* pHeight);
        HRESULT SetDestinationRect(const RECT& rcDest);
        HRESULT SetUserSharpenSliderValue(float sliderValue);
        HRESULT SetUserSharpenThreshold(float thresholdValue);
        STDMETHODIMP SetColorControls(int brightness, int contrast, int hue, int saturation) override;

    private:
        struct alignas(16) SharpenSettingsData
        {
            float fSharpenStrength;
            float fThreshold;
            float fBrightness;
            float fContrast;
            float fHueRadians;
            float fSaturation;
            float _padding[2];
        };

        HRESULT CheckShutdown() const;
        HRESULT CheckDeviceState(BOOL* pbDeviceChanged);
        HRESULT CreateVideoProcessor();
        HRESULT ProcessFrameUsingVideoProcessor(ID3D11Texture2D* pTexture, UINT dwViewIndex, RECT rcDest, UINT32 unInterlaceMode);
        HRESULT ProcessSoftwareBuffer(IMFMediaBuffer* pBuffer);
        HRESULT CreateStagingTexture(UINT width, UINT height, DXGI_FORMAT format);
        HRESULT CreateSharpenResources();
        HRESULT EnsureSharpenIntermediateResources(UINT width, UINT height, DXGI_FORMAT format);
        HRESULT ApplySharpenPass(bool refreshSourceFromBackBuffer);
        void    ApplyVideoProcessorColorControls(ID3D11VideoContext* pVideoContext);
        void    SetVideoContextParameters(ID3D11VideoContext* pVideoContext, const RECT* pSrcRect, const RECT* pDstRect, UINT32 unInterlaceMode);
        void    UpdateRectangles(RECT* pDst, RECT* pSrc);
        void    LetterBoxDstRect(LPRECT lprcLBDst, const RECT& rcSrc, const RECT& rcDst);
        
        long                            m_nRefCount;
        CCritSec                        m_critSec;
        BOOL                            m_bShutdown;
        
        // Display manager
        CDisplayManager*                m_pDisplayManager;
        HWND                            m_hwndVideo;
        
        // D3D11 Video objects
        ID3D11VideoDevice*              m_pVideoDevice;
        ID3D11VideoProcessorEnumerator* m_pVideoProcessorEnum;
        ID3D11VideoProcessor*           m_pVideoProcessor;
        ID3D11Texture2D*                m_pStagingTexture;
        DXGI_FORMAT                     m_stagingFormat;

        // Post-process sharpen resources
        ID3D11VertexShader*             m_pFullscreenVS;
        ID3D11PixelShader*              m_pSharpenPS;
        ID3D11SamplerState*             m_pLinearSampler;
        ID3D11Buffer*                   m_pSharpenSettingsBuffer;
        ID3D11Texture2D*                m_pSharpenIntermediateTexture;
        ID3D11ShaderResourceView*       m_pSharpenIntermediateSRV;
        BOOL                            m_bHasSharpenSource;
        float                           m_userSliderValue;
        float                           m_userThreshold;
        BOOL                            m_bSharpenEnabled;
        int                             m_colorBrightness;
        int                             m_colorContrast;
        int                             m_colorHue;
        int                             m_colorSaturation;
        
        // State
        BOOL                            m_bFullScreenState;
        DWORD                           m_dwRenderingPrefs;
        BOOL                            m_bCanProcessNextSample;
        BOOL                            m_bDeviceChanged;
        
        // Video dimensions
        UINT32                          m_imageWidthInPixels;
        UINT32                          m_imageHeightInPixels;
        UINT32                          m_uiRealDisplayWidth;
        UINT32                          m_uiRealDisplayHeight;
        
        // Rectangles
        RECT                            m_displayRect;
        RECT                            m_rcSrcApp;
        RECT                            m_rcDstApp;
    };
}
