/* What newlib and libnx lack (switch_compat.h). */

#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>

#include "include/switch_compat.h"

/* Horizon has no time zones: the C library's local time is UTC, so mktime is timegm. */
time_t timegm(struct tm* tm) {
    return mktime(tm);
}

/* newlib declares it but doesn't provide it. Weak: mesa-switch's NVK brings its own. */
__attribute__((weak)) int posix_memalign(void** memptr, size_t alignment, size_t size) {
    void* memory = memalign(alignment, size);
    if (memory == NULL) {
        return ENOMEM;
    }
    *memptr = memory;
    return 0;
}

/* pthread_detach calls this when it exists and fails with ENOSYS otherwise, which makes
   std::thread::detach() throw. The thread is simply never joined (the N64 port does the same). */
struct __pthread_t;
int __syscall_thread_detach(struct __pthread_t* thread) {
    (void)thread;
    return 0;
}

/* Priorities and affinity: only for the calling thread for now (libnx exposes the handle of the
   current thread only; others keep libnx's defaults). The SDK's priorities are SCHED_FIFO 1..99;
   Horizon's are 0 (highest) .. 63, applications use 0x1C-0x3F. ponytail: a thread registry keyed
   by pthread_t if the game's priorities matter on hardware. */
static int to_horizon_priority(int sched_priority) {
    int p = 0x2C - sched_priority / 4;
    if (p < 0x1C) p = 0x1C;
    if (p > 0x3B) p = 0x3B;
    return p;
}

int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const cpu_set_t* cpuset) {
    (void)cpusetsize;
    if (thread != pthread_self() || cpuset->bits == 0) {
        return 0;
    }
    /* Never core 3: the system's. */
    u64 mask = cpuset->bits & 0x7;
    if (mask == 0) {
        return 0;
    }
    int preferred = __builtin_ctzl(mask);
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, preferred, mask);
    return R_FAILED(rc) ? EINVAL : 0;
}

int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize, cpu_set_t* cpuset) {
    (void)thread;
    (void)cpusetsize;
    cpuset->bits = 0x7;
    return 0;
}

int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param) {
    (void)thread;
    if (policy) *policy = SCHED_FIFO;
    if (param) param->sched_priority = 0;
    return 0;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param) {
    (void)policy;
    if (thread != pthread_self() || !param) {
        return 0;
    }
    Result rc = svcSetThreadPriority(CUR_THREAD_HANDLE, to_horizon_priority(param->sched_priority));
    return R_FAILED(rc) ? EINVAL : 0;
}

int pthread_sigqueue(pthread_t thread, int sig, const union sigval value) {
    (void)thread;
    (void)sig;
    (void)value;
    return ENOSYS;
}

int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy) {
    (void)attr;
    (void)policy;
    return 0;
}

/* libc calls the SDK's POSIX sources make and libnx lacks. */
#include <fcntl.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

void rex_nx_thread_name(void* pthread, const char* name);  /* switch_nx.cpp: the CPU profile line */

int pthread_setname_np(pthread_t thread, const char* name) {
    rex_nx_thread_name(thread, name);
    return 0;
}

int pthread_kill(pthread_t thread, int sig) {
    (void)thread;
    (void)sig;
    return ENOSYS;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    (void)pid;
    (void)status;
    (void)options;
    errno = ECHILD;
    return -1;
}

