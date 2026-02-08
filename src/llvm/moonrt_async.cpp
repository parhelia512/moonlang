// MoonLang Runtime - Async & Timer Support (Coroutine-based)
// Copyright (c) 2026 greenteng.com
//
// "moon" keyword now uses lightweight coroutines (like Go goroutines)
// Supports millions of concurrent tasks with minimal memory overhead

// macOS: Must include sys/types.h BEFORE _XOPEN_SOURCE to get BSD types
#ifdef __APPLE__
#include <sys/types.h>
#define _XOPEN_SOURCE 600
#endif

#include "moonrt_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <malloc.h>  // for _aligned_malloc
#else
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sched.h>
#include <time.h>
#include <semaphore.h>
#ifdef __APPLE__
#include <os/lock.h>  // for os_unfair_lock (macOS spinlock replacement)
#include <sys/sysctl.h>  // for sysctlbyname to get CPU count

// macOS uses MAP_ANON instead of MAP_ANONYMOUS
#ifndef MAP_ANONYMOUS
#define MAP_ANON 0x1000
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#endif

// ============================================================================
// Configuration - Optimized for millions of coroutines
// ============================================================================

// Platform-specific coroutine stack size
#ifdef _WIN32
#define CORO_STACK_SIZE     (1024 * 1024)   // Windows: 1MB (for heavy workloads with many local vars)
#else
#define CORO_STACK_SIZE     (1024 * 1024)   // Linux/macOS: 1MB
#endif
#define CORO_QUEUE_SIZE     (1024 * 1024)   // 1M queue capacity
#define CORO_LOCAL_BATCH    256             // Local queue batch size
#define CORO_POOL_SIZE      4096            // Pool size for Coroutine struct reuse

// Coroutine configuration - designed for millions of concurrent coroutines
// No artificial limits - let the OS handle resource management
// 64KB stack × 1M coroutines = 64GB virtual address space (OK on 64-bit)

// ============================================================================
// Coroutine Structure (< 100 bytes overhead per task)
// ============================================================================

typedef enum {
    CORO_READY = 0,
    CORO_RUNNING,
    CORO_WAITING,
    CORO_DONE
} CoroState;

typedef struct Coroutine {
    int id;
    CoroState state;
    MoonFunc func;
    MoonValue** args;
    int argc;
    MoonValue* inline_args[4];  // Inline storage for up to 4 args (avoid malloc)
    
    // Closure support
    MoonValue** captures;
    int capture_count;
    
#ifdef _WIN32
    void* fiber;
    void* main_fiber;
#else
    ucontext_t ctx;
    ucontext_t* main_ctx;
    char* stack;
#endif
    
    struct Coroutine* next;
} Coroutine;

// ============================================================================
// Coroutine Pool (reduces malloc/free pressure)
// ============================================================================

static Coroutine* g_coro_pool[CORO_POOL_SIZE];
static volatile long g_coro_pool_count = 0;
#ifdef _WIN32
static SRWLOCK g_coro_pool_lock = SRWLOCK_INIT;
#define coro_pool_lock() AcquireSRWLockExclusive(&g_coro_pool_lock)
#define coro_pool_unlock() ReleaseSRWLockExclusive(&g_coro_pool_lock)
#else
static pthread_spinlock_t g_coro_pool_lock;
static bool g_coro_pool_lock_init = false;
#define coro_pool_lock() pthread_spin_lock(&g_coro_pool_lock)
#define coro_pool_unlock() pthread_spin_unlock(&g_coro_pool_lock)
#endif

static Coroutine* coro_pool_get(void) {
    coro_pool_lock();
    Coroutine* coro = NULL;
    if (g_coro_pool_count > 0) {
        coro = g_coro_pool[--g_coro_pool_count];
    }
    coro_pool_unlock();
    return coro;
}

static bool coro_pool_put(Coroutine* coro) {
    coro_pool_lock();
    bool success = false;
    if (g_coro_pool_count < CORO_POOL_SIZE) {
        g_coro_pool[g_coro_pool_count++] = coro;
        success = true;
    }
    coro_pool_unlock();
    return success;
}

// ============================================================================
// Lock-free Ring Buffer Queue
// ============================================================================

typedef struct {
    Coroutine** items;
    volatile long head;
    volatile long tail;
    long capacity;
#ifdef _WIN32
    SRWLOCK lock;
#elif defined(__APPLE__)
    // macOS: use os_unfair_lock (pthread_spinlock_t not supported)
    os_unfair_lock lock;
#else
    pthread_spinlock_t lock;
#endif
} CoroQueue;

static void queue_init(CoroQueue* q, long capacity) {
    q->items = (Coroutine**)calloc(capacity, sizeof(Coroutine*));
    q->head = 0;
    q->tail = 0;
    q->capacity = capacity;
#ifdef _WIN32
    InitializeSRWLock(&q->lock);
#elif defined(__APPLE__)
    q->lock = OS_UNFAIR_LOCK_INIT;
#else
    pthread_spin_init(&q->lock, PTHREAD_PROCESS_PRIVATE);
#endif
}

static void queue_destroy(CoroQueue* q) {
    free(q->items);
#if !defined(_WIN32) && !defined(__APPLE__)
    pthread_spin_destroy(&q->lock);
#endif
    // macOS os_unfair_lock doesn't need destruction
}

static inline void queue_lock(CoroQueue* q) {
#ifdef _WIN32
    AcquireSRWLockExclusive(&q->lock);
#elif defined(__APPLE__)
    os_unfair_lock_lock(&q->lock);
#else
    pthread_spin_lock(&q->lock);
#endif
}

