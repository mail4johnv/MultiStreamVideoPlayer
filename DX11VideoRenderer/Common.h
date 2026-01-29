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

// CRT Debug Memory Leak Detection
// Must be included before any other headers
#ifdef _DEBUG
    #define _CRTDBG_MAP_ALLOC
    #include <stdlib.h>
    #include <crtdbg.h>
    
    // Use DBG_NEW for allocations where you want file/line tracking
    // Usage: MyClass* p = DBG_NEW MyClass();
    // Note: We don't globally redefine 'new' because it breaks std::nothrow and placement new
#define DBG_NEW new (_NORMAL_BLOCK, __FILE__, __LINE__)
#else
    #define DBG_NEW new
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfobjects.h>
#include <evr.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include "Logger.h"
// ATL smart pointers for COM interfaces
#include <atlbase.h>
#include <atlcomcli.h>


#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "evr.lib")

// ============================================================================
// DEADLOCK DEBUGGING FLAG
// ============================================================================
// Uncomment the line below to enable enhanced deadlock debugging features
// This adds file/line tracking, lock ownership details, and contention warnings
//#define ENABLE_DEADLOCK_DEBUGGING

// Maximum allowed recursive lock acquisitions before warning
#define MAX_LOCK_RECURSION_DEPTH 5

// Enable call stack capture for deep recursion debugging
#define ENABLE_CALLSTACK_ON_DEEP_RECURSION

#ifdef ENABLE_DEADLOCK_DEBUGGING
    #define DEADLOCK_LOG(...) LOG_DEBUG(__VA_ARGS__)
    #define TRACE_LOCK_LOCATION(lock, file, line) \
        do { \
            if (lock) { \
                lock->SetLockLocation(file, line); \
            } \
        } while(0)
#else
    #define DEADLOCK_LOG(...) ((void)0)
    #define TRACE_LOCK_LOCATION(lock, file, line) ((void)0)
#endif



