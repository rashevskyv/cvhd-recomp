// Nintendo Switch (libnx) start-up for the cvhd port: the log on the SD card, the clocks, the exit
// path and the crash report. Everything lives in sdmc:/switch/cvhd/. Adapted from the N64 port's
// host/switch/switch_nx.cpp (rashevskyv/CBFD-Recompiled, branch switch).
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include "cvhd_version.h"  // written by switch/build.sh: CVHD_VERSION, CVHD_BUILD_UTC

extern "C" {
// libnx's own exit (init.c): __appExit (userAppExit, then the services), then the function from
// envGetExitFuncPtr. No static destructors.
[[noreturn]] void __libnx_exit(int rc);
// The NRO's load address: _start, the first instruction (ELF address 0). Not __start__, which is an
// absolute 0 that is not relocated (the Xbox port). Crash addresses are given relative to it.
void _start(void);
// __libnx_exception_handler runs on this stack when a thread faults.
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
// Report crashes even when a debugger is attached.
u32 __nx_exception_ignoredebug = 1;
// nvdrv's transfer memory: libnx gives 8 MB; the N64 port needed 32 MB on hardware.
u32 __nx_nv_transfermem_size = 0x2000000;
// membench_nx.cpp
void rex_nx_membench(void);
// memory_nx.cpp: the RAM behind the guest memory, taken first; the physical (0xA0000000) block.
void rex_nx_reserve_pool(void);
const uint8_t* rex_nx_physical_base(void);
// libnx's heap bounds (newlib.c).
extern char* fake_heap_start;
extern char* fake_heap_end;
// Counted by the SDK's command processor (sdk-edits.py, WAIT_REG_MEM), printed every 2 s here.
volatile uint64_t rex_nx_wait_polls = 0, rex_nx_wait_sleep_ms = 0, rex_nx_wait_yields = 0, rex_nx_wait_slept_ticks = 0;
// WAIT_REG_MEM polls spin this long (ticks at 19.2 MHz) before the 1 ms sleeps: file sdmc:/switch/cvhd/cpspin_us, default 300.
volatile uint64_t rex_nx_cp_spin_ticks = 0;
// The sleep of an unmet WAIT_REG_MEM poll after the spin (microseconds): file sdmc:/switch/cvhd/cpsleep_us, default 100
// (the SDK capped it at 1 ms). The guest's main thread waits for the command processor to pass these packets (console
// 0.187: 8 sleeping polls per frame), so the sleep is latency on the frame's critical path. Eden 0.231: 1000 -> 100 us
// took the stage from 35.5 to 37.9 fps and the frames under 17 ms from 29% to 39%.
volatile uint64_t rex_nx_cp_sleep_us = 100;
void rex_nx_wait_report(void);  // threading_switch.cpp (sdk-edits): per-object wait profile
void rex_nx_rm_spin_enable(int on);  // switch_compat.c: recursive mutexes without the kernel hand-off
extern volatile unsigned long long rex_nx_rm_contended, rex_nx_rm_sleeps, rex_nx_rm_yields;
// Counted by the SDK's multi-object wait (threading_switch.cpp): calls, and waits that ended by the 20 ms cap.
volatile unsigned long long rex_nx_multi_waits = 0, rex_nx_multi_timeouts = 0;
// Spin before a plain wait sleeps (sdk-edits, PosixConditionBase::Wait): nanoseconds, file sdmc:/switch/cvhd/waitspin_us.
volatile unsigned long long rex_nx_wait_spin_ns = 30000, rex_nx_wait_spin_hits = 0, rex_nx_wait_spin_misses = 0;
// The wait profile (threading_switch.cpp: two global mutexes on every wait and signal) and the PC sampler (it
// pauses the busy threads 100 times a second): only with the file sdmc:/switch/cvhd/prof.
volatile int rex_nx_waitprof = 0;
// memory_nx.cpp: dirty-page flushes asked by the command processor.
extern volatile uint64_t rex_nx_flush_calls, rex_nx_flush_pages;
}