int execvp(const char* file, char* const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    off_t here = lseek(fd, 0, SEEK_CUR);
    if (here < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, count);
    lseek(fd, here, SEEK_SET);
    return n;
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    off_t here = lseek(fd, 0, SEEK_CUR);
    if (here < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = write(fd, buf, count);
    lseek(fd, here, SEEK_SET);
    return n;
}

uid_t getuid(void) {
    return 0;
}

int getpwuid_r(uid_t uid, struct passwd* pwd, char* buf, size_t buflen, struct passwd** result) {
    (void)uid;
    (void)pwd;
    (void)buf;
    (void)buflen;
    *result = NULL;
    return ENOENT;
}

int creat(const char* path, mode_t mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

/* Recursive mutexes without the kernel hand-off (console 0.144 profile: guest worker threads spent 32-39%
   of their samples in rmutexLock <- pthread_mutex_lock <- the game's mtmsrd critical sections (the global
   critical region) and in malloc's lock). Horizon's svcArbitrateLock gives a contended mutex to the waiter
   inside the kernel before that waiter runs; on a core shared by time-sliced priority-0x3B threads the lock
   then sits owned by a thread that waits for its slice (up to 10 ms): a convoy. Here a waiter spins 20 us
   (the 0.152 version spun 100 yields = 0.3 us, so nearly every contention slept; console 0.177: 4,000
   sleeps/s cost the workers a third of their samples, ~350 us per "20 us" sleep, since a woken thread queues
   behind the running time-sliced thread of its core), then yields with core migration for 500 us (the
   owner runs if it is queued on this core; this thread may move to an idle core), then sleeps 20 us in a
   loop. Used for every RMutex (std::recursive_mutex, newlib's malloc lock). The file
   sdmc:/switch/cvhd/nospin (checked once at boot) restores libnx's. */
void __real_rmutexLock(RMutex* m);
bool __real_rmutexTryLock(RMutex* m);
void __real_rmutexUnlock(RMutex* m);

volatile unsigned long long rex_nx_rm_contended, rex_nx_rm_sleeps, rex_nx_rm_yields;
static int rm_spin = 1;

void rex_nx_rm_spin_enable(int on) {
    rm_spin = on;
}

void __wrap_rmutexLock(RMutex* m) {
    if (!rm_spin) {
        __real_rmutexLock(m);
        return;
    }
    const u32 self = threadGetCurHandle();
    if ((__atomic_load_n(&m->lock, __ATOMIC_RELAXED) & ~0x40000000u) == self) {
        m->counter++;
        return;
    }
    u32 expected = 0;
    if (!__atomic_compare_exchange_n(&m->lock, &expected, self, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&rex_nx_rm_contended, 1, __ATOMIC_RELAXED);
        const u64 t0 = armGetSystemTick();  /* 19.2 MHz */
        for (;;) {
            if (__atomic_load_n(&m->lock, __ATOMIC_RELAXED) == 0) {
                expected = 0;
                if (__atomic_compare_exchange_n(&m->lock, &expected, self, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) break;
            }
            const u64 waited = armGetSystemTick() - t0;
            if (waited < 384) {           /* 20 us */
                __asm__ volatile("yield");
            } else if (waited < 9600) {   /* 500 us */
                __atomic_fetch_add(&rex_nx_rm_yields, 1, __ATOMIC_RELAXED);
                svcSleepThread(-1);       /* YieldWithCoreMigration */
            } else {
                __atomic_fetch_add(&rex_nx_rm_sleeps, 1, __ATOMIC_RELAXED);
                svcSleepThread(20000);
            }
        }
    }
    m->counter = 1;
}

bool __wrap_rmutexTryLock(RMutex* m) {
    if (!rm_spin) return __real_rmutexTryLock(m);
    const u32 self = threadGetCurHandle();
    if ((__atomic_load_n(&m->lock, __ATOMIC_RELAXED) & ~0x40000000u) == self) {
        m->counter++;
        return true;
    }
    u32 expected = 0;
    if (!__atomic_compare_exchange_n(&m->lock, &expected, self, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return false;
    m->counter = 1;
    return true;
}

void __wrap_rmutexUnlock(RMutex* m) {
    if (!rm_spin) {
        __real_rmutexUnlock(m);
        return;
    }
    if (--m->counter == 0) {
        const u32 old = __atomic_exchange_n(&m->lock, 0, __ATOMIC_RELEASE);
        if (old & 0x40000000u) svcArbitrateUnlock(&m->lock);  /* libnx's wait flag: only if a kernel waiter exists */
    }
}
