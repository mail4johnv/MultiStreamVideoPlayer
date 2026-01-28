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

#include "Activate.h"
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
//    OutputDebugStringA(buffer);
//    
//    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
//    if (hStdOut != INVALID_HANDLE_VALUE && hStdOut != NULL)
//    {
//        DWORD written;
//        WriteConsoleA(hStdOut, buffer, (DWORD)strlen(buffer), &written, NULL);
//    }
//}

//-----------------------------------------------------------------------------
// CDXVA2RendererActivate
//-----------------------------------------------------------------------------

HRESULT CDXVA2RendererActivate::CreateInstance(HWND hwndVideo, IMFActivate** ppActivate, UINT gpuAdapterIndex)
{
    // Initialize logger on first activation (thread-safe, idempotent)
    static bool loggerInitialized = false;
    if (!loggerInitialized)
    {
        InitializeLogging("debug.log");
        loggerInitialized = true;
    }

    if (!ppActivate)
    {
        return E_POINTER;
    }

    *ppActivate = nullptr;

    CDXVA2RendererActivate* pActivate = DBG_NEW CDXVA2RendererActivate(hwndVideo, gpuAdapterIndex);
    if (!pActivate)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pActivate->Initialize();
    if (FAILED(hr))
    {
        delete pActivate;
        return hr;
    }

    // Constructor sets refcount to 1, so just assign without AddRef
    *ppActivate = static_cast<IMFActivate*>(pActivate);
    // Don't call AddRef - refcount is already 1 from constructor

    return S_OK;
}

CDXVA2RendererActivate::CDXVA2RendererActivate(HWND hwndVideo, UINT gpuAdapterIndex) :
    m_nRefCount(1),
    m_hwndVideo(hwndVideo),
    m_gpuAdapterIndex(gpuAdapterIndex),
    m_pAttributes(nullptr),
    m_pMediaSink(nullptr)
{
}

CDXVA2RendererActivate::~CDXVA2RendererActivate()
{
    //DebugLog("[DX11Activate] Destructor called\n");
    SafeRelease(m_pAttributes);
    SafeRelease(m_pMediaSink);
    //DebugLog("[DX11Activate] Destructor complete\n");
}

HRESULT CDXVA2RendererActivate::Initialize()
{
    return MFCreateAttributes(&m_pAttributes, 1);
}

// IUnknown

STDMETHODIMP CDXVA2RendererActivate::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    if (riid == IID_IUnknown)
    {
        *ppv = static_cast<IUnknown*>(static_cast<IMFActivate*>(this));
    }
    else if (riid == IID_IMFActivate)
    {
        *ppv = static_cast<IMFActivate*>(this);
    }
    else if (riid == IID_IMFAttributes)
    {
        *ppv = static_cast<IMFAttributes*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CDXVA2RendererActivate::AddRef()
{
    ULONG count = InterlockedIncrement(&m_nRefCount);
    //DebugLog("[DX11Activate] AddRef: refcount=%lu\n", count);
    return count;
}

STDMETHODIMP_(ULONG) CDXVA2RendererActivate::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    //DebugLog("[DX11Activate] Release: refcount=%lu\n", uCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

// IMFAttributes - delegate to internal attributes

STDMETHODIMP CDXVA2RendererActivate::GetItem(REFGUID guidKey, PROPVARIANT* pValue)
{
    return m_pAttributes->GetItem(guidKey, pValue);
}

STDMETHODIMP CDXVA2RendererActivate::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType)
{
    return m_pAttributes->GetItemType(guidKey, pType);
}

STDMETHODIMP CDXVA2RendererActivate::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult)
{
    return m_pAttributes->CompareItem(guidKey, Value, pbResult);
}

STDMETHODIMP CDXVA2RendererActivate::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult)
{
    return m_pAttributes->Compare(pTheirs, MatchType, pbResult);
}

STDMETHODIMP CDXVA2RendererActivate::GetUINT32(REFGUID guidKey, UINT32* punValue)
{
    return m_pAttributes->GetUINT32(guidKey, punValue);
}

STDMETHODIMP CDXVA2RendererActivate::GetUINT64(REFGUID guidKey, UINT64* punValue)
{
    return m_pAttributes->GetUINT64(guidKey, punValue);
}

STDMETHODIMP CDXVA2RendererActivate::GetDouble(REFGUID guidKey, double* pfValue)
{
    return m_pAttributes->GetDouble(guidKey, pfValue);
}

STDMETHODIMP CDXVA2RendererActivate::GetGUID(REFGUID guidKey, GUID* pguidValue)
{
    return m_pAttributes->GetGUID(guidKey, pguidValue);
}

STDMETHODIMP CDXVA2RendererActivate::GetStringLength(REFGUID guidKey, UINT32* pcchLength)
{
    return m_pAttributes->GetStringLength(guidKey, pcchLength);
}

STDMETHODIMP CDXVA2RendererActivate::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength)
{
    return m_pAttributes->GetString(guidKey, pwszValue, cchBufSize, pcchLength);
}

STDMETHODIMP CDXVA2RendererActivate::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength)
{
    return m_pAttributes->GetAllocatedString(guidKey, ppwszValue, pcchLength);
}