namespace {

const char* const kDataDir = "sdmc:/switch/cvhd";

Thread log_flush_thread;
volatile bool log_flush_running = false;

// CPU 1785 MHz (the highest), GPU 768 MHz (docked's rate), memory 1600 MHz, as the Xbox ports do.
// The old rates come back on exit.
struct Clock {
    const char* name;
    PcvModuleId module;
    u32 wanted;
    ClkrstSession session;
    bool open;
    u32 old;
    volatile u32 set;
};
Clock clocks[] = {
    {"CPU", PcvModuleId_CpuBus, 1785000000},
    {"GPU", PcvModuleId_GPU, 768000000},
    {"memory", PcvModuleId_EMC, 1600000000},
};
bool clkrst_up = false;

// Is another process serving this name? (sys-clk's own check.)
bool service_running(const char* name) {
    Handle handle;
    const Result rc = smRegisterService(&handle, smEncodeName(name), false, 1);
    if (R_SUCCEEDED(rc)) {
        smUnregisterService(smEncodeName(name));
        svcCloseHandle(handle);
        return false;
    }
    return rc == MAKERESULT(21, 4);
}

void raise_clocks() {
    // sys-clk and Horizon OC set the clocks themselves; fighting them froze consoles (NFSU2's port).
    if (service_running("sys:clk") || service_running("hoc:clk")) {
        std::printf("[clock] sys-clk or Horizon OC is running: the clocks are left to it\n");
        return;
    }
    if (!hosversionAtLeast(8, 0, 0) || R_FAILED(clkrstInitialize())) {
        std::printf("[clock] no clkrst: the clocks are unchanged\n");
        return;
    }
    clkrst_up = true;
    for (Clock& clock : clocks) {
        if (R_FAILED(clkrstOpenSession(&clock.session, clock.module, 3))) {
            std::printf("[clock] no clkrst session for the %s: unchanged\n", clock.name);
            continue;
        }
        clock.open = true;
        clkrstGetClockRate(&clock.session, &clock.old);
        // Never lower a rate (docked memory, or someone else's overclock).
        const Result rc = clock.old < clock.wanted ? clkrstSetClockRate(&clock.session, clock.wanted) : 0;
        u32 now = 0;
        clkrstGetClockRate(&clock.session, &now);
        clock.set = now;
        std::printf("[clock] %s %u -> %u MHz%s\n", clock.name, clock.old / 1000000, now / 1000000,
                    R_FAILED(rc) ? " (refused)" : "");
    }
}

// HOME, docking and sleep put the system's clocks back: set ours again.
void keep_clocks() {
    for (Clock& clock : clocks) {
        u32 now = 0;
        if (clock.set == 0 || R_FAILED(clkrstGetClockRate(&clock.session, &now)) || now >= clock.set) {
            continue;
        }
        clkrstSetClockRate(&clock.session, clock.set);
        std::printf("[clock] %s was put back to %u MHz: set to %u MHz again\n", clock.name, now / 1000000,
                    clock.set / 1000000);
    }
}

void restore_clocks() {
    if (!clkrst_up) {
        return;
    }
    for (Clock& clock : clocks) {
        if (clock.open) {
            if (clock.old != 0 && clock.set != clock.old) {
                clkrstSetClockRate(&clock.session, clock.old);
            }
            clkrstCloseSession(&clock.session);
        }
    }
    clkrstExit();
    clkrst_up = false;
}

// libnx's exit ends here instead of returning to hbl, with the services closed. Returning to hbl
// unmapped the NRO while threads still ran (the probe's exit crash, 2026-10-21).
[[noreturn]] void end_process(int) {
    svcExitProcess();
}

// Per-thread CPU time (svcGetInfo ThreadTickCount) for the 2-s profile line: which thread burns the
// time, the guest's, the GPU command processor's or another. Threads register in the create wrap
// (libnx's pthread_t is its Thread*), names come from the SDK's pthread_setname_np.
struct NxThread {
    Thread* thread;  // null = free
    Handle handle;
    char name[40];
    u64 last_ticks;
    u64 last_waited;  // rex_nx_waited_ticks at the last report
};
NxThread nx_threads[64];
Mutex nx_threads_lock;
// Ticks each thread spent blocked in the SDK's wait primitives (threading_switch.cpp's WaitScope adds them
// through rex_nx_waited_add): busy = wall - waited, the CPU work measure that Eden can give (its
// ThreadTickCount is 0). Sleeps (svcSleepThread) and the guest's own spins count as busy.
extern "C" volatile u64 rex_nx_waited_ticks[64];
volatile u64 rex_nx_waited_ticks[64];
extern "C" volatile unsigned rex_nx_swaps;  // deko3d_backend.cpp: frames presented
volatile unsigned rex_nx_swaps = 0;
extern "C" volatile double rex_nx_main_busy_ms;  // the main thread's busy ms per frame of the last period (the HUD)
volatile double rex_nx_main_busy_ms = 0;

void register_thread(Thread* thread, const char* name) {
    mutexLock(&nx_threads_lock);
    for (NxThread& t : nx_threads) {
        if (t.thread == nullptr) {
            t.thread = thread;
            t.handle = thread->handle;
            std::snprintf(t.name, sizeof(t.name), "%s", name);
            t.last_ticks = 0;
            t.last_waited = 0;
            rex_nx_waited_ticks[&t - nx_threads] = 0;
            break;
        }
    }
    mutexUnlock(&nx_threads_lock);
}

// The calling thread's slot in nx_threads, cached per thread (-1 = not a registered thread).
extern "C" void rex_nx_waited_add(u64 ticks) {
    static thread_local int slot = -2;
    if (slot == -2) {
        slot = -1;
        const Thread* self = threadGetSelf();
        mutexLock(&nx_threads_lock);
        for (const NxThread& t : nx_threads) {
            if (t.thread == self) {
                slot = int(&t - nx_threads);
                break;
            }
        }
        mutexUnlock(&nx_threads_lock);
    }
    if (slot >= 0) rex_nx_waited_ticks[slot] += ticks;
}

// One line per 2 s: each thread's busy time (wall minus the waits) per presented frame, and the share of
// the period. Comparable between Eden and the console as "work per frame", unlike the fps.
void print_busy_profile(u64 period_ticks, unsigned frames) {
    char line[1024];
    int n = std::snprintf(line, sizeof(line), "[nx] busy/frame (%u frames):", frames);
    double total_ms = 0;
    mutexLock(&nx_threads_lock);
    for (NxThread& t : nx_threads) {
        if (t.thread == nullptr) continue;
        // Guest threads and the command thread only: the host threads block outside the SDK's primitives
        // (the UI loop, the vsync worker's sleeps, the timer queue) and would show 100%.
        if (std::strstr(t.name, "XThread") == nullptr && std::strncmp(t.name, "GPU Commands", 12) != 0) continue;
        const u64 waited = rex_nx_waited_ticks[&t - nx_threads];
        const u64 dw = waited >= t.last_waited ? waited - t.last_waited : 0;
        t.last_waited = waited;
        const u64 busy = dw < period_ticks ? period_ticks - dw : 0;
        const double busy_ms = double(armTicksToNs(busy)) / 1e6;
        const double per_frame = frames ? busy_ms / frames : 0;
        if (std::strncmp(t.name, "Main XThread", 12) == 0) rex_nx_main_busy_ms = per_frame;
        const unsigned pct = (unsigned)(busy * 100 / period_ticks);
        if (dw == 0 && pct >= 99) {
            // Blocked outside the SDK's primitives for the whole period (its samples sit in the condvar svc): not busy.
            if (n < (int)sizeof(line) - 48) n += std::snprintf(line + n, sizeof(line) - n, " %s (no waits seen) |", t.name);
            continue;
        }
        if (pct >= 2) {
            total_ms += per_frame;
            if (n < (int)sizeof(line) - 48) n += std::snprintf(line + n, sizeof(line) - n, " %s %.1f ms (%u%%) |", t.name, per_frame, pct);
        }
    }
    mutexUnlock(&nx_threads_lock);
    n += std::snprintf(line + n, sizeof(line) - n, " sum %.1f ms", total_ms);
    std::printf("%s\n", line);
}

// One line per 2 s: percent of one core per thread over the period (threads under 1% left out).
void print_cpu_profile(u64 period_ticks) {
    char line[1024];
    int n = std::snprintf(line, sizeof(line), "[nx] cpu:");
    unsigned threads = 0, unreadable = 0;
    Result last_rc = 0;
    mutexLock(&nx_threads_lock);
    for (NxThread& t : nx_threads) {
        if (t.thread == nullptr) continue;
        threads++;
        u64 ticks = 0;
        const Result rc = svcGetInfo(&ticks, InfoType_ThreadTickCount, t.handle, UINT64_MAX);
        if (R_FAILED(rc)) {
            // An exited thread (closed handle) or an emulator without this InfoType (Eden).
            unreadable++;
            last_rc = rc;
            continue;
        }
        const u64 delta = t.last_ticks ? ticks - t.last_ticks : 0;
        t.last_ticks = ticks;
        const unsigned pct = (unsigned)(delta * 100 / period_ticks);
        if (pct >= 1 && n < (int)sizeof(line) - 48) {
            // Horizon time-slices only priority 0x3B (sa2-nx's notes): the priority matters.
            s32 prio = 0;
            svcGetThreadPriority(&prio, t.handle);
            n += std::snprintf(line + n, sizeof(line) - n, " %s %u%% p%02X |", t.name, pct, prio);
        }
    }
    mutexUnlock(&nx_threads_lock);
    if (unreadable) {
        n += std::snprintf(line + n, sizeof(line) - n, " %u of %u threads unreadable (rc 0x%X)", unreadable,
                           threads, last_rc);
    }
    std::printf("%s\n", line);
}


// PC sampler: 100 times a second the busiest threads are paused for a few microseconds and their
// program counter read (svcSetThreadActivity + svcGetThreadContext3). Counts per 256-byte bucket
// of the NRO; the 2-s line prints the top buckets, addr2line on cvhd.elf names the functions.
struct SampledThread {
    const char* prefix;  // thread name prefix to sample
    int ordinal;         // the n-th thread with that prefix (the game's XThreads)
    char name[48];       // the matched thread's name, for the line
    struct Bucket {
        u64 key;  // (pc - _start) >> 8, 0 = free
        u32 count;
        u64 last_pc;  // the latest exact PC seen in the bucket (svc stubs are 16 bytes apart)
    } buckets[512];
    u32 samples, outside, failed;
    Result last_rc;
    // Samples parked in svcArbitrateLock: which mutex, who owns it, the caller chain (ELF offsets).
    struct LockWait {
        u64 mutex;
        u32 owner;  // owner thread handle
        u64 chain[4];
        u32 count;
    } locks[12];
};
// The main thread, the command processor, the audio worker and the game's first six other threads
// (named "XThread..." by the SDK): a loader thread that goes silent shows where it sits.
SampledThread sampled[9] = {{"Main XThread", 0}, {"GPU Commands", 0}, {"Audio Worker", 0}, {"XThread", 0},
                            {"XThread", 1},      {"XThread", 2},      {"XThread", 3},      {"XThread", 4},
                            {"XThread", 5}};
Thread sampler_thread;
volatile bool sampler_running = false;

// Cumulative histogram per thread over the whole run, 4 KB buckets (one recompiled function or a few): the 2-s
// line keeps only its top 6, and guest code is spread over thousands of 256-byte buckets, so no profile of the
// workers' guest code could be read from it. Every 30 s: [nx] pctop <thread> (N samples): +0xOFF P% ... (top 24).
struct CumBucket {
    u64 key;  // (thread index << 48) | ((pc - base) >> 12) + 1, 0 = free
    u32 count;
};
CumBucket cum_buckets[16384];
u32 cum_samples[9];

void cum_add(u32 thread_index, u64 off) {
    const u64 key = (u64(thread_index) << 48) | ((off >> 12) + 1);
    cum_samples[thread_index]++;
    for (u32 i = (u32)(key * 2654435761u) % 16384, n = 0; n < 16384; i = (i + 1) % 16384, n++) {
        if (cum_buckets[i].key == key) {
            cum_buckets[i].count++;
            return;
        }
        if (cum_buckets[i].key == 0) {
            cum_buckets[i].key = key;
            cum_buckets[i].count = 1;
            return;
        }
    }
}

void print_pc_cumulative() {
    for (u32 t = 0; t < 9; t++) {
        if (cum_samples[t] == 0) continue;
        const SampledThread& st = sampled[t];
        char line[1024];
        int n = std::snprintf(line, sizeof(line), "[nx] pctop %s (%u samples):", st.name[0] ? st.name : st.prefix, cum_samples[t]);
        // top 24 by count (a 16384-entry scan per pick: 24 scans every 30 s)
        u32 floor = 0xFFFFFFFFu;
        for (int top = 0; top < 24; top++) {
            const CumBucket* best = nullptr;
            for (const CumBucket& b : cum_buckets) {
                if (b.key == 0 || (b.key >> 48) != t || b.count >= floor) continue;
                if (best == nullptr || b.count > best->count) best = &b;
            }
            if (best == nullptr || best->count * 1000 < cum_samples[t]) break;  // under 0.1%: stop
            floor = best->count;
            if (n > (int)sizeof(line) - 32) break;
            n += std::snprintf(line + n, sizeof(line) - n, " +0x%llX %.1f%% |", (unsigned long long)(((best->key & 0xFFFFFFFFFFFFull) - 1) << 12),
                               best->count * 100.0 / cum_samples[t]);
        }
        std::printf("%s\n", line);
    }
}

void sample_pc(SampledThread& st, u64 pc) {
    const u64 base = reinterpret_cast<u64>(&_start);
    st.samples++;
    if (pc < base || pc - base >= 0x10000000ull) {
        st.outside++;
        return;
    }
    cum_add(u32(&st - sampled), pc - base);
    const u64 key = ((pc - base) >> 8) + 1;
    for (u32 i = (u32)(key * 2654435761u) % 512, n = 0; n < 512; i = (i + 1) % 512, n++) {
        if (st.buckets[i].key == key) {
            st.buckets[i].count++;
            st.buckets[i].last_pc = pc - base;
            return;
        }
        if (st.buckets[i].key == 0) {
            st.buckets[i].key = key;
            st.buckets[i].count = 1;
            st.buckets[i].last_pc = pc - base;
            return;
        }
    }
}

// A thread parked in svcArbitrateLock: x0 = owner handle, x1 = the mutex word (libnx: the owner's
// handle, plus bit 30 when threads wait). Keyed by mutex + owner + the first caller addresses.
void record_lock_wait(SampledThread& st, const ThreadContext& ctx) {
    const u64 base = reinterpret_cast<u64>(&_start);
    u64 chain[4] = {ctx.lr - base, 0, 0, 0};
    u64 fp = ctx.fp, sp = ctx.sp;
    for (int i = 1; i < 4; i++) {
        if (fp < sp || fp - sp > 0x200000 || (fp & 15) != 0) break;
        const u64* frame = reinterpret_cast<const u64*>(fp);
        chain[i] = frame[1] - base;
        fp = frame[0];
    }
    const u64 mutex = ctx.cpu_gprs[1].x;
    const u32 owner = (u32)ctx.cpu_gprs[0].x;
    for (SampledThread::LockWait& w : st.locks) {
        if (w.count != 0 && w.mutex == mutex && w.owner == owner && w.chain[0] == chain[0] && w.chain[1] == chain[1]) {
            w.count++;
            return;
        }
    }
    for (SampledThread::LockWait& w : st.locks) {
        if (w.count == 0) {
            w.mutex = mutex;
            w.owner = owner;
            std::memcpy(w.chain, chain, sizeof(chain));
            w.count = 1;
            return;
        }
    }
}

void sampler_loop(void*) {
    while (sampler_running) {
        svcSleepThread(10'000'000LL);
        for (SampledThread& st : sampled) {
            Handle handle = INVALID_HANDLE;
            mutexLock(&nx_threads_lock);
            int seen = 0;
            for (const NxThread& t : nx_threads) {
                if (t.thread != nullptr && std::strncmp(t.name, st.prefix, std::strlen(st.prefix)) == 0) {
                    if (seen++ < st.ordinal) continue;
                    handle = t.handle;
                    std::strncpy(st.name, t.name, sizeof(st.name) - 1);
                    break;
                }
            }
            mutexUnlock(&nx_threads_lock);
            if (handle == INVALID_HANDLE) continue;
            if (R_FAILED(svcSetThreadActivity(handle, ThreadActivity_Paused))) {
                st.failed++;
                continue;
            }
            ThreadContext ctx;
            const Result rc = svcGetThreadContext3(&ctx, handle);
            svcSetThreadActivity(handle, ThreadActivity_Runnable);
            if (R_FAILED(rc)) {
                st.failed++;
                st.last_rc = rc;
                continue;
            }
            sample_pc(st, ctx.pc.x);
            const u64 fn = reinterpret_cast<u64>(&svcArbitrateLock);
            const u64 cv = reinterpret_cast<u64>(&svcWaitProcessWideKeyAtomic);
            // memcpy too (mutex 0 in the line): its callers name which copy costs the command thread 17-30%.
            const u64 mc = reinterpret_cast<u64>(&std::memcpy);
            if ((ctx.pc.x >= fn && ctx.pc.x < fn + 16) || (ctx.pc.x >= cv && ctx.pc.x < cv + 16) ||
                (ctx.pc.x >= mc && ctx.pc.x < mc + 0x400)) record_lock_wait(st, ctx);
        }
    }
}

// The top buckets of each sampled thread since the last report, as ELF offsets.
void print_pc_profile() {
    for (SampledThread& st : sampled) {
        if (st.samples == 0 && st.failed == 0) continue;
        char line[512];
        int n = std::snprintf(line, sizeof(line), "[nx] pc %s (%u samples):", st.name[0] ? st.name : st.prefix, st.samples);
        for (int top = 0; top < 6; top++) {
            SampledThread::Bucket* best = nullptr;
            for (SampledThread::Bucket& b : st.buckets) {
                if (b.key != 0 && (best == nullptr || b.count > best->count)) best = &b;
            }
            if (best == nullptr || best->count == 0) break;
            n += std::snprintf(line + n, sizeof(line) - n, top == 0 ? " +0x%llX %u%% (pc +0x%llX) |" : " +0x%llX %u%% |",
                               (unsigned long long)((best->key - 1) << 8), best->count * 100 / st.samples,
                               (unsigned long long)best->last_pc);
            best->key = 0;
            best->count = 0;
        }
        if (st.outside) n += std::snprintf(line + n, sizeof(line) - n, " outside %u%%", st.outside * 100 / st.samples);
        if (st.failed) n += std::snprintf(line + n, sizeof(line) - n, " (%u failed, rc 0x%X)", st.failed, st.last_rc);
        std::printf("%s\n", line);
        for (int top = 0; top < 3; top++) {
            SampledThread::LockWait* best = nullptr;
            for (SampledThread::LockWait& w : st.locks) {
                if (w.count != 0 && (best == nullptr || w.count > best->count)) best = &w;
            }
            if (best == nullptr) break;
            char owner[40] = "?";
            mutexLock(&nx_threads_lock);
            for (const NxThread& t : nx_threads) {
                if (t.thread != nullptr && t.handle == (best->owner & ~0x40000000u)) std::snprintf(owner, sizeof(owner), "%s", t.name);
            }
            mutexUnlock(&nx_threads_lock);
            std::printf("[nx] lockwait %s: %u of %u samples, mutex 0x%llX (elf +0x%llX) owner %s (0x%X) callers +0x%llX +0x%llX +0x%llX +0x%llX\n",
                        st.name[0] ? st.name : st.prefix, best->count, st.samples, (unsigned long long)best->mutex,
                        (unsigned long long)(best->mutex - reinterpret_cast<u64>(&_start)), owner, best->owner,
                        (unsigned long long)best->chain[0], (unsigned long long)best->chain[1],
                        (unsigned long long)best->chain[2], (unsigned long long)best->chain[3]);
            best->count = 0;
        }
        std::memset(st.locks, 0, sizeof(st.locks));
        std::memset(st.buckets, 0, sizeof(st.buckets));
        st.samples = st.outside = st.failed = 0;
    }
}

// The log is buffered and flushed twice a second; fsync commits it to the card (after a hang and
// a hard reboot, a log that was only flushed ended in garbage). Every 2 s a line gives the time
// and the memory, so a hang shows when it happened.
void log_flush_loop(void*) {
    const u64 start = armGetSystemTick();
    u64 last_profile = start;
    for (unsigned tick = 1; log_flush_running; tick++) {
        svcSleepThread(500'000'000LL);
        if (tick % 4 == 0) {
            u64 used = 0;
            svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
            const u64 now = armGetSystemTick();
            std::printf("[nx] %llu s, %llu MB used\n",
                        (unsigned long long)(armTicksToNs(now - start) / 1'000'000'000ULL),
                        (unsigned long long)(used >> 20));
            print_cpu_profile(now - last_profile);
            {
                static unsigned last_swaps = 0;
                const unsigned swaps = rex_nx_swaps;
                print_busy_profile(now - last_profile, swaps - last_swaps);
                last_swaps = swaps;
            }
            print_pc_profile();
            {
                static unsigned reports = 0;
                if (++reports % 15 == 0 && sampler_running) print_pc_cumulative();  // every 30 s
            }
            rex_nx_wait_report();
            {
                static unsigned long long last_c = 0, last_s = 0, last_y = 0;
                const unsigned long long c = rex_nx_rm_contended, s = rex_nx_rm_sleeps, y = rex_nx_rm_yields;
                std::printf("[nx] wait spin: %llu hits, %llu misses (total)\n", rex_nx_wait_spin_hits, rex_nx_wait_spin_misses);
                std::printf("[nx] rmutex: %llu contended locks, %llu yields, %llu 20 us sleeps in 2 s\n", c - last_c, y - last_y, s - last_s);
                last_c = c;
                last_s = s;
                last_y = y;
            }
            {
                // The GPU register window 0x7FC80000 folds onto physical 0xC80000: a guest store that
                // codegen did not recognise as MMIO lands there instead of in the command processor.
                // CP_RB_RPTR is register 0x1C4, CP_RB_WPTR 0x1C5.
                const uint8_t* phys = rex_nx_physical_base();
                if (phys != nullptr) {
                    const u32 rptr = __builtin_bswap32(*reinterpret_cast<const volatile u32*>(phys + 0xC80710));
                    const u32 wptr = __builtin_bswap32(*reinterpret_cast<const volatile u32*>(phys + 0xC80714));
                    std::printf("[nx] mmio probe: RAM behind CP_RB_RPTR 0x%08X, CP_RB_WPTR 0x%08X\n", rptr, wptr);
                }
            }
            {
                // The command processor's WAIT_REG_MEM polls since the last line (command_processor.cpp).
                static u64 last_polls = 0, last_sleep = 0, last_yields = 0, last_slept = 0;
                const u64 polls = rex_nx_wait_polls, sleep = rex_nx_wait_sleep_ms, yields = rex_nx_wait_yields;
                const u64 slept = rex_nx_wait_slept_ticks;
                if (polls != last_polls || yields != last_yields) {
                    std::printf("[nx] gpu waits: %llu sleeping polls (%llu ms asked, %llu ms really slept), %llu yield polls\n",
                                (unsigned long long)(polls - last_polls), (unsigned long long)(sleep - last_sleep),
                                (unsigned long long)(armTicksToNs(slept - last_slept) / 1000000ULL),
                                (unsigned long long)(yields - last_yields));
                }
                last_polls = polls;
                last_sleep = sleep;
                last_yields = yields;
                last_slept = slept;
                static unsigned long long last_mw = 0, last_mt = 0;
                const unsigned long long mw = rex_nx_multi_waits, mt = rex_nx_multi_timeouts;
                if (mw != last_mw) std::printf("[nx] multi waits: %llu calls, %llu ended by the 20 ms cap\n", mw - last_mw, mt - last_mt);
                last_mw = mw;
                last_mt = mt;
                static u64 last_fc = 0, last_fp = 0;
                const u64 fc = rex_nx_flush_calls, fp = rex_nx_flush_pages;
                if (fc != last_fc) {
                    std::printf("[nx] dirty pages: %llu flushes, %llu pages re-marked for upload\n",
                                (unsigned long long)(fc - last_fc), (unsigned long long)(fp - last_fp));
                }
                last_fc = fc;
                last_fp = fp;
            }
            last_profile = now;
            keep_clocks();
        }
        std::fflush(stdout);
        fsync(fileno(stdout));
    }
}

void open_log() {
    mkdir("sdmc:/switch", 0777);
    mkdir(kDataDir, 0777);
    char path[256];
    std::snprintf(path, sizeof(path), "%s/cvhd_log.txt", kDataDir);
    if (std::freopen(path, "w", stdout) == nullptr) {
        return;
    }
    // Buffered on the card (each flush costs); line-buffered in no-GPU runs (Eden, a host file), so
    // an abort keeps every line.
    const bool line_buffered = access("sdmc:/switch/cvhd/nogpu", F_OK) == 0;
    std::setvbuf(stdout, nullptr, line_buffered ? _IOLBF : _IOFBF, 64 * 1024);
    // stderr goes to the same file, through stdout's buffer, so the lines stay in order.
    std::fclose(stderr);
    stderr = stdout;
    log_flush_running = true;
    // Above the game's threads, on core 0 (never core 3). It sleeps between flushes.
    if (R_SUCCEEDED(threadCreate(&log_flush_thread, log_flush_loop, nullptr, nullptr, 0x4000, 0x20, 0))) {
        threadStart(&log_flush_thread);
    }
    {
        // Default 0 since 0.207: the 300 us spin of 0.189-0.206 held core 1 at priority 0x28 for up to 8 polls a frame
        // and is the one scheduling change between 0.187 (smooth ~50 fps) and 0.200 (40-45, 17/33 ms alternation).
        unsigned us = 0;
        if (FILE* f = std::fopen("sdmc:/switch/cvhd/cpspin_us", "r")) {
            if (std::fscanf(f, "%u", &us) != 1) us = 0;
            std::fclose(f);
        }
        rex_nx_cp_spin_ticks = uint64_t(us) * 192 / 10;  // armGetSystemTick: 19.2 MHz
        std::printf("[nx] WAIT_REG_MEM polls spin %u us before they sleep (file sdmc:/switch/cvhd/cpspin_us, 0 = off)\n", us);
        unsigned sleep_us = 100;
        if (FILE* f = std::fopen("sdmc:/switch/cvhd/cpsleep_us", "r")) {
            if (std::fscanf(f, "%u", &sleep_us) != 1 || sleep_us < 10) sleep_us = 100;
            std::fclose(f);
        }
        rex_nx_cp_sleep_us = sleep_us;
        std::printf("[nx] WAIT_REG_MEM polls sleep %u us each (file sdmc:/switch/cvhd/cpsleep_us)\n", sleep_us);
    }
    rex_nx_waitprof = access("sdmc:/switch/cvhd/prof", F_OK) == 0;
    std::printf("[nx] wait profile and PC sampler %s\n", rex_nx_waitprof ? "on (file sdmc:/switch/cvhd/prof)" : "off (file sdmc:/switch/cvhd/prof turns them on)");
    sampler_running = rex_nx_waitprof;
    if (sampler_running && R_SUCCEEDED(threadCreate(&sampler_thread, sampler_loop, nullptr, nullptr, 0x4000, 0x1C, 0))) {
        threadStart(&sampler_thread);
    }
}

}  // namespace

// Thread placement by name (see place_thread below); sdmc:/switch/cvhd/nopin turns it off.
bool pinning = true;
// The guest's other threads run on cores 0-2 (console 0.177 stage: core 0 was 93% idle under the waiting main thread
// while the workers and GPU Commands filled cores 1-2). sdmc:/switch/cvhd/core12 keeps them on cores 1-2 (A/B).
bool all_cores = true;

// An uncaught C++ exception ends in std::terminate -> abort -> libnx's exit, with nothing in the log.
// Name it first.
[[noreturn]] void terminate_handler() {
    std::printf("[nx] std::terminate called");
    if (std::exception_ptr ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            std::printf(": uncaught exception: %s", e.what());
        } catch (...) {
            std::printf(": uncaught exception of an unknown type");
        }
    }
    std::printf("\n");
    std::fflush(stdout);
    fsync(fileno(stdout));
    std::abort();
}

