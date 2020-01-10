/*
 * Copyright (c) 2005, 2007, Google Inc. All rights reserved.
 * Copyright (C) 2005-2018 Apple Inc. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include <wtf/FastMalloc.h>

#include <limits>
#include <string.h>
#include <wtf/CheckedArithmetic.h>
#include <wtf/DataLog.h>

#if OS(WINDOWS)
#include <windows.h>
#else
#include <pthread.h>
#if HAVE(RESOURCE_H)
#include <sys/resource.h>
#endif // HAVE(RESOURCE_H)
#endif

#if OS(DARWIN)
#include <mach/mach_init.h>
#include <malloc/malloc.h>
#endif

namespace WTF {

#if !defined(NDEBUG)
namespace {
// We do not use std::numeric_limits<size_t>::max() here due to the edge case in VC++.
// https://bugs.webkit.org/show_bug.cgi?id=173720
static size_t maxSingleAllocationSize = SIZE_MAX;
};

void fastSetMaxSingleAllocationSize(size_t size)
{
    maxSingleAllocationSize = size;
}

#define ASSERT_IS_WITHIN_LIMIT(size) do { \
        size_t size__ = (size); \
        ASSERT_WITH_MESSAGE((size__) <= maxSingleAllocationSize, "Requested size (%zu) exceeds max single allocation size set for testing (%zu)", (size__), maxSingleAllocationSize); \
    } while (false)

#define FAIL_IF_EXCEEDS_LIMIT(size) do { \
        if (UNLIKELY((size) > maxSingleAllocationSize)) \
            return nullptr; \
    } while (false)

#else // !defined(NDEBUG)

#define ASSERT_IS_WITHIN_LIMIT(size)
#define FAIL_IF_EXCEEDS_LIMIT(size)

#endif // !defined(NDEBUG)

void* fastZeroedMalloc(size_t n)
{
    void* result = fastMalloc(n);
    memset(result, 0, n);
    return result;
}

char* fastStrDup(const char* src)
{
    size_t len = strlen(src) + 1;
    char* dup = static_cast<char*>(fastMalloc(len));
    memcpy(dup, src, len);
    return dup;
}

TryMallocReturnValue tryFastZeroedMalloc(size_t n)
{
    void* result;
    if (!tryFastMalloc(n).getValue(result))
        return 0;
    memset(result, 0, n);
    return result;
}

} // namespace WTF

#if defined(USE_SYSTEM_MALLOC) && USE_SYSTEM_MALLOC

#include <wtf/OSAllocator.h>

#if OS(WINDOWS)
#include <malloc.h>
#endif

namespace WTF {

bool isFastMallocEnabled()
{
    return false;
}

size_t fastMallocGoodSize(size_t bytes)
{
#if OS(DARWIN)
    return malloc_good_size(bytes);
#else
    return bytes;
#endif
}

#if OS(WINDOWS)

void* fastAlignedMalloc(size_t alignment, size_t size)
{
    ASSERT_IS_WITHIN_LIMIT(size);
    void* p = _aligned_malloc(size, alignment);
    if (size >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::fastAlignedMalloc() alignment = %d, size = %d, p = %p\n", alignment, size, p);
    }
    if (UNLIKELY(!p)) {
        fprintf(stderr, "CRASHING from FastMalloc.cpp:139::fastAlignedMalloc() alignment = %d, size = %d\n", alignment, size);
        CRASH();
    }
    return p;
}

void* tryFastAlignedMalloc(size_t alignment, size_t size)
{
    if (size >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::tryFastAlignedMalloc() alignment = %d, size = %d\n", alignment, size);
    }
    FAIL_IF_EXCEEDS_LIMIT(size);
    return _aligned_malloc(size, alignment);
}

void fastAlignedFree(void* p)
{
//    fprintf(stderr, "[JSY] FastMalloc::fastAlignedFree() p = %p\n", p);
    _aligned_free(p);
}

#else

void* fastAlignedMalloc(size_t alignment, size_t size)
{
    ASSERT_IS_WITHIN_LIMIT(size);
    void* p = nullptr;
    posix_memalign(&p, alignment, size);
    if (UNLIKELY(!p))
        CRASH();
    return p;
}

void* tryFastAlignedMalloc(size_t alignment, size_t size)
{
    FAIL_IF_EXCEEDS_LIMIT(size);
    void* p = nullptr;
    posix_memalign(&p, alignment, size);
    return p;
}

void fastAlignedFree(void* p)
{
    free(p);
}

#endif // OS(WINDOWS)

TryMallocReturnValue tryFastMalloc(size_t n)
{
    if (n > 1024 * 1024) {
        checkMemorySize();
    }

    FAIL_IF_EXCEEDS_LIMIT(n);
    void* result = malloc(n);
    if (n >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::tryFastMalloc() n = %d, result = %p\n", n, result);
    }
    return result;
}

void* fastMalloc(size_t n)
{
    if (n > 1024 * 1024) {
        checkMemorySize();
    }

    ASSERT_IS_WITHIN_LIMIT(n);
    void* result = malloc(n);
    if (n >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::fastMalloc() n = %d, result = %p\n", n, result);
    }
    if (!result) {
        fprintf(stderr, "CRASHING from FastMalloc.cpp:194::fastMalloc(), n = %d\n", n);
        CRASH();
    }

    return result;
}

void checkMemorySize() {
    size_t size = 1024 * 1024;
    do {
        void* allocated = malloc(size);
        if (allocated) {
            fastFree(allocated);
        } else {
            break;
        }
        size *= 2;
    } while (true);

    fprintf(stderr, "[JSY] could malloc() at least %d bytes", size);
}

TryMallocReturnValue tryFastCalloc(size_t n_elements, size_t element_size)
{
    if (n_elements * element_size >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::tryFastCalloc() n_elements = %d, element_size = %d\n", n_elements, element_size);
    }
    FAIL_IF_EXCEEDS_LIMIT(n_elements * element_size);
    return calloc(n_elements, element_size);
}

void* fastCalloc(size_t n_elements, size_t element_size)
{
    ASSERT_IS_WITHIN_LIMIT(n_elements * element_size);
    void* result = calloc(n_elements, element_size);
    if (n_elements * element_size >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::fastCalloc() n_elements = %d, element_size = %d, result = %p\n", n_elements, element_size, result);
    }
    if (!result) {
        fprintf(stderr, "CRASHING from FastMalloc.cpp:212::fastCalloc() n_elements = %d, element_size = %d\n", n_elements, element_size);
        CRASH();
    }

    return result;
}

void fastFree(void* p)
{
//    fprintf(stderr, "[JSY] FastMalloc::fastFree() p = %p\n", p);
    free(p);
}

void* fastRealloc(void* p, size_t n)
{
    ASSERT_IS_WITHIN_LIMIT(n);
    void* result = realloc(p, n);
    if (n >= 8192) {
        fprintf(stderr, "[JSY] FastMalloc::fastRealloc() n = %d, p = %d, result = %p\n", n, p, result);
    }
    if (!result) {
        fprintf(stderr, "CRASHING from FastMalloc.cpp:229::fastRealloc(), n = %d\n", n);
        CRASH();
    }
        
    return result;
}

TryMallocReturnValue tryFastRealloc(void* p, size_t n)
{
    FAIL_IF_EXCEEDS_LIMIT(n);
    return realloc(p, n);
}

void releaseFastMallocFreeMemory() { }
void releaseFastMallocFreeMemoryForThisThread() { }

FastMallocStatistics fastMallocStatistics()
{
    FastMallocStatistics statistics = { 0, 0, 0 };
    return statistics;
}

size_t fastMallocSize(const void* p)
{
#if OS(DARWIN)
    return malloc_size(p);
#elif OS(WINDOWS)
    return _msize(const_cast<void*>(p));
#else
    UNUSED_PARAM(p);
    return 1;
#endif
}

void fastCommitAlignedMemory(void* ptr, size_t size)
{
    OSAllocator::commit(ptr, size, true, false);
}

void fastDecommitAlignedMemory(void* ptr, size_t size)
{
    OSAllocator::decommit(ptr, size);
}

void fastEnableMiniMode() { }

} // namespace WTF

#else // defined(USE_SYSTEM_MALLOC) && USE_SYSTEM_MALLOC

#include <bmalloc/bmalloc.h>

namespace WTF {

bool isFastMallocEnabled()
{
    return bmalloc::api::isEnabled();
}

void* fastMalloc(size_t size)
{
    ASSERT_IS_WITHIN_LIMIT(size);
    return bmalloc::api::malloc(size);
}

void* fastCalloc(size_t numElements, size_t elementSize)
{
    ASSERT_IS_WITHIN_LIMIT(numElements * elementSize);
    Checked<size_t> checkedSize = elementSize;
    checkedSize *= numElements;
    void* result = fastZeroedMalloc(checkedSize.unsafeGet());
    if (!result) {
        fprintf(stderr, "CRASHING from FastMalloc.cpp:301::fastCalloc(), numElements = %d, elementSize = %d\n", numElements, elementSize);
        CRASH();
    }
    return result;
}

void* fastRealloc(void* object, size_t size)
{
    ASSERT_IS_WITHIN_LIMIT(size);
    return bmalloc::api::realloc(object, size);
}

void fastFree(void* object)
{
    bmalloc::api::free(object);
}

size_t fastMallocSize(const void*)
{
    // FIXME: This is incorrect; best fix is probably to remove this function.
    // Caller currently are all using this for assertion, not to actually check
    // the size of the allocation, so maybe we can come up with something for that.
    return 1;
}

size_t fastMallocGoodSize(size_t size)
{
    return size;
}

void* fastAlignedMalloc(size_t alignment, size_t size)
{
    ASSERT_IS_WITHIN_LIMIT(size);
    return bmalloc::api::memalign(alignment, size);
}

void* tryFastAlignedMalloc(size_t alignment, size_t size)
{
    FAIL_IF_EXCEEDS_LIMIT(size);
    return bmalloc::api::tryMemalign(alignment, size);
}

void fastAlignedFree(void* p)
{
    bmalloc::api::free(p);
}

TryMallocReturnValue tryFastMalloc(size_t size)
{
    FAIL_IF_EXCEEDS_LIMIT(size);
    return bmalloc::api::tryMalloc(size);
}

TryMallocReturnValue tryFastCalloc(size_t numElements, size_t elementSize)
{
    FAIL_IF_EXCEEDS_LIMIT(numElements * elementSize);
    Checked<size_t, RecordOverflow> checkedSize = elementSize;
    checkedSize *= numElements;
    if (checkedSize.hasOverflowed())
        return nullptr;
    return tryFastZeroedMalloc(checkedSize.unsafeGet());
}

TryMallocReturnValue tryFastRealloc(void* object, size_t newSize)
{
    FAIL_IF_EXCEEDS_LIMIT(newSize);
    return bmalloc::api::tryRealloc(object, newSize);
}

void releaseFastMallocFreeMemoryForThisThread()
{
    bmalloc::api::scavengeThisThread();
}

void releaseFastMallocFreeMemory()
{
    bmalloc::api::scavenge();
}

FastMallocStatistics fastMallocStatistics()
{

    // FIXME: Can bmalloc itself report the stats instead of relying on the OS?
    FastMallocStatistics statistics;
    statistics.freeListBytes = 0;
    statistics.reservedVMBytes = 0;

#if OS(WINDOWS)
    PROCESS_MEMORY_COUNTERS resourceUsage;
    GetProcessMemoryInfo(GetCurrentProcess(), &resourceUsage, sizeof(resourceUsage));
    statistics.committedVMBytes = resourceUsage.PeakWorkingSetSize;
#elif HAVE(RESOURCE_H)
    struct rusage resourceUsage;
    getrusage(RUSAGE_SELF, &resourceUsage);

#if OS(DARWIN)
    statistics.committedVMBytes = resourceUsage.ru_maxrss;
#else
    statistics.committedVMBytes = resourceUsage.ru_maxrss * 1024;
#endif // OS(DARWIN)

#endif // OS(WINDOWS)
    return statistics;
}

void fastCommitAlignedMemory(void* ptr, size_t size)
{
    bmalloc::api::commitAlignedPhysical(ptr, size);
}

void fastDecommitAlignedMemory(void* ptr, size_t size)
{
    bmalloc::api::decommitAlignedPhysical(ptr, size);
}

void fastEnableMiniMode()
{
    bmalloc::api::enableMiniMode();
}

} // namespace WTF

#endif // defined(USE_SYSTEM_MALLOC) && USE_SYSTEM_MALLOC