namespace DX11VideoRenderer
{
    // Safe release template
    template <class T>
    inline void SafeRelease(T*& p)
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }

    // Safe delete template
    template <class T>
    inline void SafeDelete(T*& p)
    {
        if (p)
        {
            delete p;
            p = nullptr;
        }
    }

    // Safe array delete template
    template <class T>
    inline void SafeDeleteArray(T*& p)
    {
        if (p)
        {
            delete[] p;
            p = nullptr;
        }
    }

    // Critical section wrapper - NON-COPYABLE to prevent ODR/layout issues
    class CCritSec
    {
    private:
        CRITICAL_SECTION m_cs;
        volatile LONG m_isInitialized;
        volatile LONG m_lockCount;
        DWORD m_dwCreatorThreadId;  // Track creation thread for debugging
        ULONGLONG m_ullCreationTime; // Track creation time
        CCritSec* m_pSelf;           // Store address of this pointer for validation
        LONG m_nLockId;              // Unique ID for this lock instance
        volatile DWORD m_dwOwningThreadId; // Thread that currently owns the lock (0 if unlocked)
        ULONGLONG m_ullLastLockTime; // Time when lock was last acquired
        
#ifdef ENABLE_DEADLOCK_DEBUGGING
        // Enhanced debugging: Track lock acquisition/release locations
        char m_szLastLockLocation[256];   // File:Line where lock was last acquired
        char m_szLastUnlockLocation[256]; // File:Line where lock was last released
        DWORD m_dwLastLockThreadId;       // Thread ID that last acquired the lock
#endif
        
        // Static function to get next lock ID (avoids multiple definition issues)
        static LONG GetNextLockId()
        {
            static volatile LONG s_nNextLockId = 0;
            return InterlockedIncrement(&s_nNextLockId);
        }

        // Explicitly delete copy/move operations to prevent accidental copies that cause layout mismatches
        CCritSec(const CCritSec&) = delete;
        CCritSec& operator=(const CCritSec&) = delete;
        CCritSec(CCritSec&&) = delete;
        CCritSec& operator=(CCritSec&&) = delete;

    public:
        // COMPILE-TIME CHECK: Ensure consistent layout across all translation units
        // If you see compilation error here, it means different definitions of CCritSec exist
        static_assert(sizeof(CRITICAL_SECTION) > 0, "CRITICAL_SECTION size check");
        
        CCritSec() : m_isInitialized(0), m_lockCount(0), m_dwCreatorThreadId(GetCurrentThreadId()), 
                     m_ullCreationTime(GetTickCount64()), m_pSelf(this), 
                     m_nLockId(GetNextLockId()), m_dwOwningThreadId(0), m_ullLastLockTime(0)
#ifdef ENABLE_DEADLOCK_DEBUGGING
                     , m_dwLastLockThreadId(0)
#endif
        {
#ifdef ENABLE_DEADLOCK_DEBUGGING
            m_szLastLockLocation[0] = '\0';
            m_szLastUnlockLocation[0] = '\0';
#endif
            //LOG_DEBUG("[CCritSec::ctor] LOCK_ID=%ld, this=0x%p, creator_tid=0x%X, time=%llu\n", 
            //         m_nLockId, this, m_dwCreatorThreadId, m_ullCreationTime);
            
            __try
            {
                InitializeCriticalSection(&m_cs);
                // Verify CRITICAL_SECTION was initialized properly
                if (m_cs.DebugInfo == nullptr || m_cs.LockCount != -1)
                {
                    LOG_DEBUG("[CCritSec] LOCK_ID=%ld WARNING: CRITICAL_SECTION has suspicious state after init: LockCount=%ld\n", 
                             m_nLockId, m_cs.LockCount);
                }
                m_isInitialized = 1;
                //LOG_DEBUG("[CCritSec] LOCK_ID=%ld: Constructor completed successfully at 0x%p (self ptr: 0x%p)\n", 
                //         m_nLockId, &m_cs, m_pSelf);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                m_isInitialized = 0;
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld: Constructor EXCEPTION during InitializeCriticalSection! Status: 0x%X, this=0x%p\n", 
                         m_nLockId, GetExceptionCode(), this);
            }
        }

        ~CCritSec()
        {
            LOG_DEBUG("[CCritSec::dtor] LOCK_ID=%ld, this=0x%p, creator_tid=0x%X, age=%llu ms, is_init=%ld, owner_tid=0x%X\n", 
                     m_nLockId, this, m_dwCreatorThreadId, GetTickCount64() - m_ullCreationTime, m_isInitialized, m_dwOwningThreadId);
            
            if (m_isInitialized)
            {
                __try
                {
                    // Check if any threads still hold the lock
                    if (m_lockCount != 0)
                    {
                        LOG_DEBUG("[CCritSec] LOCK_ID=%ld WARNING: Destructor called with non-zero lock count: %ld, owner=0x%X\n", 
                                 m_nLockId, m_lockCount, m_dwOwningThreadId);
                    }
                    
                    if (m_dwOwningThreadId != 0)
                    {
                        LOG_DEBUG("[CCritSec] LOCK_ID=%ld CRITICAL WARNING: Destructor called while lock is owned by thread 0x%X!\n", 
                                 m_nLockId, m_dwOwningThreadId);
                    }
                    
                    DeleteCriticalSection(&m_cs);
                    m_isInitialized = 0;
                    //LOG_DEBUG("[CCritSec] LOCK_ID=%ld: Destructor completed (lock count was: %ld)\n", m_nLockId, m_lockCount);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    LOG_DEBUG("[CCritSec] LOCK_ID=%ld: Destructor EXCEPTION during DeleteCriticalSection! Status: 0x%X, this=0x%p\n", 
                             m_nLockId, GetExceptionCode(), this);
                }
            }
            else
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld: Destructor called but critical section was not initialized! this=0x%p\n", 
                         m_nLockId, this);
            }
        }

        void Lock(const char* file = nullptr, int line = 0)
        {
            DWORD dwThreadId = GetCurrentThreadId();
            
            if (!m_isInitialized)
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: CRITICAL ERROR - Attempting to lock uninitialized critical section from thread 0x%X! this=0x%p\n", 
                         m_nLockId, dwThreadId, this);
                return;
            }

            // Validate pointer integrity - check if this pointer matches stored self pointer
            if (this != m_pSelf)
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: CRITICAL ERROR - this pointer mismatch! Expected=0x%p, Got=0x%p, Thread=0x%X\n", 
                         m_nLockId, m_pSelf, this, dwThreadId);
                return;
            }

            // Check if this thread already owns the lock (recursive acquisition)
            if (m_dwOwningThreadId == dwThreadId)
            {
#ifdef ENABLE_DEADLOCK_DEBUGGING
                // Check for excessive recursion depth
                if (m_lockCount >= MAX_LOCK_RECURSION_DEPTH)
                {
                    DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: *** CRITICAL WARNING *** Excessive recursion depth %ld (max=%d) at %s:%d\n", 
                             m_nLockId, m_lockCount, MAX_LOCK_RECURSION_DEPTH, file ? file : "unknown", line);
                    DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: *** Possible recursive callback loop detected! ***\n", m_nLockId);
                    DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: *** Current stack depth: %ld, Thread: 0x%X ***\n", 
                             m_nLockId, m_lockCount, dwThreadId);
                             