extern "C" void userAppInit(void) {
    open_log();
    std::set_terminate(terminate_handler);
    std::printf("[nx] Castlevania: Harmony of Despair, version " CVHD_VERSION ", built " CVHD_BUILD_UTC "\n");
    AppletType type = appletGetAppletType();
    const bool application = type == AppletType_Application || type == AppletType_SystemApplication;
    std::printf("[nx] launch: %s (applet type %d)\n",
                application ? "application (title takeover)" : "APPLET: not the full memory, start with title "
                                                               "takeover (hold R while starting a game)",
                (int)type);
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    // "used" counts libnx's heap, which takes almost all of the pool at start (fake_heap_*).
    std::printf("[nx] memory: pool %llu MB, %llu MB used at start (the heap itself: %llu MB)\n",
                (unsigned long long)(total >> 20), (unsigned long long)(used >> 20),
                (unsigned long long)((fake_heap_end - fake_heap_start) >> 20));
    // The RAM behind the guest memory: the first allocation of the program (memory_nx.cpp).
    rex_nx_reserve_pool();
    appletSetAutoSleepDisabled(true);
    raise_clocks();
    envSetExitFuncPtr(end_process);
    register_thread(threadGetSelf(), "main");
    const bool rm_spin = access("sdmc:/switch/cvhd/nospin", F_OK) != 0;
    rex_nx_rm_spin_enable(rm_spin);
    if (FILE* f = std::fopen("sdmc:/switch/cvhd/waitspin_us", "r")) {
        unsigned us = 0;
        if (std::fscanf(f, "%u", &us) == 1) rex_nx_wait_spin_ns = (unsigned long long)us * 1000;
        std::fclose(f);
    }
    std::printf("[nx] wait spin: %llu us before a plain wait sleeps (sdmc:/switch/cvhd/waitspin_us, 0 = off)\n", rex_nx_wait_spin_ns / 1000);
    std::printf("[nx] recursive mutexes: %s (sdmc:/switch/cvhd/nospin restores libnx's)\n", rm_spin ? "spin then 20 us sleeps" : "libnx kernel hand-off");
    pinning = access("sdmc:/switch/cvhd/nopin", F_OK) != 0;
    all_cores = access("sdmc:/switch/cvhd/core12", F_OK) != 0;
    std::printf("[nx] guest threads on cores %s (file sdmc:/switch/cvhd/core12 = 1-2)\n", all_cores ? "0-2" : "1-2");
    std::printf("[nx] thread placement %s\n", pinning ? "on (sdmc:/switch/cvhd/nopin turns it off)" : "off (nopin)");
    // Is the guest arena's memory as fast as malloc memory? (membench_nx.cpp, ~1 s, once.)
    rex_nx_membench();
}

