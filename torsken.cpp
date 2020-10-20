#include <array>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <execinfo.h>
#include <mutex>
#include <unordered_map>

#ifndef BACKTRACE_COUNT
#define BACKTRACE_COUNT 32
#endif

struct Allocations {
    Allocations(Allocations &&) = default;
    Allocations() = default;
    Allocations(size_t s)
        : size(s)
    {
        ::backtrace(&backtrace[0], BACKTRACE_COUNT);
    }

    Allocations &operator=(Allocations &&) = default;

    size_t size { 0 };
    std::array<void *, BACKTRACE_COUNT> backtrace {};
private:
    Allocations(const Allocations &) = delete;
    Allocations &operator=(const Allocations &) = delete;
};

void *(*sRealMalloc)(size_t);
void (*sRealFree)(void *);
void *(*sRealCalloc)(size_t, size_t);
void *(*sRealRealloc)(void *, size_t);
void *(*sRealReallocarray)(void *, size_t, size_t);

std::unordered_map<void *, Allocations> sAllocations;
bool sEnabled = false;
std::recursive_mutex sMutex;

static void dump()
{
    FILE *f = stderr;
    bool close = false;
    if (const char *TORSKEN_OUTPUT = getenv("TORSKEN_OUTPUT")) {
        if (!strcmp(TORSKEN_OUTPUT, "stdout")) {
            f = stdout;
        } else if (!strcmp(TORSKEN_OUTPUT, "stderr")) {
            f = stderr;
        } else {
            f = fopen(TORSKEN_OUTPUT, "w");
            if (!f) {
                fprintf(stderr, "Failed to open %s for writing\n", TORSKEN_OUTPUT);
                f = stderr;
            } else {
                close = true;
            }
        }
    }

    for (const auto &ref : sAllocations) {
        fprintf(f, "%p:", ref.first);
        for (size_t i=0; i<BACKTRACE_COUNT && ref.second.backtrace[i]; ++i) {
            fprintf(f, " %p", ref.second.backtrace[i]);
        }
        fwrite("\n", 1, 1, f);
    }

    if (close)
        fclose(f);
}

static std::once_flag sOnce;

static const bool initialized = ([]() {
    sRealMalloc = reinterpret_cast<decltype(sRealMalloc)>(dlsym(RTLD_NEXT, "malloc"));
    sRealFree = reinterpret_cast<decltype(sRealFree)>(dlsym(RTLD_NEXT, "free"));
    sRealCalloc = reinterpret_cast<decltype(sRealCalloc)>(dlsym(RTLD_NEXT, "calloc"));
    sRealRealloc = reinterpret_cast<decltype(sRealRealloc)>(dlsym(RTLD_NEXT, "realloc"));
    sRealReallocarray = reinterpret_cast<decltype(sRealReallocarray)>(dlsym(RTLD_NEXT, "reallocarray"));
    printf("GOT HERE %p %p\n", sRealReallocarray, reallocarray);
    atexit(dump);
    return true;
})();

extern "C" {
void *malloc2(size_t size)
{
    printf("FUCKING MALLOC %zu\n", size);
    fflush(stdout);
    sleep(4);
    std::unique_lock<std::recursive_mutex> lock(sMutex);
    void *ret = sRealMalloc(size);
    if (sEnabled) {
        sEnabled = false;
        sAllocations[ret] = Allocations(size);
        sEnabled = true;
    }
    return ret;
}

// void free(void *ptr)
// {
//     std::unique_lock<std::recursive_mutex> lock(sMutex);
//     sRealFree(ptr);
//     if (sEnabled) {
//         sEnabled = false;
//         sAllocations.erase(ptr);
//         sEnabled = true;
//     }
// }

// void *calloc(size_t nmemb, size_t size)
// {
//     std::unique_lock<std::recursive_mutex> lock(sMutex);
//     void *ret = sRealCalloc(nmemb, size);
//     if (sEnabled) {
//         sEnabled = false;
//         sAllocations[ret] = nmemb * size;
//         sEnabled = true;
//     }
//     return ret;
// }

// void *realloc(void *ptr, size_t size)
// {
//     std::unique_lock<std::recursive_mutex> lock(sMutex);
//     void *ret = sRealRealloc(ptr, size);
//     if (sEnabled) {
//         sEnabled = false;
//         if (ptr != ret) {
//             sAllocations.erase(ptr);
//         }
//         sAllocations[ret] = size;
//         sEnabled = true;
//     }
//     return ret;
// }

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    std::unique_lock<std::recursive_mutex> lock(sMutex);
    void *ret = sRealReallocarray(ptr, nmemb, size);
    if (sEnabled) {
        sEnabled = false;
        if (ptr != ret) {
            sAllocations.erase(ptr);
        }
        sAllocations[ret] = nmemb * size;
        sEnabled = true;
    }
    return ret;
}
}
