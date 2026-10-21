// Horizon memory probe for the cvhd port. Answers, on hardware, the questions the ReXGlue memory model
// raises (CLAUDE.md "Guest memory model"):
//   1. Can one RAM range be mapped at several addresses (svcMapProcessMemory on the own process)?
//   2. CodeMemory MapOwner + MapSlave: two views, slave read-only?
//   3. Can an aliased page be reprotected (svcSetMemoryPermission / svcSetProcessMemoryPermission)?
//   4. Does a data abort on a protected page resume after our own __libnx_exception_entry fixes it?
//   5. How many CodeMemory handles may one process hold?
//   6. svcMapPhysicalMemory at an arbitrary address (needs system resource size)?
// Log: sdmc:/switch/cvhd/probe_log.txt. Exits by itself after the tests.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>

static FILE* g_log;
static void plog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char line[512];
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    printf("%s\n", line);
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);
        fsync(fileno(g_log));
    }
}

static void* reserve(size_t size) {
    virtmemLock();
    void* p = virtmemFindAslr(size, 0x1000);
    if (p) virtmemAddReservation(p, size);
    virtmemUnlock();
    return p;
}

// Test 4 state: exc.s calls probe_fault(type, frame) and resumes when it returns 0.
volatile u8* g_guard_page;
volatile int g_faults;
int probe_fault(u64 type, ThreadExceptionFrameA64* frame) {
    u64 far = frame->far;
    if (g_guard_page && far >= (u64)g_guard_page && far < (u64)g_guard_page + 0x1000) {
        g_faults++;
        Result rc = svcSetMemoryPermission((void*)g_guard_page, 0x1000, Perm_Rw);
        plog("  fault type %#lx at pc %#lx far %#lx esr %#x -> reprotect rw rc %#x, resuming",
             type, frame->elr_el1, far, frame->esr, rc);
        return rc == 0 ? 0 : 1;
    }
    plog("  unexpected fault type %#lx pc %#lx far %#lx esr %#x", type, frame->elr_el1, far, frame->esr);
    return 1;
}

int main(int argc, char* argv[]) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    mkdir("sdmc:/switch/cvhd", 0777);
    g_log = fopen("sdmc:/switch/cvhd/probe_log.txt", "w");

    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    plog("probe start: memory total %lu MB used %lu MB, applet %d", total >> 20, used >> 20,
         appletGetAppletType());

    const size_t size = 0x100000;  // 1 MB per test
    Result rc;

    // 1. svcMapProcessMemory alias of heap memory.
    {
        u8* src = aligned_alloc(0x1000, size);
        memset(src, 0x11, size);
        u8* dst1 = reserve(size);
        u8* dst2 = reserve(size);
        rc = svcMapProcessMemory(dst1, CUR_PROCESS_HANDLE, (u64)src, size);
        plog("1. svcMapProcessMemory heap -> %p: rc %#x", dst1, rc);
        if (rc == 0) {
            src[0x100] = 0x22;
            dst1[0x200] = 0x33;
            plog("   alias ok: dst sees %#x (want 0x22), src sees %#x (want 0x33), src still readable %#x",
                 dst1[0x100], src[0x200], src[0]);
            rc = svcMapProcessMemory(dst2, CUR_PROCESS_HANDLE, (u64)src, size);
            plog("   second alias -> %p: rc %#x%s", dst2, rc, rc == 0 ? ", two aliases work" : "");
            if (rc == 0) {
                dst2[0x300] = 0x44;
                plog("   dst1 sees %#x (want 0x44)", dst1[0x300]);
            }
            // 3. reprotect the alias.
            rc = svcSetMemoryPermission(dst1, 0x1000, Perm_R);
            plog("3. svcSetMemoryPermission(alias, R): rc %#x", rc);
            if (rc != 0) {
                rc = svcSetProcessMemoryPermission(CUR_PROCESS_HANDLE, (u64)dst1, 0x1000, Perm_R);
                plog("   svcSetProcessMemoryPermission(alias, R): rc %#x", rc);
            }
            Result rc_src = svcSetMemoryPermission(src, 0x1000, Perm_R);
            plog("   svcSetMemoryPermission(source heap page, R): rc %#x", rc_src);
            if (rc_src == 0) svcSetMemoryPermission(src, 0x1000, Perm_Rw);
            if (rc == 0) {
                // 4. fault + resume on the protected alias page.
                g_guard_page = dst1;
                plog("4. writing to the read-only alias page (expect one fault, then resume)");
                dst1[0x10] = 0x55;
                plog("   after write: faults %d, value %#x (want 0x55), src sees %#x", g_faults, dst1[0x10],
                     src[0x10]);
                g_guard_page = NULL;
            }
        }
    }

    // 2. CodeMemory owner + slave views.
    {
        u8* src = aligned_alloc(0x1000, size);
        memset(src, 0x66, size);
        Handle h;
        rc = svcCreateCodeMemory(&h, src, size);
        plog("2. svcCreateCodeMemory: rc %#x", rc);
        if (rc == 0) {
            u8* owner = reserve(size);
            u8* slave = reserve(size);
            rc = svcControlCodeMemory(h, CodeMapOperation_MapOwner, owner, size, Perm_Rw);
            plog("   MapOwner rw -> %p: rc %#x", owner, rc);
            Result rc2 = svcControlCodeMemory(h, CodeMapOperation_MapSlave, slave, size, Perm_Rx);
            plog("   MapSlave rx -> %p: rc %#x", slave, rc2);
            if (rc == 0 && rc2 == 0) {
                owner[0x40] = 0x77;
                plog("   slave sees %#x (want 0x77), owner sees %#x (want 0x66)", slave[0x40], owner[0]);
                Result rc3 = svcSetMemoryPermission(owner, 0x1000, Perm_R);
                plog("   svcSetMemoryPermission(owner page, R): rc %#x", rc3);
                if (rc3 == 0) svcSetMemoryPermission(owner, 0x1000, Perm_Rw);
                rc3 = svcSetProcessMemoryPermission(CUR_PROCESS_HANDLE, (u64)owner, 0x1000, Perm_R);
                plog("   svcSetProcessMemoryPermission(owner page, R): rc %#x", rc3);
                if (rc3 == 0) svcSetProcessMemoryPermission(CUR_PROCESS_HANDLE, (u64)owner, 0x1000, Perm_Rw);
            }
        }
    }

    // 5. How many CodeMemory handles.
    {
        int n = 0;
        for (; n < 1024; n++) {
            u8* src = aligned_alloc(0x1000, 0x1000);
            if (!src) break;
            Handle h;
            rc = svcCreateCodeMemory(&h, src, 0x1000);
            if (rc != 0) {
                plog("5. CodeMemory handle %d failed: rc %#x", n, rc);
                break;
            }
        }
        plog("5. CodeMemory handles created: %d (cap 1024)", n);
    }

    // 6. svcMapPhysicalMemory at a reserved address.
    {
        u8* addr = reserve(size);
        rc = svcMapPhysicalMemory(addr, size);
        plog("6. svcMapPhysicalMemory(%p, 1 MB): rc %#x", addr, rc);
        if (rc == 0) {
            addr[0] = 1;
            svcUnmapPhysicalMemory(addr, size);
        }
    }

    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    plog("probe done: used %lu MB. Press + to exit.", used >> 20);
    if (g_log) fclose(g_log);
    g_log = NULL;

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