static inline void queue_unlock(CoroQueue* q) {
#ifdef _WIN32
    ReleaseSRWLockExclusive(&q->lock);
#elif defined(__APPLE__)
    os_unfair_lock_unlock(&q->lock);
#else
    pthread_spin_unlock(&q->lock);
#endif
}

static bool queue_push(CoroQueue* q, Coroutine* coro) {
    queue_lock(q);
    long next = (q->tail + 1) % q->capacity;
    if (next == q->head) {
        queue_unlock(q);
        return false;
    }
    q->items[q->tail] = coro;
    q->tail = next;
    queue_unlock(q);
    return true;
}

static Coroutine* queue_pop(CoroQueue* q) {
    queue_lock(q);
    if (q->head == q->tail) {
        queue_unlock(q);
        return NULL;
    }
    Coroutine* coro = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    queue_unlock(q);
    return coro;
}

static inline long queue_size(CoroQueue* q) {
    return (q->tail - q->head + q->capacity) % q->capacity;
}

// ============================================================================
// Global Scheduler (M:N threading like Go)
// ============================================================================

typedef struct {
    CoroQueue global_queue;
    CoroQueue* local_queues;
    
    int num_workers;
    volatile bool running;
    volatile long active_count;
    volatile long total_spawned;
    
#ifdef _WIN32
    HANDLE* workers;
    HANDLE work_semaphore;
    CRITICAL_SECTION stats_lock;
#else
    pthread_t* workers;
    sem_t work_semaphore;
    pthread_mutex_t stats_lock;
#endif
} Scheduler;

static Scheduler g_sched = {0};
static volatile bool g_sched_init = false;

#ifdef _WIN32
static __declspec(thread) int tls_worker_id = -1;
static __declspec(thread) Coroutine* tls_current = NULL;
static __declspec(thread) void* tls_main_fiber = NULL;
#else
static __thread int tls_worker_id = -1;
static __thread Coroutine* tls_current = NULL;
static __thread ucontext_t tls_main_ctx;
#endif

// ============================================================================
// Coroutine Lifecycle
// ============================================================================

static void coro_entry(Coroutine* coro);

#ifdef _WIN32
static void CALLBACK fiber_entry(void* param) {
    Coroutine* coro = (Coroutine*)param;
    coro_entry(coro);
}
#else
static void context_entry(void) {
    if (tls_current) {
        coro_entry(tls_current);
    }
}
#endif

