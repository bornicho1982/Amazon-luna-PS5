#!/usr/bin/env python3

path = "apps/luna-ps5/src/net/http_client.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

# Replace include and mutex definition
old_header = """#include <vector>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include "net/sce_http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace luna::net {

namespace {

static std::recursive_mutex g_httpMutex;"""

new_header = """#include <vector>
#include <atomic>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include "net/sce_http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
extern "C" uint64_t scePthreadSelf(void);
#endif

namespace luna::net {

namespace {

class AtomicRecursiveLock {
    std::atomic<uint64_t> owner_{0};
    uint32_t count_{0};
public:
    void lock() {
#if defined(_WIN32)
        uint64_t tid = static_cast<uint64_t>(GetCurrentThreadId());
#else
        uint64_t tid = scePthreadSelf();
#endif
        if (owner_.load(std::memory_order_relaxed) == tid) {
            count_++;
            return;
        }
        uint64_t expected = 0;
        while (!owner_.compare_exchange_weak(expected, tid, std::memory_order_acquire, std::memory_order_relaxed)) {
            expected = 0;
#if defined(_WIN32)
            Sleep(1);
#else
            struct timespec ts = {0, 1000000}; // 1ms
            nanosleep(&ts, nullptr);
#endif
        }
        count_ = 1;
    }

    void unlock() {
        if (--count_ == 0) {
            owner_.store(0, std::memory_order_release);
        }
    }
};

class AtomicRecursiveGuard {
    AtomicRecursiveLock& lock_;
public:
    explicit AtomicRecursiveGuard(AtomicRecursiveLock& l) : lock_(l) { lock_.lock(); }
    ~AtomicRecursiveGuard() { lock_.unlock(); }
};

static AtomicRecursiveLock g_httpLock;"""

content = content.replace("\r\n", "\n")
old_header = old_header.replace("\r\n", "\n")
new_header = new_header.replace("\r\n", "\n")

if old_header in content:
    content = content.replace(old_header, new_header)
    print("Header replaced successfully!")
else:
    print("Error: old_header not found!")

# Replace all lock_guard occurrences
old_lock = "std::lock_guard<std::recursive_mutex> lock(g_httpMutex);"
new_lock = "AtomicRecursiveGuard lock(g_httpLock);"

count = content.count(old_lock)
content = content.replace(old_lock, new_lock)
print(f"Replaced {count} lock occurrences.")

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("Saved http_client.cpp!")