#ifdef ENABLE_CALLSTACK_ON_DEEP_RECURSION
                    // Log call stack for analysis
                    if (m_lockCount == MAX_LOCK_RECURSION_DEPTH || m_lockCount == 10 || m_lockCount == 15 || m_lockCount == 20)
                    {
                        DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: === RECURSION DEPTH MILESTONE: %ld ===\n", m_nLockId, m_lockCount);
                    }
#endif
                }
                DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: Thread 0x%X RECURSIVE lock acquisition (current count: %ld) at %s:%d\n", 
                         m_nLockId, dwThreadId, m_lockCount, file ? file : "unknown", line);
#else
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: Thread 0x%X RECURSIVE lock acquisition (current count: %ld)\n", 
                         m_nLockId, dwThreadId, m_lockCount);
#endif
            }
            else
            {
#ifdef ENABLE_DEADLOCK_DEBUGGING
                DWORD currentOwner = m_dwOwningThreadId;
                DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: Thread 0x%X attempting to acquire lock (current owner: 0x%X, count: %ld) at %s:%d\n", 
                         m_nLockId, dwThreadId, currentOwner, m_lockCount, file ? file : "unknown", line);
                
                // If lock is held by another thread, log where it was acquired
                if (currentOwner != 0 && currentOwner != dwThreadId && m_szLastLockLocation[0] != '\0')
                {
                    DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: WARNING - Lock currently held by thread 0x%X since %s\n",
                             m_nLockId, currentOwner, m_szLastLockLocation);
                }
#else
                //LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: Thread 0x%X attempting to acquire lock (current owner: 0x%X, count: %ld)\n", 
                //         m_nLockId, dwThreadId, m_dwOwningThreadId, m_lockCount);
#endif
            }
            
            ULONGLONG ullStartTime = GetTickCount64();
#ifdef ENABLE_DEADLOCK_DEBUGGING
            DWORD previousOwner = m_dwOwningThreadId;
