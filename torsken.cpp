#include <array>
#include <circular_buffer.h>
#include <climits>
#include <condition_variable>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <malloc.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <thread>
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

struct Item
{
    Item() = default;
    Item(Type t, void *p, size_t s, unsigned long long tm)
        : type(t), ptr(p), size(s), time(tm)
    {}

    Item &operator=(const Item &) = default;

    Type type;
    void *ptr;
    size_t size;
    unsigned long long time;
    void *backtrace[BACKTRACE_COUNT];
    int backtraceCount;
};

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

static pthread_mutex_t sMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_cond_t sCond;
static pthread_t sThread;
static bool sStopped = false;
static bool sInInit = false;
static bool sEnabled = true;
static CircularBuffer<1024, Item> sItems;
static const unsigned long long sStarted = mono();
static size_t sThreshold = 32;
static int sFile = -1;

static void *thread(void *)
{
    while (true) {
        Item item;
        {
            pthread_mutex_lock(&sMutex);
            while (sItems.empty() && !sStopped)
                pthread_cond_wait(&sCond, &sMutex);
            const bool stopped = sStopped;
            if (!stopped) {
                sEnabled = false;
                item = sItems.pop_front();
                sEnabled = true;
            }
            pthread_mutex_unlock(&sMutex);
            if (stopped)
                break;
        }
        char buf[512];
        int w = snprintf(buf, sizeof(buf), "%c,%zx,%zu,%llu,%d\n",
                         item.type, reinterpret_cast<size_t>(item.ptr), item.size, item.time, item.backtraceCount - 2);
        // for (int i=2; i<count; ++i) {
        //     w += snprintf(buf + w, sizeof(buf) - w, ",%zx", reinterpret_cast<size_t>(backtrace[i]));
        // }
        // buf[w++] = '\n';
        ssize_t ret = write(sFile, buf, w);
        static_cast<void>(ret);
        backtrace_symbols_fd(item.backtrace + 2, item.backtraceCount - 2, sFile);
    }
    return nullptr;
}

static void init()
{
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

    // pthread_cond_init(&sCond, nullptr);
    // const size_t stackSize = 4096 * 64;
    // void *stackPointer = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    // pthread_attr_t attr;
    // pthread_attr_init(&attr);
    // pthread_attr_setstack(&attr, stackPointer, stackSize);
    // pthread_create(&sThread, &attr, thread, nullptr);
    // pthread_attr_destroy(&attr);
    pthread_create(&sThread, nullptr, thread, nullptr);

    atexit([]() {
        if (sFile != STDERR_FILENO) {
            fsync(sFile);
            ::close(sFile);
        }
    });

    // // get the pthread_once that happens inside of backtrace(3) to happen first
    // void *backtrace[1];
    // ::backtrace(backtrace, 1);

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

inline static void log(Type t, void *ptr, size_t size)
{
    if (size < sThreshold)
        return;
    pthread_mutex_lock(&sMutex);
    if (sEnabled) {
        sEnabled = false;
        const unsigned long long time = mono() - sStarted;
        sItems.append(Item(t, ptr, size, time));
        Item &item = sItems.last();
        item.backtraceCount = ::backtrace(&item.backtrace[0], BACKTRACE_COUNT);
        pthread_cond_signal(&sCond);
        // char buf[16384];
        // int w = snprintf(buf, sizeof(buf), "%c,%zx,%zu,%llu", t, reinterpret_cast<size_t>(ptr), size, time);
        // for (int i=2; i<count; ++i) {
        //     w += snprintf(buf + w, sizeof(buf) - w, ",%zx", reinterpret_cast<size_t>(backtrace[i]));
        // }
        // buf[w++] = '\n';
        // write(sFile, buf, w);
        // backtrace_symbols_fd(backtrace + 2, count - 2, sFile);
        sEnabled = true;
    }
    pthread_mutex_unlock(&sMutex);
}


extern "C" {
void *malloc(size_t size)
{
    if (!sRealMalloc)
        init();
    void *const ret = sRealMalloc(size);
    if (!sInInit)
        log(Malloc, ret, size);
    return ret;
}

unsigned char sInitCallocBuffer[1024];
void free(void *ptr)
{
    if (ptr == sInitCallocBuffer)
        return;
    // if (sInInit) {
    //     char buf[1024];
    //     const int w = snprintf(buf, sizeof(buf), "free %p\n", ptr);
    //     write(STDOUT_FILENO, buf, w);
    // }
    sRealFree(ptr);
    log(Free, ptr, 0);
}

void *calloc(size_t nmemb, size_t size)
{
    if (sInInit) {
        if (nmemb == 1 && size == 32) {
            // this is for dlsym and apparently nullptr is cool
            return nullptr;
        }
        // char buf[1024];
        // const int w = snprintf(buf, sizeof(buf), "calloc %zu %zu\n", nmemb, size);
        // write(STDOUT_FILENO, buf, w);
        // return nullptr;
        // this is for the thread's stack
        return sInitCallocBuffer;
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
}