// Cores and priorities by thread name (Horizon: lower number = more urgent; only 0x3B is time-sliced,
// in 10 ms slices; svcSleepThread(0) yields only to the same priority). The guest's main thread owns
// core 0 at 0x3A: it preempts the time-sliced workers that share the core when it wakes (in the stage it
// waits 80% of the time for them); the command processor polls the guest's values in 1 ms sleeps and
// must preempt the guest at once, so it sits above every guest thread on core 1; the vsync worker and
// audio go to core 2; the other guest threads start on cores 0, 2, 1 in turn and may run on all three.
// sdmc:/switch/cvhd/nopin turns this off (A/B). ponytail: a fixed table; revisit with the cpu line.
struct Placement {
    const char* prefix;
    u32 priority;
    int core;
    u64 mask;
};
const Placement placements[] = {
    {"Main XThread", 0x3A, 0, 0x1},
    {"GPU Commands", 0x28, 1, 0x2},
    {"GPU VSync", 0x27, 2, 0x4},
    {"Audio Worker", 0x2A, 2, 0x4},
    {"XMA", 0x2B, 2, 0x4},
    {"XThread", 0x3B, -1, 0x6},  // the game's other threads, time-sliced; mask 0x7 unless core12
};

void place_thread(Handle handle, const char* name) {
    if (!pinning) return;
    for (const Placement& p : placements) {
        if (std::strncmp(name, p.prefix, std::strlen(p.prefix)) == 0) {
            svcSetThreadPriority(handle, p.priority);
            const u64 mask = (all_cores && p.mask == 0x6) ? 0x7 : p.mask;
            static const int deal[3] = {0, 2, 1};
            static unsigned dealt = 0;
            const int core = p.core >= 0 ? p.core : all_cores ? deal[dealt++ % 3] : 1 + int(dealt++ % 2);
            svcSetThreadCoreMask(handle, core, mask);
            std::printf("[nx] thread \"%s\": priority 0x%X, core %d (mask 0x%llX)\n", name, p.priority, core,
                        (unsigned long long)mask);
            return;
        }
    }
}

