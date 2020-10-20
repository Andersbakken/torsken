#include <array>
#include <thread>
#include <climits>
#include <dlfcn.h>
#include <execinfo.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#ifndef BACKTRACE_COUNT
#define BACKTRACE_COUNT 32
#endif

static inline unsigned long long mono()
{
    timespec ts;
#if defined(__APPLE__)
    static double sTimebase = 0.0;
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, []() -> void {
        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        sTimebase = tb.numer;
        sTimebase /= tb.denom;
    });
    const double time = mach_absolute_time() * sTimebase;
    ts.tv_sec = time * +1.0E-9;
    ts.tv_nsec = time - (ts->tv_sec * static_cast<uint64_t>(1000000000));
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (static_cast<unsigned long long>(ts.tv_sec) * 1000ull) + (static_cast<unsigned long long>(ts.tv_nsec) / 1000000ull);
}

struct Allocation {
    Allocation(Allocation &&) = default;
    Allocation() = default;
    Allocation(size_t s) : size(s), time(mono())
    {
        ::backtrace(&backtrace[0], BACKTRACE_COUNT);
    }

    Allocation &operator=(Allocation &&) = default;

    size_t size { 0 };
    unsigned long long time { 0 };
    std::array<void *, BACKTRACE_COUNT> backtrace {};

private:
    Allocation(const Allocation &) = delete;
    Allocation &operator=(const Allocation &) = delete;
};

static void *(*sRealMalloc)(size_t);
static void (*sRealFree)(void *);
static void *(*sRealCalloc)(size_t, size_t);
static void *(*sRealRealloc)(void *, size_t);
static void *(*sRealReallocarray)(void *, size_t, size_t);
static bool sEnabled = true;
static bool sInInit = false;
static bool sExit = false;
static char sDumpDir[PATH_MAX];
static size_t sDumpIndex = 0;
static pthread_t sThread = 0;
static const unsigned long long sStarted = mono();

static std::unordered_map<void *, Allocation> &sAllocations()
{
    static std::unordered_map<void *, Allocation> s;
    return s;
}

static std::recursive_mutex &sMutex()
{
    static std::recursive_mutex s;
    return s;
}

static void dump()
{
    char buf[PATH_MAX + 16];
    snprintf(buf, sizeof(buf), "%s/%05zu", sDumpDir, ++sDumpIndex);
    FILE *f = fopen(buf, "w");
    if (!f)
        return;

    for (const auto &ref : sAllocations()) {
        int w = snprintf(buf, sizeof(buf), "%p,%zu,%llu", ref.first, ref.second.size, ref.second.time - sStarted);
        fwrite(buf, 1, w, f);
        for (size_t i = 0; i < BACKTRACE_COUNT && ref.second.backtrace[i]; ++i) {
            w = snprintf(buf, sizeof(buf), ",%p", ref.second.backtrace[i]);
            fwrite(buf, 1, w, f);
        }
        fwrite("\n", 1, 1, f);
    }
    fclose(f);
}

static void recursive_mkdir(const char *dir)
{
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

static void init()
{
    sInInit = true;
    sRealMalloc = reinterpret_cast<decltype(sRealMalloc)>(dlsym(RTLD_NEXT, "malloc"));
    sRealFree = reinterpret_cast<decltype(sRealFree)>(dlsym(RTLD_NEXT, "free"));
    sRealCalloc = reinterpret_cast<decltype(sRealCalloc)>(dlsym(RTLD_NEXT, "calloc"));
    sRealRealloc = reinterpret_cast<decltype(sRealRealloc)>(dlsym(RTLD_NEXT, "realloc"));
    sRealReallocarray = reinterpret_cast<decltype(sRealReallocarray)>(dlsym(RTLD_NEXT, "reallocarray"));
    atexit([]() {
        {
            std::unique_lock<std::recursive_mutex> lock(sMutex());
            sExit = true;
        }
        if (sThread) {
            pthread_join(sThread, nullptr);
        }
    });
    sInInit = false;

    const char *dir = getenv("TORSKEN_DIR");
    if (dir) {
        snprintf(sDumpDir, sizeof(sDumpDir), "%s", dir);
    } else {
        snprintf(sDumpDir, sizeof(sDumpDir), "/%s/torsken_%d", getenv("PWD"), getpid());
    }

    if (const char *interval = getenv("TORSKEN_INTERVAL")) {
        const uintptr_t ms = std::max(500, atoi(interval));
        pthread_create(&sThread, nullptr, [](void *arg) -> void * {
            const useconds_t sleepTime = reinterpret_cast<uintptr_t>(arg) * 1000;
            while (true) {
                {
                    std::unique_lock<std::recursive_mutex> lock(sMutex());
                    if (sExit)
                        break;
                    dump();
                }
                usleep(sleepTime);
            }
            return nullptr;
        }, reinterpret_cast<void*>(ms));
    }

    recursive_mkdir(sDumpDir);
}

extern "C" {
void *malloc(size_t size)
{
    if (!sRealMalloc)
        init();
    std::unique_lock<std::recursive_mutex> lock(sMutex());
    void *ret = sRealMalloc(size);
    if (sEnabled) {
        sEnabled = false;
        sAllocations()[ret] = Allocation(size);
        sEnabled = true;
    }
    return ret;
}
static unsigned char sBuffer[8 * 1024];
void free(void *ptr)
{
    if (ptr == sBuffer)
        return;

    if (!sRealMalloc)
        init();
    std::unique_lock<std::recursive_mutex> lock(sMutex());
    sRealFree(ptr);
    if (sEnabled) {
        sEnabled = false;
        sAllocations().erase(ptr);
        sEnabled = true;
    }
}

void *calloc(size_t nmemb, size_t size)
{
    if (sInInit) {
        memset(&sBuffer, 0, sizeof(sBuffer));
        return sBuffer;
    }

    std::unique_lock<std::recursive_mutex> lock(sMutex());
    if (!sRealMalloc)
        init();
    void *ret = sRealCalloc(nmemb, size);
    if (sEnabled) {
        sEnabled = false;
        sAllocations()[ret] = nmemb * size;
        sEnabled = true;
    }
    return ret;
}

void *realloc(void *ptr, size_t size)
{
    if (!sRealMalloc)
        init();
    std::unique_lock<std::recursive_mutex> lock(sMutex());
    void *ret = sRealRealloc(ptr, size);
    if (sEnabled) {
        sEnabled = false;
        if (ptr != ret) {
            sAllocations().erase(ptr);
        }
        sAllocations()[ret] = size;
        sEnabled = true;
    }
    return ret;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    if (!sRealMalloc)
        init();
    std::unique_lock<std::recursive_mutex> lock(sMutex());
    void *ret = sRealReallocarray(ptr, nmemb, size);
    if (sEnabled) {
        sEnabled = false;
        if (ptr != ret) {
            sAllocations().erase(ptr);
        }
        sAllocations()[ret] = nmemb * size;
        sEnabled = true;
    }
    return ret;
}
}
