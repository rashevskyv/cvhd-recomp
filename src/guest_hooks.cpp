// Guest function hooks: a strong definition of a generated function replaces its weak thunk (DEFINE_REX_FUNC);
// __imp__<name> is the recompiled body.
#include <rex/ppc.h>
#include <cstdint>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <unistd.h>
#endif

extern "C" void __imp__sub_82620BD0(PPCContext& __restrict ctx, uint8_t* base);

// The XDK's "block until the GPU has consumed the ring to a position": one iteration (32x db16cyc, a no-op here,
// then the ring's read-pointer write-back word against the awaited position; returns 1 = keep waiting), looped by
// sub_826275C0. The guest's main thread spun through it at priority 0x3A on core 0 (24% of its samples in Eden
// 0.213), starving the time-sliced worker on that core. A short sleep per iteration frees the core; the wait's
// granularity becomes ~20-50 us, which the frame does not notice.
#ifdef __SWITCH__
extern "C" void rex_nx_waited_add(unsigned long long ticks);  // switch_nx.cpp: the busy/frame line
#endif
extern "C" void sub_82620BD0(PPCContext& __restrict ctx, uint8_t* base) {
#ifdef __SWITCH__
  const uint64_t t0 = armGetSystemTick();
  svcSleepThread(20000);
  rex_nx_waited_add(armGetSystemTick() - t0);  // a wait, not work
#else
  usleep(20);
#endif
  __imp__sub_82620BD0(ctx, base);
}
