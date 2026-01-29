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
#include "MediaSink.h"

namespace DX11VideoRenderer
{
    // CDXVA2RendererActivate: Implements IMFActivate for creating the DX11 video renderer sink
    class CDXVA2RendererActivate : public IMFActivate
    {
    public:
        // Static creation method with optional GPU adapter index
        static HRESULT CreateInstance(HWND hwndVideo, IMFActivate** ppActivate, UINT gpuAdapterIndex = 0);

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

        // IMFAttributes (IMFActivate inherits from IMFAttributes)
        STDMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue);
        STDMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType);
        STDMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult);
        STDMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult);
        STDMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue);
        STDMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue);
        STDMETHODIMP GetDouble(REFGUID guidKey, double* pfValue);
        STDMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue);
        STDMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength);
        STDMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength);
        STDMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength);
        STDMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize);
        STDMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize);
        STDMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize);
        STDMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv);
        STDMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value);
        STDMETHODIMP DeleteItem(REFGUID guidKey);
        STDMETHODIMP DeleteAllItems();
        STDMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue);
        STDMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue);
        STDMETHODIMP SetDouble(REFGUID guidKey, double fValue);
        STDMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue);
        STDMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue);
        STDMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize);
        STDMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown);
        STDMETHODIMP LockStore();
        STDMETHODIMP UnlockStore();
        STDMETHODIMP GetCount(UINT32* pcItems);
        STDMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue);
        STDMETHODIMP CopyAllItems(IMFAttributes* pDest);

        // IMFActivate
        STDMETHODIMP ActivateObject(REFIID riid, void** ppv);
        STDMETHODIMP ShutdownObject();
        STDMETHODIMP DetachObject();

    protected:
        CDXVA2RendererActivate(HWND hwndVideo, UINT gpuAdapterIndex = 0);
        virtual ~CDXVA2RendererActivate();

        HRESULT Initialize();

    private:
        long m_nRefCount;
        CCritSec m_critSec;
        
        HWND m_hwndVideo;
        UINT m_gpuAdapterIndex;
        IMFAttributes* m_pAttributes;
        IMFMediaSink* m_pMediaSink;
    };

    // Helper function to create the DX11 video renderer sink activate object
    HRESULT CreateDX11VideoRendererActivate(HWND hwndVideo, IMFActivate** ppActivate, UINT gpuAdapterIndex = 0);

} // namespace DX11VideoRenderer
