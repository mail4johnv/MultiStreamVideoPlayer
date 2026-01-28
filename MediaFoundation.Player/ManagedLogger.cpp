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

#include "ManagedLogger.h"
#include <cstdlib>
#include <vcclr.h>

// Forward declare C-style native functions to avoid /clr conflicts
extern "C"
{
    bool InitializeLogger(const char* filename);
    void LogMessage(const char* message);
    void FlushLogger();
	bool IsLoggerInitialized();
}

using namespace System;

// Helper to convert managed string to native char array
// This avoids including <msclr/marshal.h> which can conflict with /clr
static void ManagedToNative(String^ input, char* output, int maxlen)
{
    if (!input || !output || maxlen <= 0)
        return;

    pin_ptr<const wchar_t> wch = PtrToStringChars(input);
    wcstombs_s(nullptr, output, maxlen, wch, _TRUNCATE);
}

namespace MediaFoundation {
    namespace Player {

        bool ManagedLogger::Initialize(String^ filename)
        {
            if (IsLoggerInitialized())
                return true;  // Already initialized

            if (filename == nullptr)
                return false;

            try
            {
                char buffer[260];  // MAX_PATH
                ManagedToNative(filename, buffer, sizeof(buffer));
                bool success = InitializeLogger(buffer);
                return success;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ManagedLogger::IsInitialized()
        {
            return IsLoggerInitialized();
        }

        void ManagedLogger::Log(String^ message)
        {
            if (message == nullptr)
                return;

            try
            {
                char buffer[2048];
                ManagedToNative(message, buffer, sizeof(buffer));
                LogMessage(buffer);
            }
            catch (...)
            {
                // Silently fail to avoid exceptions in logging code
            }
        }

        void ManagedLogger::LogError(String^ message)
        {
            if (message == nullptr)
                return;

            try
            {
                String^ formattedMessage = "[ERROR] " + message;
                char buffer[2048];
                ManagedToNative(formattedMessage, buffer, sizeof(buffer));
                LogMessage(buffer);
            }
            catch (...)
            {
            }
        }

        void ManagedLogger::LogWarning(String^ message)
        {
            if (message == nullptr)
                return;

            try
            {
                String^ formattedMessage = "[WARNING] " + message;
                char buffer[2048];
                ManagedToNative(formattedMessage, buffer, sizeof(buffer));
                LogMessage(buffer);
            }
            catch (...)
            {
            }
        }

        void ManagedLogger::LogInfo(String^ message)
        {
            if (message == nullptr)
                return;

            try
            {
                String^ formattedMessage = "[INFO] " + message;
                char buffer[2048];
                ManagedToNative(formattedMessage, buffer, sizeof(buffer));
                LogMessage(buffer);
            }
            catch (...)
            {
            }
        }


        void ManagedLogger::Flush()
        {
            try
            {
                FlushLogger();
            }
            catch (...)
            {
                // Silently fail
            }
        }

    } // end namespace Player
} // end namespace MediaFoundation
