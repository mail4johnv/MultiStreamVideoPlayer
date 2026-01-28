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

// Minimal include to avoid /clr conflicts
using namespace System;

namespace MediaFoundation {
    namespace Player {

        /// <summary>
        /// C++/CLI wrapper that bridges native CThreadSafeLogger to managed code.
        /// Handles string conversions and provides thread-safe logging interface.
        /// </summary>
        public ref class ManagedLogger
        {
        public:
            /// <summary>
            /// Initialize the native logger with a log file path.
            /// Thread-safe and idempotent.
            /// </summary>
            /// <param name="filename">Path to the log file</param>
            /// <returns>True if initialization succeeded, false otherwise</returns>
            static bool Initialize(String^ filename);

            /// <summary>
            /// Check if logger has been initialized.
            /// </summary>
            /// <returns>True if logger is initialized, false otherwise</returns>
            static bool IsInitialized();

            /// <summary>
            /// Log a message to the native logger.
            /// </summary>
            /// <param name="message">Message to log</param>
            static void Log(String^ message);

            /// <summary>
            /// Log an error message.
            /// </summary>
            /// <param name="message">Error message</param>
            static void LogError(String^ message);

            /// <summary>
            /// Log a warning message.
            /// </summary>
            /// <param name="message">Warning message</param>
            static void LogWarning(String^ message);

            /// <summary>
            /// Log an informational message.
            /// </summary>
            /// <param name="message">Info message</param>
            static void LogInfo(String^ message);

            /// <summary>
            /// Flush the log buffer to disk.
            /// </summary>
            static void Flush();

        private:
            // Private constructor - utility class, not instantiable
            ManagedLogger() {}
        };

    } // end namespace Player
} // end namespace MediaFoundation
