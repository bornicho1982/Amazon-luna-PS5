// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "util/crash_logger.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace luna::util {

namespace {

#if defined(_WIN32)
int syncFileDescriptor(FILE* f) {
    return _commit(_fileno(f));
}
#else
int syncFileDescriptor(FILE* f) {
    return fsync(fileno(f));
}
#endif

std::string g_logFile = "/data/homebrew/PPSA99825/luna_trace.log";
std::mutex g_logMutex;
std::string g_currentStep;
bool g_initialized = false;
}

void CrashLogger::init(const std::string& logPath) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile = logPath;

    // Preserve previous session log before truncating
    if (FILE* oldF = std::fopen(g_logFile.c_str(), "rb")) {
        std::fseek(oldF, 0, SEEK_END);
        long sz = std::ftell(oldF);
        if (sz > 100) {
            std::fseek(oldF, 0, SEEK_SET);
            std::string prevPath = g_logFile + ".prev";
            if (FILE* prevF = std::fopen(prevPath.c_str(), "wb")) {
                char copyBuf[4096];
                size_t n = 0;
                while ((n = std::fread(copyBuf, 1, sizeof(copyBuf), oldF)) > 0) {
                    std::fwrite(copyBuf, 1, n, prevF);
                }
                std::fflush(prevF);
                syncFileDescriptor(prevF);
                std::fclose(prevF);
            }
        }
        std::fclose(oldF);
    }

    FILE* f = std::fopen(g_logFile.c_str(), "w");
    if (!f) {
        g_logFile = "./luna_trace.log";
        f = std::fopen(g_logFile.c_str(), "w");
    }
    if (f) {
        std::fprintf(f, "=== LUNA PS5 TRACE LOG INITIALIZED ===\n");
        std::fflush(f);
        syncFileDescriptor(f);
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
        syncFileDescriptor(f);
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