static Coroutine* coro_create(MoonFunc func, MoonValue** args, int argc,
                              MoonValue** captures, int capture_count) {
    // Try to get from pool first
    Coroutine* coro = coro_pool_get();
    if (!coro) {
        coro = (Coroutine*)malloc(sizeof(Coroutine));
        if (!coro) {
            fprintf(stderr, "ERROR: malloc failed for Coroutine! spawned=%ld\n", g_sched.total_spawned);
            return NULL;
        }
    }
    
#ifdef _WIN32
    coro->id = (int)InterlockedIncrement((volatile LONG*)&g_sched.total_spawned);
#else
    coro->id = (int)__sync_add_and_fetch(&g_sched.total_spawned, 1);
#endif
    
    coro->state = CORO_READY;
    coro->func = func;
    coro->argc = argc;
    coro->next = NULL;
    
    // Copy and retain args - use inline storage for small arg counts
    if (argc > 0) {
        if (argc <= 4) {
            // Use inline storage for common case (up to 4 args)
            coro->args = coro->inline_args;
        } else {
            coro->args = (MoonValue**)malloc(sizeof(MoonValue*) * argc);
            if (!coro->args) {
                fprintf(stderr, "ERROR: malloc failed for coro args! argc=%d\n", argc);
                free(coro);
                return NULL;
            }
        }
        for (int i = 0; i < argc; i++) {
            coro->args[i] = args[i];
            if (args[i]) moon_retain(args[i]);
        }
    } else {
        coro->args = NULL;
    }
    
    // Copy and retain closure captures
    coro->capture_count = capture_count;
    if (capture_count > 0 && captures) {
        coro->captures = (MoonValue**)malloc(sizeof(MoonValue*) * capture_count);
        for (int i = 0; i < capture_count; i++) {
            coro->captures[i] = captures[i];
            if (captures[i]) moon_retain(captures[i]);
        }
    } else {
        coro->captures = NULL;
    }
    
#ifdef _WIN32
    coro->fiber = CreateFiberEx(CORO_STACK_SIZE, CORO_STACK_SIZE, 
                                 FIBER_FLAG_FLOAT_SWITCH, fiber_entry, coro);
    if (!coro->fiber) {
        DWORD err = GetLastError();
        fprintf(stderr, "ERROR: CreateFiberEx failed! Error=%lu, spawned=%ld, active=%ld\n",
                err, g_sched.total_spawned, g_sched.active_count);
        if (coro->args) {
            for (int i = 0; i < argc; i++) {
                if (coro->args[i]) moon_release(coro->args[i]);
            }
            // Only free if not using inline storage
            if (coro->args != coro->inline_args) {
                free(coro->args);
            }
        }
        if (coro->captures) {
            for (int i = 0; i < capture_count; i++) {
                if (coro->captures[i]) moon_release(coro->captures[i]);
            }
            free(coro->captures);
        }
        free(coro);
        return NULL;
    }
    coro->main_fiber = NULL;
#else
    // macOS doesn't support MAP_STACK, only use it on Linux
#ifdef __APPLE__
    coro->stack = (char*)mmap(NULL, CORO_STACK_SIZE,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#else
    coro->stack = (char*)mmap(NULL, CORO_STACK_SIZE,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
#endif
    if (coro->stack == MAP_FAILED) {
        if (coro->args) {
            for (int i = 0; i < argc; i++) {
                if (coro->args[i]) moon_release(coro->args[i]);
            }
            // Only free if not using inline storage
            if (coro->args != coro->inline_args) {
                free(coro->args);
            }
        }
        if (coro->captures) {
            for (int i = 0; i < capture_count; i++) {
                if (coro->captures[i]) moon_release(coro->captures[i]);
            }
            free(coro->captures);
        }
        free(coro);
        return NULL;
    }
    
    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = CORO_STACK_SIZE;
    coro->ctx.uc_link = NULL;
    coro->main_ctx = NULL;
    makecontext(&coro->ctx, context_entry, 0);
#endif
    
    return coro;
}

static volatile long g_coro_destroyed = 0;

static void coro_destroy(Coroutine* coro) {
    if (!coro) return;
    
    // Validate the coro struct before using
    if (coro->id <= 0 || coro->argc < 0 || coro->argc > 100) {
        fprintf(stderr, "ERROR: invalid coro struct in coro_destroy! id=%d, argc=%d\n", 
                coro->id, coro->argc);
        return;
    }
    
#ifdef _WIN32
    InterlockedIncrement(&g_coro_destroyed);
    
    if (coro->fiber) {
        __try {
            DeleteFiber(coro->fiber);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            fprintf(stderr, "CRASH in DeleteFiber! coro=%d, code=0x%08lX\n", 
                    coro->id, GetExceptionCode());
        }
    }
#else
    __sync_add_and_fetch(&g_coro_destroyed, 1);
    if (coro->stack) munmap(coro->stack, CORO_STACK_SIZE);
#endif
    
    if (coro->args) {
        for (int i = 0; i < coro->argc; i++) {
            if (coro->args[i]) moon_release(coro->args[i]);
        }
        // Only free if not using inline storage
        if (coro->args != coro->inline_args) {
            free(coro->args);
        }
    }
    
    // Release closure captures
    if (coro->captures) {
        for (int i = 0; i < coro->capture_count; i++) {
            if (coro->captures[i]) moon_release(coro->captures[i]);
        }
        free(coro->captures);
    }
    
    // Clear sensitive fields before pooling
    coro->func = NULL;
    coro->args = NULL;
    coro->argc = 0;
    coro->captures = NULL;
    coro->capture_count = 0;
#ifdef _WIN32
    coro->fiber = NULL;
#else
    coro->stack = NULL;
    coro->main_ctx = NULL;
#endif
    
    // Clear inline_args to avoid dangling pointers
    memset(coro->inline_args, 0, sizeof(coro->inline_args));
    
    // Try to put back in pool, else free
    if (!coro_pool_put(coro)) {
        free(coro);
    }
}

static void coro_entry(Coroutine* coro) {
    coro->state = CORO_RUNNING;
    
#ifdef _WIN32
    __try {
#endif
        // Execute the coroutine function
        if (coro->func) {
            // Set closure captures before calling
            if (coro->captures && coro->capture_count > 0) {
                moon_set_closure_captures(coro->captures, coro->capture_count);
            }
            
            MoonValue* result = coro->func(coro->args, coro->argc);
            if (result) moon_release(result);
            
            // Clear captures after call
            if (coro->captures && coro->capture_count > 0) {
                moon_set_closure_captures(NULL, 0);
            }
        }
#ifdef _WIN32
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "CRASH in coroutine %d! Exception code: 0x%08lX\n", 
                coro->id, GetExceptionCode());
    }
#endif
    
    // CRITICAL: Must set state and decrement count before switching back
    // This ensures the coroutine will be properly destroyed
    coro->state = CORO_DONE;
    
#ifdef _WIN32
    InterlockedDecrement((volatile LONG*)&g_sched.active_count);
    SwitchToFiber(coro->main_fiber);
#else
    __sync_sub_and_fetch(&g_sched.active_count, 1);
    if (coro->main_ctx) {
        setcontext(coro->main_ctx);
    }
#endif
}

static inline void coro_resume(Coroutine* coro) {
    coro->state = CORO_RUNNING;  // CRITICAL: Must set state before resume!
#ifdef _WIN32
    coro->main_fiber = tls_main_fiber;
    
    // Validate fiber pointer
    if (!coro->fiber) {
        fprintf(stderr, "ERROR: coro->fiber is NULL! coro=%d\n", coro->id);
        coro->state = CORO_DONE;
        return;
    }
    
    __try {
        SwitchToFiber(coro->fiber);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "CRASH in SwitchToFiber! coro=%d, fiber=%p, code=0x%08lX\n", 
                coro->id, coro->fiber, GetExceptionCode());
        coro->state = CORO_DONE;
    }
#else
    coro->main_ctx = &tls_main_ctx;
    tls_current = coro;
    swapcontext(&tls_main_ctx, &coro->ctx);
#endif
}

// ============================================================================
// Work Stealing Scheduler
// ============================================================================

static Coroutine* steal_work(int worker_id) {
    // Try other workers' queues
    for (int i = 0; i < g_sched.num_workers; i++) {
        int target = (worker_id + i + 1) % g_sched.num_workers;
        Coroutine* coro = queue_pop(&g_sched.local_queues[target]);
        if (coro) return coro;
    }
    // Try global queue
    return queue_pop(&g_sched.global_queue);
}

