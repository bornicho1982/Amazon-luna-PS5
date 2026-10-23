#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Synchronous disk and memory crash-logger for PS5 eboot.bin */

#include <string>

namespace luna::util {

class CrashLogger {
public:
    static void init(const std::string& logPath = "/data/homebrew/PPSA99825/luna_trace.log");
    static void log(const char* fmt, ...);
    static void logStep(int stepNum, const char* desc);
    static std::string getCurrentStepText();
};

} // namespace luna::util