#endif
            
            __try
            {
                EnterCriticalSection(&m_cs);
                m_ullLastLockTime = GetTickCount64();
                ULONGLONG ullWaitTime = m_ullLastLockTime - ullStartTime;
                
                // Update owning thread (first time only)
                if (m_dwOwningThreadId == 0)
                {
                    m_dwOwningThreadId = dwThreadId;
                }
                
#ifdef ENABLE_DEADLOCK_DEBUGGING
                // Store lock acquisition location
                if (file)
                {
                    sprintf_s(m_szLastLockLocation, sizeof(m_szLastLockLocation), "%s:%d", file, line);
                }
                else
                {
                    sprintf_s(m_szLastLockLocation, sizeof(m_szLastLockLocation), "unknown");
                }
                m_dwLastLockThreadId = dwThreadId;
#endif
                
                LONG newCount = InterlockedIncrement(&m_lockCount);
                
#ifdef ENABLE_DEADLOCK_DEBUGGING
                DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: Thread 0x%X ACQUIRED lock successfully (count: %ld, wait_time: %llu ms) at %s:%d\n", 
                         m_nLockId, dwThreadId, newCount, ullWaitTime, file ? file : "unknown", line);
                
                // Enhanced warning for long waits
                if (ullWaitTime > 100)
                {
                    DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: WARNING - Lock acquisition took %llu ms (possible contention/deadlock)\n", 
                             m_nLockId, ullWaitTime);
                    if (previousOwner != 0 && previousOwner != dwThreadId)
                    {
                        DEADLOCK_LOG("[CCritSec] LOCK_ID=%ld Lock: Previous owner thread=0x%X held lock at: %s\n",
                                 m_nLockId, previousOwner, m_szLastLockLocation);
                    }
                }
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: Thread 0x%X ACQUIRED lock successfully (count: %ld, wait_time: %llu ms)\n", 
                         m_nLockId, dwThreadId, newCount, ullWaitTime);
#else
                
                if (ullWaitTime > 100)
                {
                    LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: WARNING - Lock acquisition took %llu ms (possible contention/deadlock)\n", 
                             m_nLockId, ullWaitTime);
                }
#endif
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Lock: EXCEPTION in EnterCriticalSection from thread 0x%X! Status: 0x%X, this=0x%p, &m_lockCount=0x%p\n", 
                         m_nLockId, dwThreadId, GetExceptionCode(), this, &m_lockCount);
            }
        }

        void Unlock(const char* file = nullptr, int line = 0)
        {
            DWORD dwThreadId = GetCurrentThreadId();

#ifdef ENABLE_DEADLOCK_DEBUGGING
            // Store unlock location
            if (file)
            {
                sprintf_s(m_szLastUnlockLocation, sizeof(m_szLastUnlockLocation), "%s:%d", file, line);
            }
#endif

            if (!m_isInitialized)
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: CRITICAL ERROR - Attempting to unlock uninitialized critical section from thread 0x%X! this=0x%p\n", 
                         m_nLockId, dwThreadId, this);
                return;
            }

            // Validate pointer integrity - check if this pointer matches stored self pointer
            if (this != m_pSelf)
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: CRITICAL ERROR - this pointer mismatch! Expected=0x%p, Got=0x%p, Thread=0x%X\n", 
                         m_nLockId, m_pSelf, this, dwThreadId);
                return;
            }

            // Check if this thread owns the lock (WARNING only - don't block unlock)
            if (m_dwOwningThreadId != dwThreadId && m_dwOwningThreadId != 0)
            {
                //LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: WARNING - Thread 0x%X trying to unlock, but tracked owner is 0x%X (may be tracking error, proceeding anyway)\n", 
                //         m_nLockId, dwThreadId, m_dwOwningThreadId);
                // Don't return - let Windows CRITICAL_SECTION handle the real ownership check
            }

            ULONGLONG ullHoldTime = GetTickCount64() - m_ullLastLockTime;
            //LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: Thread 0x%X releasing lock (current count: %ld, held_for: %llu ms)\n", 
            //         m_nLockId, dwThreadId, m_lockCount, ullHoldTime);

            __try
            {
                LeaveCriticalSection(&m_cs);
                LONG newCount = InterlockedDecrement(&m_lockCount);
                
                // Clear owning thread when fully released
                if (newCount == 0)
                {
                    m_dwOwningThreadId = 0;
                    //LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: Thread 0x%X FULLY RELEASED lock (count: %ld)\n", 
                    //         m_nLockId, dwThreadId, newCount);
                }
                else
                {
                    //LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: Thread 0x%X released lock (remaining count: %ld)\n", 
                    //         m_nLockId, dwThreadId, newCount);
                }
                
                if (ullHoldTime > 500)
                {
                    //LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: WARNING - Lock was held for %llu ms (possible performance issue)\n", 
                    //         m_nLockId, ullHoldTime);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LOG_DEBUG("[CCritSec] LOCK_ID=%ld Unlock: EXCEPTION in LeaveCriticalSection from thread 0x%X! Status: 0x%X, this=0x%p, &m_lockCount=0x%p\n", 
                         m_nLockId, dwThreadId, GetExceptionCode(), this, &m_lockCount);
            }
        }

        BOOL IsInitialized() const { return m_isInitialized != 0; }
        LONG GetLockCount() const { return m_lockCount; }
        LONG GetLockId() const { return m_nLockId; }
        DWORD GetOwningThreadId() const { return m_dwOwningThreadId; }
        
        // Raw access for advanced operations (use with caution)
        CRITICAL_SECTION& GetRawCritSec() { return m_cs; }
        