#ifdef _WIN32
static unsigned __stdcall worker_func(void* param) {
    int id = (int)(intptr_t)param;
    tls_worker_id = id;
    
    // Convert to fiber
    tls_main_fiber = ConvertThreadToFiber(NULL);
    if (!tls_main_fiber) return 1;
    
    __try {
        while (g_sched.running) {
            Coroutine* coro = queue_pop(&g_sched.local_queues[id]);
            if (!coro) coro = steal_work(id);
            
            if (coro) {
                tls_current = coro;
                coro_resume(coro);
                
                if (coro->state == CORO_DONE) {
                    coro_destroy(coro);
                } else if (coro->state == CORO_READY) {
                    // Re-queue for later execution
                    if (!queue_push(&g_sched.local_queues[id], coro)) {
                        // Local queue full, try global
                        if (!queue_push(&g_sched.global_queue, coro)) {
                            // All queues full - must destroy to avoid leak
                            fprintf(stderr, "Warning: dropping coroutine %d (queues full)\n", coro->id);
                            InterlockedDecrement((volatile LONG*)&g_sched.active_count);
                            coro_destroy(coro);
                        }
                    }
                } else {
                    // Unexpected state (RUNNING or WAITING after resume returned)
                    // This indicates a bug - coroutine crashed or has invalid state
                    fprintf(stderr, "Warning: coroutine %d in unexpected state %d, destroying\n", 
                            coro->id, coro->state);
                    InterlockedDecrement((volatile LONG*)&g_sched.active_count);
                    coro_destroy(coro);
                }
                tls_current = NULL;
            } else {
                WaitForSingleObject(g_sched.work_semaphore, 1);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "WORKER %d CRASH! Exception: 0x%08lX, coro=%p\n", 
                id, GetExceptionCode(), (void*)tls_current);
    }
    
    ConvertFiberToThread();
    return 0;
}
#else
static void* worker_func(void* param) {
    int id = (int)(intptr_t)param;
    tls_worker_id = id;
    
    while (g_sched.running) {
        Coroutine* coro = queue_pop(&g_sched.local_queues[id]);
        if (!coro) coro = steal_work(id);
        
        if (coro) {
            tls_current = coro;
            coro_resume(coro);
            
            if (coro->state == CORO_DONE) {
                coro_destroy(coro);
            } else if (coro->state == CORO_READY) {
                // Re-queue for later execution
                if (!queue_push(&g_sched.local_queues[id], coro)) {
                    // Local queue full, try global
                    if (!queue_push(&g_sched.global_queue, coro)) {
                        // All queues full - must destroy to avoid leak
                        fprintf(stderr, "Warning: dropping coroutine %d (queues full)\n", coro->id);
                        __sync_sub_and_fetch(&g_sched.active_count, 1);
                        coro_destroy(coro);
                    }
                }
            } else {
                // Unexpected state (RUNNING or WAITING after resume returned)
                // This indicates a bug - coroutine crashed or has invalid state
                fprintf(stderr, "Warning: coroutine %d in unexpected state %d, destroying\n", 
                        coro->id, coro->state);
                __sync_sub_and_fetch(&g_sched.active_count, 1);
                coro_destroy(coro);
            }
            tls_current = NULL;
        } else {
            struct timespec ts = {0, 1000000}; // 1ms
            nanosleep(&ts, NULL);
        }
    }
    
    return NULL;
}
#endif

static void sched_init(void) {
    if (g_sched_init) return;
    
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_sched.num_workers = si.dwNumberOfProcessors;
#elif defined(__APPLE__)
    // macOS: use sysctlbyname to get CPU count
    int ncpu = 1;
    size_t len = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0);
    g_sched.num_workers = ncpu;
#else
    // Linux: use sysconf
    g_sched.num_workers = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    
    if (g_sched.num_workers < 1) g_sched.num_workers = 1;
    if (g_sched.num_workers > 256) g_sched.num_workers = 256;
    
    queue_init(&g_sched.global_queue, CORO_QUEUE_SIZE);
    
    g_sched.local_queues = (CoroQueue*)malloc(sizeof(CoroQueue) * g_sched.num_workers);
    for (int i = 0; i < g_sched.num_workers; i++) {
        queue_init(&g_sched.local_queues[i], CORO_LOCAL_BATCH * 4);
    }
    
    g_sched.running = true;
    g_sched.active_count = 0;
    g_sched.total_spawned = 0;
    
#ifdef _WIN32
    g_sched.work_semaphore = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    InitializeCriticalSection(&g_sched.stats_lock);
    g_sched.workers = (HANDLE*)malloc(sizeof(HANDLE) * g_sched.num_workers);
    
    for (int i = 0; i < g_sched.num_workers; i++) {
        unsigned tid;
        g_sched.workers[i] = (HANDLE)_beginthreadex(NULL, 0, worker_func, 
                                                     (void*)(intptr_t)i, 0, &tid);
    }
#else
    sem_init(&g_sched.work_semaphore, 0, 0);
    pthread_mutex_init(&g_sched.stats_lock, NULL);
    g_sched.workers = (pthread_t*)malloc(sizeof(pthread_t) * g_sched.num_workers);
    
    for (int i = 0; i < g_sched.num_workers; i++) {
        pthread_create(&g_sched.workers[i], NULL, worker_func, (void*)(intptr_t)i);
    }
#endif
    
    g_sched_init = true;
}

