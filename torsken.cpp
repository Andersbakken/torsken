#include <array>
#include <climits>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <malloc.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

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

static void *(*sRealMalloc)(size_t);
static void (*sRealFree)(void *);
static void *(*sRealCalloc)(size_t, size_t);
static void *(*sRealRealloc)(void *, size_t);
static void *(*sRealReallocarray)(void *, size_t, size_t);
static int (*sRealPosix_memalign)(void **, size_t, size_t);
static void *(*sRealAligned_alloc)(size_t, size_t);
static void *(*sRealValloc)(size_t size);
static void *(*sRealMemalign)(size_t alignment, size_t size);
static void *(*sRealPvalloc)(size_t size);
// static int (*sRealBacktrace)(void **, int);
// static char **(*sRealBacktrace_symbols)(void *const *, int);
// static void (*sRealBacktrace_symbols_fd)(void *const *, int, int);

static bool sInInit = false;
static bool sEnabled = true;
static std::recursive_mutex sMutex;
static const unsigned long long sStarted = mono();
static size_t sThreshold = 32;
static int sFile = -1;
// static size_t sBacktraceCount = 32;

enum Type {
    Malloc = 'm',
    Free = 'f',
    Realloc = 'r',
    Calloc = 'c',
    ReallocArray = 'a',
    PosixMemalign = 'p',
    AlignedAlloc = 'A',
    Valloc = 'v',
    Memalign = 'm',
    PValloc = 'P'
};

inline static void log(Type t, void *ptr, size_t size)
{
    if (size < sThreshold)
        return;
    void *backtrace[128];
    const int count = ::backtrace(&backtrace[0], 32);
    std::unique_lock<std::recursive_mutex> lock(sMutex);
    if (!sEnabled)
        return;
    sEnabled = false;
    const unsigned long long time = mono() - sStarted;
    char buf[16384];
    int w = snprintf(buf, sizeof(buf), "%c,%zx,%zu,%llu", t, reinterpret_cast<size_t>(ptr), size, time);
    for (int i=2; i<count; ++i) {
        w += snprintf(buf + w, sizeof(buf) - w, ",%zx", reinterpret_cast<size_t>(backtrace[i]));
    }
    buf[w++] = '\n';
    ssize_t ret = write(sFile, buf, w);
    static_cast<void>(ret);
    // backtrace_symbols_fd(backtrace + 2, count - 2, sFile);
    sEnabled = true;
}

static void init()
{
    // sleep(5);
    sInInit = true;
    sRealMalloc = reinterpret_cast<decltype(sRealMalloc)>(dlsym(RTLD_NEXT, "malloc"));
    sRealFree = reinterpret_cast<decltype(sRealFree)>(dlsym(RTLD_NEXT, "free"));
    sRealCalloc = reinterpret_cast<decltype(sRealCalloc)>(dlsym(RTLD_NEXT, "calloc"));
    sRealRealloc = reinterpret_cast<decltype(sRealRealloc)>(dlsym(RTLD_NEXT, "realloc"));
    sRealReallocarray = reinterpret_cast<decltype(sRealReallocarray)>(dlsym(RTLD_NEXT, "reallocarray"));
    sRealPosix_memalign = reinterpret_cast<decltype(sRealPosix_memalign)>(dlsym(RTLD_NEXT, "posix_memalign"));
    sRealAligned_alloc = reinterpret_cast<decltype(sRealAligned_alloc)>(dlsym(RTLD_NEXT, "aligned_alloc"));
    sRealValloc = reinterpret_cast<decltype(sRealValloc)>(dlsym(RTLD_NEXT, "valloc"));
    sRealMemalign = reinterpret_cast<decltype(sRealMemalign)>(dlsym(RTLD_NEXT, "memalign"));
    sRealPvalloc = reinterpret_cast<decltype(sRealPvalloc)>(dlsym(RTLD_NEXT, "pvalloc"));
    // sRealBacktrace = reinterpret_cast<decltype(sRealBacktrace)>(dlsym(RTLD_NEXT, "backtrace"));
    // sRealBacktrace_symbols = reinterpret_cast<decltype(sRealBacktrace_symbols)>(dlsym(RTLD_NEXT, "backtrace_symbols"));
    // sRealBacktrace_symbols_fd = reinterpret_cast<decltype(sRealBacktrace_symbols_fd)>(dlsym(RTLD_NEXT, "backtrace_symbols_fd"));

    atexit([]() {
        if (sFile != STDERR_FILENO) {
            fsync(sFile);
            ::close(sFile);
        }
    });

    // get the pthread_conce that happens inside of backtrace(3) to happen first
    void *backtrace[1];
    ::backtrace(backtrace, 1);

    if (const char *path = getenv("TORSK_OUTPUT")) {
        sFile = open(path, O_CREAT, 0664);
    } else {
        // char buf[1024];
        // snprintf(buf, sizeof(buf), "torsken_%d", getpid());
        // sFile = open(buf, O_CREAT, 0664);
        sFile = STDERR_FILENO;

    }
    if (const char *threshold = getenv("TORSK_THRESHOLD")) {
        sThreshold = std::max(0, atoi(threshold));
    }

    if (sFile == -1) {
        abort();
    }
    sInInit = false;
}

