// Nintendo Switch (libnx) start-up for the cvhd port: the log on the SD card, the clocks, the exit
// path and the crash report. Everything lives in sdmc:/switch/cvhd/. Adapted from the N64 port's
// host/switch/switch_nx.cpp (rashevskyv/CBFD-Recompiled, branch switch).
#include <atomic>
#include <cstdio>
#include <cstring>
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

// The log is buffered and flushed twice a second; fsync commits it to the card (after a hang and
// a hard reboot, a log that was only flushed ended in garbage). Every 2 s a line gives the time
// and the memory, so a hang shows when it happened.
void log_flush_loop(void*) {
    const u64 start = armGetSystemTick();
    for (unsigned tick = 1; log_flush_running; tick++) {
        svcSleepThread(500'000'000LL);
        if (tick % 4 == 0) {
            u64 used = 0;
            svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
            std::printf("[nx] %llu s, %llu MB used\n",
                        (unsigned long long)(armTicksToNs(armGetSystemTick() - start) / 1'000'000'000ULL),
                        (unsigned long long)(used >> 20));
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
    std::setvbuf(stdout, nullptr, _IOFBF, 64 * 1024);
    // stderr goes to the same file, through stdout's buffer, so the lines stay in order.
    std::fclose(stderr);
    stderr = stdout;
    log_flush_running = true;
    // Above the game's threads, on core 0 (never core 3). It sleeps between flushes.
    if (R_SUCCEEDED(threadCreate(&log_flush_thread, log_flush_loop, nullptr, nullptr, 0x4000, 0x20, 0))) {
        threadStart(&log_flush_thread);
    }
}

}  // namespace

extern "C" void userAppInit(void) {
    open_log();
    std::printf("[nx] Castlevania: Harmony of Despair, version " CVHD_VERSION ", built " CVHD_BUILD_UTC "\n");
    AppletType type = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication) {
        std::printf("[nx] warning: started as an applet (type %d): start it with title takeover "
                    "(hold R while starting a game) for the full memory\n", (int)type);
    }
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::printf("[nx] memory: %llu MB, %llu MB used at start\n", (unsigned long long)(total >> 20),
                (unsigned long long)(used >> 20));
    appletSetAutoSleepDisabled(true);
    raise_clocks();
    envSetExitFuncPtr(end_process);
}

extern "C" void userAppExit(void) {
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
    static std::atomic<int> next{1};
    const int core = next.fetch_add(1, std::memory_order_relaxed) % 3;
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 0x7);
    return start.func(start.arg);
}

extern "C" int __wrap___syscall_thread_create(void** thread, void* (*func)(void*), void* arg, void* stack_addr,
                                              size_t stack_size) {
    if (stack_addr == nullptr && stack_size < min_thread_stack) {
        stack_size = min_thread_stack;
    }
    return __real___syscall_thread_create(thread, spread_and_run, new ThreadStart{func, arg}, stack_addr,
                                          stack_size);
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