static void sched_signal(void) {
#ifdef _WIN32
    ReleaseSemaphore(g_sched.work_semaphore, 1, NULL);
#else
    sem_post(&g_sched.work_semaphore);
#endif
}

// Shutdown scheduler and free all resources
static void sched_shutdown(void) {
    if (!g_sched_init) return;
    
    // Signal all workers to stop
    g_sched.running = false;
    
#ifdef _WIN32
    // Wake up all workers
    for (int i = 0; i < g_sched.num_workers; i++) {
        ReleaseSemaphore(g_sched.work_semaphore, 1, NULL);
    }
    
    // Wait for all workers to finish
    WaitForMultipleObjects(g_sched.num_workers, g_sched.workers, TRUE, 5000);
    
    // Close thread handles
    for (int i = 0; i < g_sched.num_workers; i++) {
        if (g_sched.workers[i]) {
            CloseHandle(g_sched.workers[i]);
        }
    }
    
    // Close semaphore
    if (g_sched.work_semaphore) {
        CloseHandle(g_sched.work_semaphore);
    }
    
    // Destroy critical section
    DeleteCriticalSection(&g_sched.stats_lock);
#else
    // Wake up all workers
    for (int i = 0; i < g_sched.num_workers; i++) {
        sem_post(&g_sched.work_semaphore);
    }
    
    // Wait for all workers to finish
    for (int i = 0; i < g_sched.num_workers; i++) {
        pthread_join(g_sched.workers[i], NULL);
    }
    
    // Destroy semaphore and mutex
    sem_destroy(&g_sched.work_semaphore);
    pthread_mutex_destroy(&g_sched.stats_lock);
#endif
    
    // Drain and destroy queues - free any remaining coroutines
    Coroutine* coro;
    while ((coro = queue_pop(&g_sched.global_queue)) != NULL) {
        coro_destroy(coro);
    }
    queue_destroy(&g_sched.global_queue);
    
    for (int i = 0; i < g_sched.num_workers; i++) {
        while ((coro = queue_pop(&g_sched.local_queues[i])) != NULL) {
            coro_destroy(coro);
        }
        queue_destroy(&g_sched.local_queues[i]);
    }
    
    // Free workers array and local queues
    free(g_sched.workers);
    free(g_sched.local_queues);
    
    // Reset scheduler state
    g_sched.workers = NULL;
    g_sched.local_queues = NULL;
    g_sched.num_workers = 0;
    g_sched.active_count = 0;
    g_sched.total_spawned = 0;
    g_sched_init = false;
}

// ============================================================================
// Public API - "moon" keyword (now coroutine-based!)
// ============================================================================

// moon func(args) - spawn lightweight coroutine (like go func())
void moon_async(MoonValue* func, MoonValue** args, int argc) {
    if (!func) {
        fprintf(stderr, "Runtime Error: moon requires a function\n");
        return;
    }
    
    MoonFunc funcPtr = NULL;
    MoonValue** captures = NULL;
    int capture_count = 0;
    
    // Handle both regular functions and closures
    if (func->type == MOON_FUNC && func->data.funcVal) {
        funcPtr = func->data.funcVal;
    }
    else if (func->type == MOON_CLOSURE && func->data.closureVal) {
        MoonClosure* closure = func->data.closureVal;
        funcPtr = closure->func;
        captures = closure->captures;
        capture_count = closure->capture_count;
    }
    else {
        fprintf(stderr, "Runtime Error: moon requires a function\n");
        return;
    }
    
    sched_init();
    
    Coroutine* coro = coro_create(funcPtr, args, argc, captures, capture_count);
    if (!coro) {
        fprintf(stderr, "Runtime Error: failed to create coroutine (active=%ld)\n", g_sched.active_count);
        return;
    }
    
#ifdef _WIN32
    InterlockedIncrement((volatile LONG*)&g_sched.active_count);
#else
    __sync_add_and_fetch(&g_sched.active_count, 1);
#endif
    
    // Distribute to workers round-robin
    static volatile long next_worker = 0;
#ifdef _WIN32
    int target = (int)(InterlockedIncrement(&next_worker) % g_sched.num_workers);
#else
    int target = (int)(__sync_add_and_fetch(&next_worker, 1) % g_sched.num_workers);
#endif
    
    if (!queue_push(&g_sched.local_queues[target], coro)) {
        // Local queue full, try global queue
        if (!queue_push(&g_sched.global_queue, coro)) {
            // Both queues full - this is a serious problem
            fprintf(stderr, "Runtime Error: all coroutine queues full!\n");
            // Decrement active count since we failed to queue
#ifdef _WIN32
            InterlockedDecrement((volatile LONG*)&g_sched.active_count);
#else
            __sync_sub_and_fetch(&g_sched.active_count, 1);
#endif
            coro_destroy(coro);
            return;
        }
    }
    
    sched_signal();
}

// Legacy function name for compatibility (no closure support)
void moon_async_call(MoonFunc func, MoonValue** args, int argc) {
    if (!func) return;
    sched_init();
    
    Coroutine* coro = coro_create(func, args, argc, NULL, 0);
    if (!coro) {
        fprintf(stderr, "Runtime Error: failed to create coroutine\n");
        return;
    }
    
#ifdef _WIN32
    InterlockedIncrement((volatile LONG*)&g_sched.active_count);
#else
    __sync_add_and_fetch(&g_sched.active_count, 1);
#endif
    
    static volatile long next_worker = 0;
#ifdef _WIN32
    int target = (int)(InterlockedIncrement(&next_worker) % g_sched.num_workers);
#else
    int target = (int)(__sync_add_and_fetch(&next_worker, 1) % g_sched.num_workers);
#endif
    
    if (!queue_push(&g_sched.local_queues[target], coro)) {
        queue_push(&g_sched.global_queue, coro);
    }
    
    sched_signal();
}