// The SDK names its threads through pthread_setname_np (switch_compat.c sends it here).
extern "C" void rex_nx_thread_name(void* pthread, const char* name) {
    Handle handle = INVALID_HANDLE;
    mutexLock(&nx_threads_lock);
    for (NxThread& t : nx_threads) {
        if (t.thread == static_cast<Thread*>(pthread)) {
            std::snprintf(t.name, sizeof(t.name), "%s", name);
            handle = t.handle;
            break;
        }
    }
    mutexUnlock(&nx_threads_lock);
    if (handle != INVALID_HANDLE) place_thread(handle, name);
}

extern "C" void userAppExit(void) {
    if (sampler_running) {
        sampler_running = false;
        threadWaitForExit(&sampler_thread);
        threadClose(&sampler_thread);
    }
    log_flush_running = false;
    threadWaitForExit(&log_flush_thread);
    threadClose(&log_flush_thread);
    restore_clocks();
    std::fflush(stdout);
    fsync(fileno(stdout));
}

// libnx gives a thread made without a stack size only 128 KB (every std::thread). The linker
// sends pthread_create's call here (CMakeLists.txt, --wrap) to raise it to 2 MB.
constexpr size_t min_thread_stack = 2 * 1024 * 1024;

extern "C" int __real___syscall_thread_create(void** thread, void* (*func)(void*), void* arg, void* stack_addr,
                                              size_t stack_size);

