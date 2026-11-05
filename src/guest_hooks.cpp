// Guest function hooks: a strong definition of a generated function replaces its weak thunk (DEFINE_REX_FUNC);
// __imp__<name> is the recompiled body.
#include <rex/ppc.h>
#include <rex/system/xmemory.h>  // GuestPtr: the Switch address fold
#include <cstdint>
#include <cstring>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <thread>
#endif

extern "C" void __imp__sub_8263D048(PPCContext& __restrict ctx, uint8_t* base);

// The XDK's "block until the GPU has consumed the ring to a position": one iteration (32x db16cyc, a no-op here,
// then the ring's read-pointer write-back word against the awaited position; returns 1 = keep waiting), looped by
// sub_826275C0 (TU: the loop around sub_8263D048; base exe sub_82620BD0). The guest's main thread spun through it at priority 0x3A on core 0 (24% of its samples in Eden
// 0.213), starving the time-sliced worker on that core. A short sleep per iteration frees the core; the wait's
// granularity becomes ~20-50 us, which the frame does not notice.
#ifdef __SWITCH__
extern "C" void rex_nx_waited_add(unsigned long long ticks);  // switch_nx.cpp: the busy/frame line
#endif
extern "C" void sub_8263D048(PPCContext& __restrict ctx, uint8_t* base) {
#ifdef __SWITCH__
  const uint64_t t0 = armGetSystemTick();
  svcSleepThread(20000);
  rex_nx_waited_add(armGetSystemTick() - t0);  // a wait, not work
#else
  std::this_thread::yield();  // PC: many cores, and a Windows sleep is 1 ms coarse
#endif
  __imp__sub_8263D048(ctx, base);
}

// The XDK CRT's memcpy / memmove / memset (the main module and each character DLL carry their own copy; found
// by their first instructions, CLAUDE.md Findings 2026-10-29): one host call instead of a recompiled byte/word
// loop that paid the address fold and a dirty-page mark per store. r3 (the return value) is left as it was.
// memcpy is memmove here: the PPC loop copies forward, std::memcpy with an overlap is undefined.
// The DLL names are weak aliases kept global by tools/module_keep_symbols.py (it reads this file).
#ifdef __SWITCH__
extern "C" void rex_nx_mark_range(uint32_t guest, uint32_t length);  // memory_nx.cpp: the GPU's dirty pages
#define CVHD_MARK(guest, n) rex_nx_mark_range((guest), (n))
#else
#define CVHD_MARK(guest, n) ((void)0)  // SharedMemory watches the pages with faults on the other hosts
#endif
#define CVHD_MEMMOVE_HOOK(name) \
  extern "C" void name(PPCContext& __restrict ctx, uint8_t* base) { \
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32; \
  std::memmove(rex::memory::GuestPtr(base, dst), rex::memory::GuestPtr(base, src), n); \
  CVHD_MARK(dst, n); \
  }
#define CVHD_MEMSET_HOOK(name) \
  extern "C" void name(PPCContext& __restrict ctx, uint8_t* base) { \
  const uint32_t dst = ctx.r3.u32, n = ctx.r5.u32; \
  std::memset(rex::memory::GuestPtr(base, dst), int(ctx.r4.u32 & 0xFF), n); \
  CVHD_MARK(dst, n); \
  }
// title update exe (0.0.2.4; base exe: sub_8273A030 / sub_8273AD00 / sub_8273A590): memcpy 1208 call sites, memset 606, memmove falls into memcpy for dst < src
CVHD_MEMMOVE_HOOK(sub_827567D0)
CVHD_MEMMOVE_HOOK(sub_827574A0)
CVHD_MEMSET_HOOK(sub_82756D30)
// dllalucard
CVHD_MEMMOVE_HOOK(sub_83024C40)
CVHD_MEMMOVE_HOOK(sub_83025170)
CVHD_MEMSET_HOOK(sub_83024BA0)
// dllaxearmor
CVHD_MEMMOVE_HOOK(sub_835307D0)
CVHD_MEMMOVE_HOOK(sub_83530D00)
CVHD_MEMSET_HOOK(sub_83530730)
// dllcharlotte
CVHD_MEMMOVE_HOOK(sub_831D19D0)
CVHD_MEMMOVE_HOOK(sub_831D1F00)
CVHD_MEMSET_HOOK(sub_831D1930)
// dlljonathan
CVHD_MEMMOVE_HOOK(sub_83142AC0)
CVHD_MEMMOVE_HOOK(sub_83142FF0)
CVHD_MEMSET_HOOK(sub_83142A20)
// dllshanoa
CVHD_MEMMOVE_HOOK(sub_83262AC0)
CVHD_MEMMOVE_HOOK(sub_83262FF0)
CVHD_MEMSET_HOOK(sub_83262A20)
// dllsoma
CVHD_MEMMOVE_HOOK(sub_830B2AE0)
CVHD_MEMMOVE_HOOK(sub_830B3010)
CVHD_MEMSET_HOOK(sub_830B2A40)