extern "C" {
void *malloc(size_t size)
{
    if (!sRealMalloc)
        init();
    void *const ret = sRealMalloc(size);
    log(Malloc, ret, size);
    return ret;
}

void free(void *ptr)
{
    sRealFree(ptr);
    log(Free, ptr, 0);
}

void *calloc(size_t nmemb, size_t size)
{
    if (sInInit) {
        return nullptr;
    }

    if (!sRealCalloc)
        init();

    void *const ret = sRealCalloc(nmemb, size);
    log(Calloc, ret, nmemb * size);
    return ret;
}

void *realloc(void *ptr, size_t size)
{
    void *const ret = sRealRealloc(ptr, size);
    log(Realloc, ret, size);
    return ret;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    void *const ret = sRealReallocarray(ptr, nmemb, size);
    log(ReallocArray, ret, size);
    return ret;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    const int ret = sRealPosix_memalign(memptr, alignment, size);
    log(PosixMemalign, *memptr, size);
    return ret;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    void *const ret = sRealAligned_alloc(alignment, size);
    log(AlignedAlloc, ret, size);
    return ret;
}

void *valloc(size_t size)
{
    void *const ret = sRealValloc(size);
    log(Valloc, ret, size);
    return ret;
}

void *memalign(size_t alignment, size_t size)
{
    void *const ret = sRealMemalign(alignment, size);
    log(Memalign, ret, size);
    return ret;
}

void *pvalloc(size_t size)
{
    void *const ret = sRealPvalloc(size);
    log(PValloc, ret, size);
    return ret;
}

// int backtrace(void **buffer, int size)
// {
//     // std::unique_lock<std::recursive_mutex> lock(sMutex);
//     // if (size != sBacktraceCount || true) {
//     //     write(STDERR_FILENO, "BALLS1\n", 7);
//     //     char buf[1024];
//     //     int w = snprintf(buf, sizeof(buf), "%d\n", size);
//     //     write(STDERR_FILENO, buf, w);
//     //     _exit(1);
//     // }
//     return sRealBacktrace(buffer, size);
// }

// char **backtrace_symbols(void *const *buffer, int size)
// {
//     // write(STDERR_FILENO, "BALLS2\n", 7);
//     // _exit(1);
//     // std::unique_lock<std::recursive_mutex> lock(sMutex);
//     return sRealBacktrace_symbols(buffer, size);
// }

// void backtrace_symbols_fd(void *const *buffer, int size, int fd)
// {
//     // write(STDERR_FILENO, "BALLS3\n", 7);
//     // _exit(1);
//     // std::unique_lock<std::recursive_mutex> lock(sMutex);
//     return sRealBacktrace_symbols_fd(buffer, size, fd);
// }
}
