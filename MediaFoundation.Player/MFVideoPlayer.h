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

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <shlwapi.h>
#include <vcclr.h>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;

namespace MediaFoundation {
    namespace Player {

        // Forward declarations
        ref class VideoPlayer;
        class CPlayerCallback;

        public enum class PlayerState {
            Closed = 0,
            Ready,
            OpenPending,
            Started,
            Paused,
            Stopped,
            Closing
        };

        public ref class GPUAdapterInfo {
        public:
            property unsigned int AdapterIndex { unsigned int get(); }
            property String^ Description { String^ get(); }
            property unsigned int VendorId { unsigned int get(); }
            property unsigned int DeviceId { unsigned int get(); }
            property unsigned long long DedicatedVideoMemory { unsigned long long get(); }
            property unsigned long long SharedSystemMemory { unsigned long long get(); }
            
        internal:
            GPUAdapterInfo(unsigned int idx, String^ desc, unsigned int vendor, unsigned int device, 
                          unsigned long long vram, unsigned long long shared) :
                m_index(idx), m_description(desc), m_vendorId(vendor), m_deviceId(device),
                m_vram(vram), m_shared(shared) {}
        private:
            unsigned int m_index;
            String^ m_description;
            unsigned int m_vendorId;
            unsigned int m_deviceId;
            unsigned long long m_vram;
            unsigned long long m_shared;
        };

        public ref class VideoPlayer {
        private:
            IMFMediaSession* m_pSession;
            IMFMediaSource* m_pSource;
            IMFVideoDisplayControl* m_pVideoDisplay;
            IMFPresentationDescriptor* m_pPresentationDescriptor;
            IMFTopology* m_pTopology;
            CPlayerCallback* m_pCallback;
            
            PlayerState m_state;
            HWND m_hwndVideo;
            HWND m_hwndEvent;
            DWORD m_nrcEventCookie;
            unsigned int m_gpuAdapterIndex;  // GPU adapter to use for rendering
            double m_sharpenStrength;
            double m_sharpenThreshold;
            int m_colorBrightness;
            int m_colorContrast;
            int m_colorHue;
            int m_colorSaturation;
            
            CRITICAL_SECTION* m_pCritSec;
            bool m_initialized;

            // Internal methods
            HRESULT CreateSession();
            HRESULT CloseSession();
            HRESULT CreateMediaSource(String^ url);
            HRESULT CreateTopologyFromSource(IMFTopology** ppTopology);
            HRESULT AddBranchToPartialTopology(IMFTopology* pTopology, IMFPresentationDescriptor* pPD, DWORD iStream);
            HRESULT CreateSourceStreamNode(IMFPresentationDescriptor* pPD, IMFStreamDescriptor* pSD, IMFTopologyNode** ppNode);
            HRESULT CreateOutputNode(IMFStreamDescriptor* pSD, IMFTopologyNode** ppNode);
            
        internal:
            IMFMediaSession* GetSession() { return m_pSession; }
            
        public:
            HRESULT HandleEvent(IMFMediaEvent* pEvent);
            
        public:
            VideoPlayer();
            ~VideoPlayer();
            !VideoPlayer();

            // Events
            event EventHandler^ MediaOpened;
            event EventHandler^ MediaEnded;
            event EventHandler^ MediaFailed;

            // Properties
            property PlayerState State {
                PlayerState get() { return m_state; }
            }

            property IntPtr VideoWindow {
                IntPtr get() { return IntPtr(m_hwndVideo); }
                void set(IntPtr hwnd);
            }

            property TimeSpan Duration {
                TimeSpan get();
            }

            property TimeSpan Position {
                TimeSpan get();
                void set(TimeSpan value);
            }

            property double Volume {
                double get();
                void set(double value);
            }

            property bool IsMuted {
                bool get();
                void set(bool value);
            }

            property double SharpenStrength {
                double get() { return m_sharpenStrength; }
                void set(double value);
            }

            property double SharpenThreshold {
                double get() { return m_sharpenThreshold; }
                void set(double value);
            }

            property int Brightness {
                int get() { return m_colorBrightness; }
                void set(int value);
            }

            property int Contrast {
                int get() { return m_colorContrast; }
                void set(int value);
            }

            property int Hue {
                int get() { return m_colorHue; }
                void set(int value);
            }

            property int Saturation {
                int get() { return m_colorSaturation; }
                void set(int value);
            }

            // Methods
            void OpenUrl(String^ url);
            void Play();
            void Pause();
            void Stop();
            void Shutdown();
            void Repaint();
            void ResizeVideo(int width, int height);
            bool HasVideo();
            bool HasAudio();
            
            // GPU adapter selection
            List<GPUAdapterInfo^>^ EnumerateGPUs();
            void SetGPUAdapter(unsigned int adapterIndex);
        };

        // Event callback class
        class CPlayerCallback : public IMFAsyncCallback {
        private:
            long m_cRef;
            gcroot<VideoPlayer^> m_pPlayer;

        public:
            CPlayerCallback(VideoPlayer^ player) : m_cRef(1), m_pPlayer(player) {}

            // IUnknown methods
            STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
                static const QITAB qit[] = {
                    QITABENT(CPlayerCallback, IMFAsyncCallback),
                    { 0 }
                };
                return QISearch(this, qit, riid, ppv);
            }

            STDMETHODIMP_(ULONG) AddRef() {
                return InterlockedIncrement(&m_cRef);
            }

            STDMETHODIMP_(ULONG) Release() {
                ULONG count = InterlockedDecrement(&m_cRef);
                if (count == 0) {
                    delete this;
                }
                return count;
            }

            // IMFAsyncCallback methods
            STDMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) {
                return E_NOTIMPL;
            }

            STDMETHODIMP Invoke(IMFAsyncResult* pResult);
        };
    }
}
