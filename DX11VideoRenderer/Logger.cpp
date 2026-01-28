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

#include "Logger.h"
#include <new>
#include <assert.h>
#include <strsafe.h>

// Definition: exactly one translation unit must provide the storage
CThreadSafeLogger CThreadSafeLogger::instance;

CThreadSafeLogger::CThreadSafeLogger() 
    : m_hFile(nullptr)
    , m_bufferPos(0)
    , m_isInitialized(false)
    , m_lastFlushTime(0)
{
    InitializeCriticalSection(&m_cs);
}

CThreadSafeLogger::~CThreadSafeLogger()
{
    Flush();
    if (m_hFile)
    {
        fclose(m_hFile);
        m_hFile = nullptr;
    }
    DeleteCriticalSection(&m_cs);
}

CThreadSafeLogger& CThreadSafeLogger::GetInstance()
{
    return instance;
}

bool CThreadSafeLogger::Initialize(const char* filename)
{
    EnterCriticalSection(&m_cs);

    if (m_isInitialized)
    {
        LeaveCriticalSection(&m_cs);
        return true;
    }

    errno_t err = fopen_s(&m_hFile, filename, "w");
    if (err != 0 || !m_hFile)
    {
        m_isInitialized = false;
        LeaveCriticalSection(&m_cs);
        return false;
    }

    // Use line buffering for faster writes - only 4KB buffer
    setvbuf(m_hFile, nullptr, _IOLBF, 4096);

    m_bufferPos = 0;
    m_lastFlushTime = GetTickCount64();
    m_isInitialized = true;

    LeaveCriticalSection(&m_cs);
    return true;
}

void CThreadSafeLogger::Log(const char* format, ...)
{
    if (!m_isInitialized)
        return;

    char tempBuffer[2048];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(tempBuffer, sizeof(tempBuffer), format, args);
    va_end(args);

    if (written <= 0)
        return;

    EnterCriticalSection(&m_cs);

    // Check if we need to flush before adding new data
    size_t spaceNeeded = written;
    if (m_bufferPos + spaceNeeded >= BUFFER_SIZE)
    {
        // Buffer full, flush it first
        FlushUnlocked();
    }

    // Also flush on time interval
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - m_lastFlushTime > FLUSH_INTERVAL_MS)
    {
        FlushUnlocked();
        m_lastFlushTime = currentTime;
    }

    // Copy to buffer
    if (m_bufferPos + spaceNeeded < BUFFER_SIZE)
    {
        memcpy(m_buffer + m_bufferPos, tempBuffer, written);
        m_bufferPos += written;
    }

    LeaveCriticalSection(&m_cs);
}

void CThreadSafeLogger::Flush()

{
    EnterCriticalSection(&m_cs);
    FlushUnlocked();
    LeaveCriticalSection(&m_cs);
}

void CThreadSafeLogger::FlushUnlocked()
{
    if (!m_isInitialized || !m_hFile || m_bufferPos == 0)
        return;

    fwrite(m_buffer, 1, m_bufferPos, m_hFile);
    fflush(m_hFile);
    m_bufferPos = 0;
    m_lastFlushTime = GetTickCount64();
}