#ifdef ENABLE_DEADLOCK_DEBUGGING
        // Enhanced debugging methods
        void SetLockLocation(const char* file, int line)
        {
            if (file)
            {
                sprintf_s(m_szLastLockLocation, sizeof(m_szLastLockLocation), "%s:%d", file, line);
            }
        }
        
        const char* GetLockLocation() const { return m_szLastLockLocation; }
        const char* GetUnlockLocation() const { return m_szLastUnlockLocation; }
        DWORD GetLastLockThreadId() const { return m_dwLastLockThreadId; }
#endif
    };

    // Auto lock helper - NON-COPYABLE
    class CAutoLock
    {
    private:
        CCritSec* m_pLock;
        DWORD m_dwThreadId;
        ULONGLONG m_ullStartTime;

        // Delete copy/move to prevent accidental copies
        CAutoLock(const CAutoLock&) = delete;
        CAutoLock& operator=(const CAutoLock&) = delete;
        CAutoLock(CAutoLock&&) = delete;
        CAutoLock& operator=(CAutoLock&&) = delete;

    public:
#ifdef ENABLE_DEADLOCK_DEBUGGING
        CAutoLock(CCritSec* pLock, const char* file = __FILE__, int line = __LINE__) 
            : m_pLock(pLock), m_dwThreadId(GetCurrentThreadId())
        {
            m_ullStartTime = GetTickCount64();
            
            if (m_pLock)
            {
                DEADLOCK_LOG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X acquiring lock at 0x%p from %s:%d\n", 
                         m_pLock->GetLockId(), m_dwThreadId, m_pLock, file, line);
                m_pLock->Lock(file, line);
                ULONGLONG ullElapsed = GetTickCount64() - m_ullStartTime;
                DEADLOCK_LOG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X acquired lock (elapsed: %llu ms) at %s:%d\n", 
                         m_pLock->GetLockId(), m_dwThreadId, ullElapsed, file, line);
                if (ullElapsed > 100)
                {
                    DEADLOCK_LOG("[CAutoLock] LOCK_ID=%ld: WARNING - Lock acquisition took %llu ms at %s:%d\n", 
                             m_pLock->GetLockId(), ullElapsed, file, line);
                }
            }
            else
            {
                DEADLOCK_LOG("[CAutoLock] Constructor: WARNING - pLock is NULL from thread 0x%X at %s:%d\n", m_dwThreadId, file, line);
            }
        }
