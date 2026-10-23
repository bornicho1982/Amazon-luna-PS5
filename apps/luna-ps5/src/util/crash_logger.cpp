// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "util/crash_logger.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace luna::util {

namespace {
std::string g_logFile = "/data/homebrew/PPSA99825/luna_trace.log";
std::mutex g_logMutex;
std::string g_currentStep;
bool g_initialized = false;
}

void CrashLogger::init(const std::string& logPath) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile = logPath;
    FILE* f = std::fopen(g_logFile.c_str(), "w");
    if (!f) {
        g_logFile = "./luna_trace.log";
        f = std::fopen(g_logFile.c_str(), "w");
    }
    if (f) {
        std::fprintf(f, "=== LUNA PS5 TRACE LOG INITIALIZED ===\n");
        std::fflush(f);
        fsync(fileno(f));
        std::fclose(f);
    }
    g_initialized = true;
}

void CrashLogger::log(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::printf("%s\n", buf);

    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* f = std::fopen(g_logFile.c_str(), "a");
    if (!f && !g_initialized) {
        g_logFile = "./luna_trace.log";
        f = std::fopen(g_logFile.c_str(), "a");
    }
    if (f) {
        std::fprintf(f, "%s\n", buf);
        std::fflush(f);
        fsync(fileno(f));
        std::fclose(f);
    }
}

void CrashLogger::logStep(int stepNum, const char* desc) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "[STEP %d] %s", stepNum, desc);
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_currentStep = buf;
    }
    log("%s", buf);
}

std::string CrashLogger::getCurrentStepText() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_currentStep;
}

} // namespace luna::util