// yield() - give up CPU to other coroutines
void moon_yield(void) {
    Coroutine* coro = tls_current;
    if (coro && coro->state == CORO_RUNNING) {
        coro->state = CORO_READY;
#ifdef _WIN32
        SwitchToFiber(coro->main_fiber);
#else
        if (coro->main_ctx) {
            swapcontext(&coro->ctx, coro->main_ctx);
        }
#endif
    }
}

// num_goroutines() - get active coroutine count
MoonValue* moon_num_goroutines(void) {
    return moon_int(g_sched.active_count);
}

// num_cpu() - get worker thread count
MoonValue* moon_num_cpu(void) {
    sched_init();
    return moon_int(g_sched.num_workers);
}

// wait_all() - wait for all coroutines to finish
void moon_wait_all(void) {
    while (g_sched.active_count > 0) {
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
}

// ============================================================================
// Atomic Operations (for thread-safe counters)
// ============================================================================

// Check if MoonValue is in small integer cache (DO NOT modify these!)
static bool is_small_int_cache(MoonValue* v) {
    extern MoonValue g_small_ints[];
    extern volatile bool g_small_ints_initialized;
    if (!g_small_ints_initialized) return false;
    // Check if pointer is within the small int cache array
    return (v >= &g_small_ints[0] && v < &g_small_ints[MOON_SMALL_INT_COUNT]);
}

// atomic_counter(initial) - Create a NEW MoonValue for atomic operations
// This creates a non-cached MoonValue that can be safely modified atomically
// Uses aligned allocation to ensure atomic operations work correctly
MoonValue* moon_atomic_counter(MoonValue* initial) {
    int64_t val = initial ? moon_to_int(initial) : 0;
    
    // Allocate with 16-byte alignment for atomic operations
#ifdef _WIN32
    MoonValue* v = (MoonValue*)_aligned_malloc(sizeof(MoonValue), 16);
#else
    MoonValue* v = (MoonValue*)aligned_alloc(16, sizeof(MoonValue));
#endif
    if (!v) {
        return moon_int(val);  // Fallback
    }
    
    v->type = MOON_INT;
    v->refcount = 1;
    v->data.intVal = val;
    
    // Memory barrier to ensure visibility
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    
    return v;
}

// atomic_add(ptr, delta) - atomically add to a MoonValue integer
// WARNING: ptr must be created with atomic_counter(), not a regular integer!
// Returns the NEW value after addition
MoonValue* moon_atomic_add(MoonValue* ptr, MoonValue* delta) {
    if (!ptr || ptr->type != MOON_INT) {
        return moon_int(0);
    }
    
    // Safety check: warn if trying to modify small int cache
    if (is_small_int_cache(ptr)) {
        fprintf(stderr, "WARNING: atomic_add on small integer cache! Use atomic_counter() instead.\n");
        return moon_int(ptr->data.intVal);
    }
    
    int64_t d = moon_to_int(delta);
    
    // Use volatile pointer to prevent compiler optimizations
    volatile int64_t* target = (volatile int64_t*)&ptr->data.intVal;
    
#ifdef _WIN32
    // Windows: InterlockedAdd64 is a full memory barrier
    int64_t newVal = InterlockedAdd64((volatile LONG64*)target, d);
#else
    // GCC/Clang: use __atomic for proper sequentially consistent operation
    int64_t newVal = __atomic_add_fetch(&ptr->data.intVal, d, __ATOMIC_SEQ_CST);
#endif
    
    return moon_int(newVal);
}

// atomic_get(ptr) - atomically read a MoonValue integer
MoonValue* moon_atomic_get(MoonValue* ptr) {
    if (!ptr || ptr->type != MOON_INT) {
        return moon_int(0);
    }
#ifdef _WIN32
    // Use InterlockedOr64 with 0 to atomically read (full barrier)
    int64_t val = InterlockedOr64((volatile LONG64*)&ptr->data.intVal, 0);
    return moon_int(val);
#else
    // Use __atomic_load for sequentially consistent read
    int64_t val = __atomic_load_n(&ptr->data.intVal, __ATOMIC_SEQ_CST);
    return moon_int(val);
#endif
}

// atomic_set(ptr, value) - atomically set a MoonValue integer
// Returns the OLD value
MoonValue* moon_atomic_set(MoonValue* ptr, MoonValue* value) {
    if (!ptr || ptr->type != MOON_INT) {
        return moon_int(0);
    }
    int64_t newVal = moon_to_int(value);
#ifdef _WIN32
    int64_t oldVal = InterlockedExchange64((volatile LONG64*)&ptr->data.intVal, newVal);
    return moon_int(oldVal);
#else
    // Use __atomic_exchange for sequentially consistent exchange
    int64_t oldVal = __atomic_exchange_n(&ptr->data.intVal, newVal, __ATOMIC_SEQ_CST);
    return moon_int(oldVal);
#endif
}

// atomic_cas(ptr, expected, desired) - compare-and-swap
// Returns true if swap succeeded
MoonValue* moon_atomic_cas(MoonValue* ptr, MoonValue* expected, MoonValue* desired) {
    if (!ptr || ptr->type != MOON_INT) {
        return moon_bool(false);
    }
    int64_t exp = moon_to_int(expected);
    int64_t des = moon_to_int(desired);
#ifdef _WIN32
    int64_t old = InterlockedCompareExchange64((volatile LONG64*)&ptr->data.intVal, des, exp);
    return moon_bool(old == exp);
#else
    return moon_bool(__sync_bool_compare_and_swap(&ptr->data.intVal, exp, des));
#endif
}

// ============================================================================
// Mutex Support (Coroutine-aware, like Go's sync.Mutex)
// ============================================================================

// Spinlock-based mutex that yields to other coroutines when contended
// This avoids blocking the worker thread like OS mutexes would

typedef struct MoonMutex {
    volatile long locked;      // 0 = unlocked, 1 = locked
    volatile long waiters;     // Number of waiting coroutines
} MoonMutex;

// mutex() - create a new mutex
MoonValue* moon_mutex(void) {
    MoonMutex* mtx = (MoonMutex*)malloc(sizeof(MoonMutex));
    if (!mtx) return moon_null();
    mtx->locked = 0;
    mtx->waiters = 0;
    return moon_int((int64_t)(uintptr_t)mtx);
}

// lock(mutex) - acquire mutex (coroutine-friendly with exponential backoff)
void moon_lock(MoonValue* mtxVal) {
    if (!mtxVal || mtxVal->type != MOON_INT) return;
    MoonMutex* mtx = (MoonMutex*)(uintptr_t)moon_to_int(mtxVal);
    if (!mtx) return;
    
    int attempts = 0;
    
    while (1) {
#ifdef _WIN32
        // Try to acquire: if locked==0, set to 1 and return old value (0)
        if (InterlockedCompareExchange(&mtx->locked, 1, 0) == 0) {
            return;  // Got the lock!
        }
#else
        if (__sync_bool_compare_and_swap(&mtx->locked, 0, 1)) {
            return;  // Got the lock!
        }
#endif
        
        attempts++;
        
        // Exponential backoff strategy
        if (attempts < 10) {
            // First 10 attempts: just spin
#ifdef _WIN32
            YieldProcessor();
#elif defined(__APPLE__)
    #if defined(__arm64__) || defined(__aarch64__)
            // Apple Silicon (ARM64): use yield instruction
            __asm__ volatile("yield" ::: "memory");
    #else
            // Intel Mac (x86_64): use pause instruction
            __asm__ volatile("pause" ::: "memory");
    #endif
#else
            __asm__ volatile("pause" ::: "memory");
#endif
        } else if (attempts < 20) {
            // Next 10 attempts: yield to other coroutines
            if (tls_current && tls_current->state == CORO_RUNNING) {
                moon_yield();
            } else {
#ifdef _WIN32
                Sleep(0);
#else
                sched_yield();
#endif
            }
        } else {
            // After 20 attempts: yield + brief sleep (backoff)
            if (tls_current && tls_current->state == CORO_RUNNING) {
                moon_yield();
            }
#ifdef _WIN32
            Sleep(1);  // 1ms backoff
#else
            usleep(1000);
#endif
            // Reset attempts to prevent infinite backoff growth
            if (attempts > 50) attempts = 20;
        }
    }
}

// unlock(mutex) - release mutex
void moon_unlock(MoonValue* mtxVal) {
    if (!mtxVal || mtxVal->type != MOON_INT) return;
    MoonMutex* mtx = (MoonMutex*)(uintptr_t)moon_to_int(mtxVal);
    if (!mtx) return;
    
#ifdef _WIN32
    InterlockedExchange(&mtx->locked, 0);
#else
    __sync_lock_release(&mtx->locked);
#endif
}

// trylock(mutex) - try to acquire mutex (non-blocking)
// Returns true if acquired, false if already locked
MoonValue* moon_trylock(MoonValue* mtxVal) {
    if (!mtxVal || mtxVal->type != MOON_INT) return moon_bool(false);
    MoonMutex* mtx = (MoonMutex*)(uintptr_t)moon_to_int(mtxVal);
    if (!mtx) return moon_bool(false);
    
#ifdef _WIN32
    return moon_bool(InterlockedCompareExchange(&mtx->locked, 1, 0) == 0);
#else
    return moon_bool(__sync_bool_compare_and_swap(&mtx->locked, 0, 1));
#endif
}

// mutex_free(mutex) - destroy mutex and free memory
void moon_mutex_free(MoonValue* mtxVal) {
    if (!mtxVal || mtxVal->type != MOON_INT) return;
    MoonMutex* mtx = (MoonMutex*)(uintptr_t)moon_to_int(mtxVal);
    if (!mtx) return;
    free(mtx);
}

// ============================================================================
// Timer Support (unchanged, uses threads for timing accuracy)
// ============================================================================

typedef struct TimerEntry {
    int id;
    MoonFunc callback;
    int interval_ms;
    bool repeat;
    volatile bool active;
    volatile bool finished;     // Mark when thread has completed
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    struct TimerEntry* next;
} TimerEntry;

static TimerEntry* g_timers = NULL;
static volatile int g_next_timer_id = 1;

#ifdef _WIN32
static CRITICAL_SECTION g_timer_lock;
static bool g_timer_lock_init = false;
#else
static pthread_mutex_t g_timer_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void init_timer_lock(void) {
#ifdef _WIN32
    if (!g_timer_lock_init) {
        InitializeCriticalSection(&g_timer_lock);
        g_timer_lock_init = true;
    }
#endif
}

static void lock_timers(void) {
    init_timer_lock();
#ifdef _WIN32
    EnterCriticalSection(&g_timer_lock);
#else
    pthread_mutex_lock(&g_timer_lock);
#endif
}

static void unlock_timers(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_timer_lock);
#else
    pthread_mutex_unlock(&g_timer_lock);
#endif
}