#else
        CAutoLock(CCritSec* pLock) : m_pLock(pLock), m_dwThreadId(GetCurrentThreadId())
        {
            m_ullStartTime = GetTickCount64();
            
            if (m_pLock)
            {
                //LOG_DEBUG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X acquiring lock at 0x%p\n", 
                         //m_pLock->GetLockId(), m_dwThreadId, m_pLock);
                m_pLock->Lock();
                ULONGLONG ullElapsed = GetTickCount64() - m_ullStartTime;
                //LOG_DEBUG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X acquired lock (elapsed: %llu ms)\n", 
                //         m_pLock->GetLockId(), m_dwThreadId, ullElapsed);
                if (ullElapsed > 100)
                {
                    LOG_DEBUG("[CAutoLock] LOCK_ID=%ld: WARNING - Lock acquisition took %llu ms (possible contention)\n", 
                             m_pLock->GetLockId(), ullElapsed);
                }
            }
            else
            {
                LOG_DEBUG("[CAutoLock] Constructor: WARNING - pLock is NULL from thread 0x%X\n", m_dwThreadId);
            }
        }
#endif

        ~CAutoLock()
        {
            if (m_pLock)
            {
                ULONGLONG ullHeldTime = GetTickCount64() - m_ullStartTime;
#ifdef ENABLE_DEADLOCK_DEBUGGING
                DEADLOCK_LOG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X releasing lock at 0x%p (held for: %llu ms)\n", 
                         m_pLock->GetLockId(), m_dwThreadId, m_pLock, ullHeldTime);
                m_pLock->Unlock(__FILE__, __LINE__);
                DEADLOCK_LOG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X released lock\n", 
                         m_pLock->GetLockId(), m_dwThreadId);
                
                if (ullHeldTime > 500)
                {
                    DEADLOCK_LOG("[CAutoLock] LOCK_ID=%ld: WARNING - Lock was held for %llu ms (possible deadlock scenario)\n", 
                             m_pLock->GetLockId(), ullHeldTime);
                }
#else
                //LOG_DEBUG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X releasing lock at 0x%p (held for: %llu ms)\n", 
                //         m_pLock->GetLockId(), m_dwThreadId, m_pLock, ullHeldTime);
                m_pLock->Unlock();
                //LOG_DEBUG("[CAutoLock] LOCK_ID=%ld: Thread 0x%X released lock\n", 
                //         m_pLock->GetLockId(), m_dwThreadId);
                
                if (ullHeldTime > 500)
                {
                    LOG_DEBUG("[CAutoLock] LOCK_ID=%ld: WARNING - Lock was held for %llu ms (possible deadlock scenario)\n", 
                             m_pLock->GetLockId(), ullHeldTime);
                }
#endif
            }
        }
    };

    // Base class for preventing copy
    class CBase
    {
    protected:
        CBase() {}
        virtual ~CBase() {}

    private:
        CBase(const CBase&) = delete;
        CBase& operator=(const CBase&) = delete;
    };

    // Helper function to create a video area
    inline MFVideoArea MakeArea(float x, float y, DWORD width, DWORD height)
    {
        MFVideoArea area;
        area.OffsetX.value = static_cast<short>(x);
        area.OffsetX.fract = static_cast<WORD>((x - area.OffsetX.value) * 65536);
        area.OffsetY.value = static_cast<short>(y);
        area.OffsetY.fract = static_cast<WORD>((y - area.OffsetY.value) * 65536);
        area.Area.cx = width;
        area.Area.cy = height;
        return area;
    }

    // Convert MFVideoArea to RECT
    inline RECT MFVideoAreaToRect(const MFVideoArea& area)
    {
        RECT rc;
        rc.left = static_cast<LONG>(area.OffsetX.value + (area.OffsetX.fract / 65536.0f));
        rc.top = static_cast<LONG>(area.OffsetY.value + (area.OffsetY.fract / 65536.0f));
        rc.right = rc.left + area.Area.cx;
        rc.bottom = rc.top + area.Area.cy;
        return rc;
    }

    // GCD helper for aspect ratio calculations
    inline int gcd(int a, int b)
    {
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        while (b != 0)
        {
            int t = b;
            b = a % b;
            a = t;
        }
        return a;
    }

    // Forward declarations
    class CMediaSink;
    class CStreamSink;
    class CPresenter;
    class CScheduler;
}
