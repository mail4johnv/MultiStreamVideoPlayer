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
#include <stdio.h>
// Forward declaration for DebugLog
#define DebugLog LOG_DEBUG

// ============================================================================
// THREAD-SAFE HIGH-PERFORMANCE LOGGER CLASS
// ============================================================================
// This logger uses buffered I/O and only locks briefly for thread safety
// It writes to file in batches to minimize I/O operations
class CThreadSafeLogger
{
private:
    static const size_t BUFFER_SIZE = 65536; // 64KB buffer
    
    CRITICAL_SECTION m_cs;
    FILE* m_hFile;
    char m_buffer[BUFFER_SIZE];
    size_t m_bufferPos;
    bool m_isInitialized;
    ULONGLONG m_lastFlushTime;
    static const ULONGLONG FLUSH_INTERVAL_MS = 100; // Flush at least every 100ms
    
    // Private constructor - singleton pattern
    CThreadSafeLogger();

    ~CThreadSafeLogger();
    static CThreadSafeLogger instance;

public:
    // Get singleton instance
    static CThreadSafeLogger& GetInstance();

    // Initialize logger - call once at startup
    bool Initialize(const char* filename);
	bool IsInitialized() const { return m_isInitialized; }

    // Thread-safe logging with printf-style formatting
    void Log(const char* format, ...);

    // Manual flush - call on shutdown
    void Flush();

private:
    // Internal flush without lock (assumes caller holds lock)
    void FlushUnlocked();
};

// Global logger instance helper - initialize at startup
inline bool InitializeLogging(const char* filename = "debug.log")
{
    return CThreadSafeLogger::GetInstance().Initialize(filename);
}

// Helper macro for logging - use this instead of raw Log() calls
#define LOG_DEBUG(fmt, ...) CThreadSafeLogger::GetInstance().Log(fmt, __VA_ARGS__)