// Forward declaration for cleanup
static void cleanup_finished_timers(void);

#ifdef _WIN32
static unsigned __stdcall timer_thread_func(void* data) {
    TimerEntry* timer = (TimerEntry*)data;
    
    do {
        Sleep(timer->interval_ms);
        if (!timer->active) break;
        
        // Execute callback as coroutine for consistency
        if (timer->callback) {
            MoonValue* funcVal = moon_func(timer->callback);
            moon_async(funcVal, NULL, 0);
            moon_release(funcVal);
        }
        
    } while (timer->repeat && timer->active);
    
    // Mark timer as finished for cleanup
    timer->finished = true;
    return 0;
}
#else
static void* timer_thread_func(void* data) {
    TimerEntry* timer = (TimerEntry*)data;
    
    do {
        usleep(timer->interval_ms * 1000);
        if (!timer->active) break;
        
        if (timer->callback) {
            MoonValue* funcVal = moon_func(timer->callback);
            moon_async(funcVal, NULL, 0);
            moon_release(funcVal);
        }
        
    } while (timer->repeat && timer->active);
    
    // Mark timer as finished for cleanup
    timer->finished = true;
    return NULL;
}
#endif

// Clean up finished timers to prevent memory leak
static void cleanup_finished_timers(void) {
    lock_timers();
    
    TimerEntry** pp = &g_timers;
    while (*pp) {
        TimerEntry* t = *pp;
        if (t->finished) {
            // Remove from list
            *pp = t->next;
            
#ifdef _WIN32
            // Close thread handle
            if (t->thread) {
                WaitForSingleObject(t->thread, 0);  // Non-blocking check
                CloseHandle(t->thread);
            }
#endif
            // Free the timer entry
            free(t);
        } else {
            pp = &(*pp)->next;
        }
    }
    
    unlock_timers();
}