// Every new thread starts on its creator's core (0) in libnx: deal them over cores 0-2 (never 3, the
// system's), as the Xbox port's xbox_nx_spread_thread does. ponytail: round robin; pin the GPU
// command processor and the guest's main thread to their own cores once the profile says where
// the time goes.
struct ThreadStart {
    void* (*func)(void*);
    void* arg;
};

static void* spread_and_run(void* p) {
    ThreadStart start = *static_cast<ThreadStart*>(p);
    delete static_cast<ThreadStart*>(p);
    // Named (and placed) by the creator before this ran? Then keep that placement; otherwise deal
    // the thread over cores 0-2 round robin.
    char name[40] = "thread";
    Thread* self = threadGetSelf();
    mutexLock(&nx_threads_lock);
    for (const NxThread& t : nx_threads) {
        if (t.thread == self) {
            std::snprintf(name, sizeof(name), "%s", t.name);
            break;
        }
    }
    mutexUnlock(&nx_threads_lock);
    if (std::strcmp(name, "thread") != 0 && pinning) {
        place_thread(CUR_THREAD_HANDLE, name);
    } else {
        static std::atomic<int> next{1};
        const int core = next.fetch_add(1, std::memory_order_relaxed) % 3;
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 0x7);
    }
    return start.func(start.arg);
}