STDMETHODIMP CDXVA2RendererActivate::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize)
{
    return m_pAttributes->GetBlobSize(guidKey, pcbBlobSize);
}

STDMETHODIMP CDXVA2RendererActivate::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize)
{
    return m_pAttributes->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize);
}

STDMETHODIMP CDXVA2RendererActivate::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize)
{
    return m_pAttributes->GetAllocatedBlob(guidKey, ppBuf, pcbSize);
}

STDMETHODIMP CDXVA2RendererActivate::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv)
{
    return m_pAttributes->GetUnknown(guidKey, riid, ppv);
}

STDMETHODIMP CDXVA2RendererActivate::SetItem(REFGUID guidKey, REFPROPVARIANT Value)
{
    return m_pAttributes->SetItem(guidKey, Value);
}

STDMETHODIMP CDXVA2RendererActivate::DeleteItem(REFGUID guidKey)
{
    return m_pAttributes->DeleteItem(guidKey);
}

STDMETHODIMP CDXVA2RendererActivate::DeleteAllItems()
{
    return m_pAttributes->DeleteAllItems();
}

STDMETHODIMP CDXVA2RendererActivate::SetUINT32(REFGUID guidKey, UINT32 unValue)
{
    return m_pAttributes->SetUINT32(guidKey, unValue);
}

STDMETHODIMP CDXVA2RendererActivate::SetUINT64(REFGUID guidKey, UINT64 unValue)
{
    return m_pAttributes->SetUINT64(guidKey, unValue);
}

STDMETHODIMP CDXVA2RendererActivate::SetDouble(REFGUID guidKey, double fValue)
{
    return m_pAttributes->SetDouble(guidKey, fValue);
}

STDMETHODIMP CDXVA2RendererActivate::SetGUID(REFGUID guidKey, REFGUID guidValue)
{
    return m_pAttributes->SetGUID(guidKey, guidValue);
}

STDMETHODIMP CDXVA2RendererActivate::SetString(REFGUID guidKey, LPCWSTR wszValue)
{
    return m_pAttributes->SetString(guidKey, wszValue);
}

STDMETHODIMP CDXVA2RendererActivate::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize)
{
    return m_pAttributes->SetBlob(guidKey, pBuf, cbBufSize);
}

STDMETHODIMP CDXVA2RendererActivate::SetUnknown(REFGUID guidKey, IUnknown* pUnknown)
{
    return m_pAttributes->SetUnknown(guidKey, pUnknown);
}

STDMETHODIMP CDXVA2RendererActivate::LockStore()
{
    return m_pAttributes->LockStore();
}

STDMETHODIMP CDXVA2RendererActivate::UnlockStore()
{
    return m_pAttributes->UnlockStore();
}

STDMETHODIMP CDXVA2RendererActivate::GetCount(UINT32* pcItems)
{
    return m_pAttributes->GetCount(pcItems);
}

STDMETHODIMP CDXVA2RendererActivate::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue)
{
    return m_pAttributes->GetItemByIndex(unIndex, pguidKey, pValue);
}

STDMETHODIMP CDXVA2RendererActivate::CopyAllItems(IMFAttributes* pDest)
{
    return m_pAttributes->CopyAllItems(pDest);
}

// IMFActivate

STDMETHODIMP CDXVA2RendererActivate::ActivateObject(REFIID riid, void** ppv)
{
    CAutoLock lock(&m_critSec);

    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = nullptr;

    HRESULT hr = S_OK;

    // Create sink if not already created
    if (!m_pMediaSink)
    {
        hr = CMediaSink::CreateInstance(m_hwndVideo, &m_pMediaSink, m_gpuAdapterIndex);
    }

    if (SUCCEEDED(hr))
    {
        hr = m_pMediaSink->QueryInterface(riid, ppv);
    }

    return hr;
}

STDMETHODIMP CDXVA2RendererActivate::ShutdownObject()
{
    DebugLog("[DX11Activate] ShutdownObject: entering\n");
    
    CAutoLock lock(&m_critSec);

    if (m_pMediaSink)
    {
        DebugLog("[DX11Activate] ShutdownObject: calling MediaSink::Shutdown\n");
        m_pMediaSink->Shutdown();
        DebugLog("[DX11Activate] ShutdownObject: releasing MediaSink\n");
        SafeRelease(m_pMediaSink);
    }

    DebugLog("[DX11Activate] ShutdownObject: complete\n");
    return S_OK;
}

STDMETHODIMP CDXVA2RendererActivate::DetachObject()
{
    CAutoLock lock(&m_critSec);

    // Detach without shutdown
    SafeRelease(m_pMediaSink);

    return S_OK;
}

//-----------------------------------------------------------------------------
// Helper function
//-----------------------------------------------------------------------------

HRESULT DX11VideoRenderer::CreateDX11VideoRendererActivate(HWND hwndVideo, IMFActivate** ppActivate, UINT gpuAdapterIndex)
{
    return CDXVA2RendererActivate::CreateInstance(hwndVideo, ppActivate, gpuAdapterIndex);
}