static int create_timer(MoonFunc callback, int ms, bool repeat) {
    if (!callback || ms <= 0) return 0;
    
    // Clean up any finished timers first to prevent memory buildup
    cleanup_finished_timers();
    
    TimerEntry* timer = (TimerEntry*)malloc(sizeof(TimerEntry));
    if (!timer) return 0;
    
#ifdef _WIN32
    timer->id = InterlockedIncrement((volatile LONG*)&g_next_timer_id);
#else
    timer->id = __sync_add_and_fetch(&g_next_timer_id, 1);
#endif
    
    timer->callback = callback;
    timer->interval_ms = ms;
    timer->repeat = repeat;
    timer->active = true;
    timer->finished = false;
    
    lock_timers();
    timer->next = g_timers;
    g_timers = timer;
    unlock_timers();
    
#ifdef _WIN32
    unsigned tid;
    timer->thread = (HANDLE)_beginthreadex(NULL, 0, timer_thread_func, timer, 0, &tid);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&timer->thread, &attr, timer_thread_func, timer);
    pthread_attr_destroy(&attr);
#endif
    
    return timer->id;
}

MoonValue* moon_set_timeout(MoonValue* callback, MoonValue* ms) {
    if (!callback || callback->type != MOON_FUNC) return moon_int(0);
    int delay = (int)moon_to_int(ms);
    return moon_int(create_timer(callback->data.funcVal, delay, false));
}

MoonValue* moon_set_interval(MoonValue* callback, MoonValue* ms) {
    if (!callback || callback->type != MOON_FUNC) return moon_int(0);
    int interval = (int)moon_to_int(ms);
    return moon_int(create_timer(callback->data.funcVal, interval, true));
}

void moon_clear_timer(MoonValue* idVal) {
    int id = (int)moon_to_int(idVal);
    
    lock_timers();
    for (TimerEntry* t = g_timers; t; t = t->next) {
        if (t->id == id) {
            t->active = false;
            break;
        }
    }
    unlock_timers();
}

// Clean up ALL timers (stop all and free memory)
static void cleanup_all_timers(void) {
    lock_timers();
    
    // First, stop all active timers
    for (TimerEntry* t = g_timers; t; t = t->next) {
        t->active = false;
    }
    
    unlock_timers();
    
    // Wait a bit for timer threads to notice and finish
#ifdef _WIN32
    Sleep(10);
#else
    usleep(10000);
#endif
    
    // Now clean up all timer entries
    lock_timers();
    
    while (g_timers) {
        TimerEntry* t = g_timers;
        g_timers = t->next;
        
#ifdef _WIN32
        if (t->thread) {
            WaitForSingleObject(t->thread, 100);  // Brief wait
            CloseHandle(t->thread);
        }
#endif
        free(t);
    }
    
    unlock_timers();
    
#ifdef _WIN32
    // Clean up timer lock
    if (g_timer_lock_init) {
        DeleteCriticalSection(&g_timer_lock);
        g_timer_lock_init = false;
    }
#endif
}

// ============================================================================
// Public API - Runtime Cleanup
// ============================================================================

// Clean up all runtime resources (call before program exit)
void moon_runtime_cleanup(void) {
    // Wait for all active coroutines to finish
    moon_wait_all();
    
    // Clean up all timers
    cleanup_all_timers();
    
    // Shutdown scheduler
    sched_shutdown();
}