extern "C" int __wrap___syscall_thread_create(void** thread, void* (*func)(void*), void* arg, void* stack_addr,
                                              size_t stack_size) {
    if (stack_addr == nullptr && stack_size < min_thread_stack) {
        stack_size = min_thread_stack;
    }
    const int rc = __real___syscall_thread_create(thread, spread_and_run, new ThreadStart{func, arg},
                                                  stack_addr, stack_size);
    if (rc == 0 && thread != nullptr && *thread != nullptr) {
        // libnx's pthread_t is its Thread*; the SDK names it right after this call.
        register_thread(static_cast<Thread*>(*thread), "thread");
    }
    return rc;
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(&_start);
    std::fprintf(stdout,
                 "[nx] crash: exception 0x%X, pc 0x%llX (ELF +0x%llX), lr 0x%llX (ELF +0x%llX), far 0x%llX, "
                 "sp 0x%llX\n",
                 ctx->error_desc, (unsigned long long)ctx->pc.x, (unsigned long long)(ctx->pc.x - base),
                 (unsigned long long)ctx->lr.x, (unsigned long long)(ctx->lr.x - base),
                 (unsigned long long)ctx->far.x, (unsigned long long)ctx->sp.x);
    for (int i = 0; i < 29; i++) {
        std::fprintf(stdout, "  x%-2d 0x%016llX%s", i, (unsigned long long)ctx->cpu_gprs[i].x,
                     (i % 4 == 3) ? "\n" : "");
    }
    std::fprintf(stdout, "\n  fp 0x%016llX\n", (unsigned long long)ctx->fp.x);
    std::fflush(stdout);
    fsync(fileno(stdout));
}

// Name of the calling thread as registered by the thread-create wrap (xobject.cpp's wait profile).
extern "C" void rex_nx_current_thread_name(char* out, unsigned long size) {
    out[0] = 0;
    const Thread* self = threadGetSelf();
    mutexLock(&nx_threads_lock);
    for (const NxThread& t : nx_threads) {
        if (t.thread == self) {
            std::snprintf(out, size, "%s", t.name);
            break;
        }
    }
    mutexUnlock(&nx_threads_lock);
    if (out[0] == 0) std::snprintf(out, size, "?");
}
