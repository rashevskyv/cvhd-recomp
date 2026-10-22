#!/usr/bin/env python3
"""Generates census_generated.cpp: a counting wrapper for every recompiled function in an address
range. The generated functions are weak aliases (`sub_X` -> `__imp__sub_X`), so a strong `sub_X`
here takes every direct and table call, counts it and runs the original.

Every wrapper records its callers (the PowerPC link register holds the return address) and the PM4
words the call wrote: the device's command-buffer write position (device+48, "last word written")
before and after the call, inclusive of nested calls, and the packets in that range decoded (type-0
register writes by register group, type-3 opcodes). The device pointer is r3 of `device_fn`, a
function known to take the device (the register writer). A reporter thread prints the busiest
functions every 5 s with calls per frame, PM4 words per frame (inclusive and self), the packet
histogram and up to 6 caller addresses; the swap function is the frame counter. A function whose
callers lie outside the Direct3D library is one of the game's entry points
(docs/native-renderer-plan.md M1).

    python3 tools/census_gen.py generated/default/cvhd_register.cpp src/census/census_generated.cpp \
        0x82600000 0x82660000 sub_826287A8 sub_8262CAF0
"""
import re
import sys

register_cpp, out_cpp, lo, hi, swap_fn = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16), sys.argv[5]
device_fn = sys.argv[6] if len(sys.argv) > 6 else 'sub_8262CAF0'
# Functions whose first calls are logged word by word (before/after write positions, the packets).
trace_fns = sys.argv[7].split(',') if len(sys.argv) > 7 else []
addrs = sorted({int(m, 16) for m in re.findall(r'SetFunction\(0x([0-9A-Fa-f]{8}), sub_', open(register_cpp).read())
                if lo <= int(m, 16) < hi})
names = ['sub_%08X' % a for a in addrs]
n = len(names)
swap_index = names.index(swap_fn) if swap_fn in names else -1
device_index = names.index(device_fn) if device_fn in names else -1
trace_indices = [names.index(t) for t in trace_fns if t in names]

HEAD = r'''#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <rex/ppc.h>
#include <rex/logging.h>
namespace {
constexpr int kN = @N@;
constexpr int kSwapIndex = @SWAP@;
constexpr int kDeviceIndex = @DEVIDX@;
constexpr int kTrace[] = {@TRACE@};
constexpr int kTraceCalls = 4;
constexpr int kCallers = 12;
struct Stat {
  std::atomic<uint32_t> calls{0};
  std::atomic<uint32_t> words{0};      // PM4 words written during the call, nested calls included
  std::atomic<uint32_t> self_words{0}; // the same minus the words of nested wrapped calls
  std::atomic<uint32_t> switches{0};   // calls after which the write position moved backwards (segment switch)
  std::atomic<uint32_t> callers[kCallers];       // the first kCallers distinct return addresses
  std::atomic<uint32_t> caller_calls[kCallers];  // calls from each of them
  std::atomic<uint32_t> t3[128];       // type-3 opcode histogram (inclusive)
  std::atomic<uint32_t> t0[8];         // type-0 writes: <0x2000, 0x2000-0x23FF, ALU 0x4000-0x47FF, fetch 0x4800-0x48FF, bool/loop 0x4900-0x4FFF, other, type-2 nop, type-1
};
Stat census[kN];
const char* const census_names[kN] = {
@NAMES@
};
thread_local uint32_t census_nested_words = 0;
uint8_t* census_base = nullptr;
std::atomic<uint32_t> census_device{0};
inline uint32_t be32(const uint8_t* base, uint32_t addr) { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + addr)); }
const char* op_name(int op) {
  switch (op) {
    case 0x22: return "DRAW_INDX"; case 0x36: return "DRAW_INDX_2"; case 0x2D: return "SET_CONSTANT";
    case 0x2F: return "LOAD_ALU_CONST"; case 0x27: return "IM_LOAD"; case 0x2B: return "IM_LOAD_IMM";
    case 0x3C: return "WAIT_REG_MEM"; case 0x46: return "EVENT_WRITE"; case 0x64: return "XE_SWAP";
    case 0x3F: return "INDIRECT_BUFFER"; case 0x3D: return "MEM_WRITE"; case 0x21: return "REG_RMW";
    case 0x26: return "WAIT_FOR_IDLE"; case 0x3B: return "INVALIDATE_STATE"; case 0x10: return "NOP";
    case 0x45: return "COND_WRITE"; case 0x5E: return "CONTEXT_UPDATE"; case 0x54: return "INTERRUPT";
    case 0x56: return "SET_SHADER_CONST"; case 0x58: return "EVENT_WRITE_SHD"; case 0x5A: return "EVENT_WRITE_EXT";
    default: return nullptr;
  }
}
void census_note_caller(Stat& s, uint32_t lr) {
  for (int k = 0; k < kCallers; k++) {
    uint32_t v = s.callers[k].load(std::memory_order_relaxed);
    if (v == 0) { uint32_t expected = 0; if (!s.callers[k].compare_exchange_strong(expected, lr) && expected != lr) continue; v = lr; }
    if (v == lr) { s.caller_calls[k].fetch_add(1, std::memory_order_relaxed); return; }
  }
}
void census_pm4(Stat& s, const uint8_t* base, uint32_t before, uint32_t after) {
  for (uint32_t p = before + 4; p <= after;) {
    uint32_t hdr = be32(base, p);
    uint32_t type = hdr >> 30;
    if (type == 3) {
      s.t3[(hdr >> 8) & 0x7F].fetch_add(1, std::memory_order_relaxed);
      p += 4 * (1 + ((hdr >> 16) & 0x3FFF) + 1);
    } else if (type == 0) {
      uint32_t reg = hdr & 0x7FFF;
      int b = reg < 0x2000 ? 0 : reg < 0x2400 ? 1 : reg < 0x4000 ? 5 : reg < 0x4800 ? 2 : reg < 0x4900 ? 3 : reg < 0x5000 ? 4 : 5;
      s.t0[b].fetch_add(1, std::memory_order_relaxed);
      p += 4 * (1 + ((hdr >> 16) & 0x3FFF) + 1);
    } else if (type == 2) {
      s.t0[6].fetch_add(1, std::memory_order_relaxed);
      p += 4;
    } else {
      s.t0[7].fetch_add(1, std::memory_order_relaxed);
      p += 4;
    }
  }
}
void census_report_loop() {
  std::vector<uint32_t> last_calls(kN, 0), last_words(kN, 0), last_self(kN, 0);
  static const char* const t0names[8] = {"r<2000", "r2000", "alu", "fetch", "bool", "rother", "nop", "t1"};
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    struct Row { uint32_t calls, words, self; int i; };
    std::vector<Row> rows;
    uint32_t frames = 0;
    for (int i = 0; i < kN; i++) {
      uint32_t c = census[i].calls.load(std::memory_order_relaxed), w = census[i].words.load(std::memory_order_relaxed), sw = census[i].self_words.load(std::memory_order_relaxed);
      Row r{c - last_calls[i], w - last_words[i], sw - last_self[i], i};
      last_calls[i] = c; last_words[i] = w; last_self[i] = sw;
      if (i == kSwapIndex) frames = r.calls;
      if (r.calls) rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.words != b.words ? a.words > b.words : a.calls > b.calls; });
    // The busiest 48 by PM4 words, then the busiest 24 by calls among the rest (entry points that write nothing).
    if (rows.size() > 48) {
      std::sort(rows.begin() + 48, rows.end(), [](const Row& a, const Row& b) { return a.calls > b.calls; });
      if (rows.size() > 72) rows.resize(72);
    }
    double f = frames ? frames : 1;
    uint32_t dev = census_device.load();
    std::string line = "census3 (5 s, " + std::to_string(frames) + " frames, " + std::to_string(rows.size()) + " distinct): ";
    int shown = 0;
    for (const Row& r : rows) {
      if (shown++ >= 72) break;
      char buf[256];
      std::snprintf(buf, sizeof(buf), "%s calls %.1f/f words %.1f/f self %.1f/f callers", census_names[r.i], r.calls / f, r.words / f, r.self / f);
      line += buf;
      Stat& s = census[r.i];
      for (int k = 0; k < kCallers; k++) {
        uint32_t v = s.callers[k].load(std::memory_order_relaxed);
        if (!v) break;
        std::snprintf(buf, sizeof(buf), " %08X:%u", v, s.caller_calls[k].exchange(0, std::memory_order_relaxed));
        line += buf;
      }
      if (r.words) {
        line += " [";
        for (int op = 0; op < 128; op++) {
          uint32_t c = s.t3[op].exchange(0, std::memory_order_relaxed);
          if (!c) continue;
          const char* nm = op_name(op);
          if (nm) std::snprintf(buf, sizeof(buf), " %s %.1f", nm, c / f); else std::snprintf(buf, sizeof(buf), " op%02X %.1f", op, c / f);
          line += buf;
        }
        for (int b = 0; b < 8; b++) {
          uint32_t c = s.t0[b].exchange(0, std::memory_order_relaxed);
          if (!c) continue;
          std::snprintf(buf, sizeof(buf), " %s %.1f", t0names[b], c / f);
          line += buf;
        }
        line += " ]";
      }
      uint32_t sw = s.switches.exchange(0, std::memory_order_relaxed);
      if (sw) { std::snprintf(buf, sizeof(buf), " switches %u", sw); line += buf; }
      line += " | ";
    }
    REXLOG_INFO("{}", line);
    if (census_base && dev) {
      REXLOG_INFO("census3 device 0x{:08X} write 0x{:08X} end 0x{:08X} limit 0x{:08X}", dev, be32(census_base, dev + 48), be32(census_base, dev + 52), be32(census_base, dev + 56));
    }
  }
}
struct CensusStart { CensusStart() { std::thread(census_report_loop).detach(); } } census_start;
template <void (*Impl)(PPCContext&, uint8_t*)>
inline void census_call(int i, PPCContext& ctx, uint8_t* base) {
  Stat& s = census[i];
  s.calls.fetch_add(1, std::memory_order_relaxed);
  census_note_caller(s, ctx.lr);
  if (i == kDeviceIndex && ctx.r3.u32) { census_base = base; census_device.store(ctx.r3.u32, std::memory_order_relaxed); }
  uint32_t dev = census_device.load(std::memory_order_relaxed);
  if (!dev) { Impl(ctx, base); return; }
  // A lock wait in another thread "sees" the words the drawing thread writes meanwhile (tens of
  // thousands): a call whose r3 is not the device is credited at most 2048 words.
  const uint32_t max_words = ctx.r3.u32 == dev ? (1u << 30) : 2048;
  uint32_t saved_nested = census_nested_words;
  census_nested_words = 0;
  uint32_t before = be32(base, dev + 48);
  uint32_t end_before = be32(base, dev + 52);  // the segment's end: a write position past it is a new segment
  Impl(ctx, base);
  uint32_t after = be32(base, dev + 48);
  // BeginVertices-style draws leave +48 at the state's end and the draw packet's last word at +13652
  // (EndVertices copies it to +48 later): take the farther of the two.
  uint32_t draw_end = be32(base, dev + 13652);
  for (size_t k = 0; k < sizeof(kTrace) / sizeof(kTrace[0]); k++) {
    if (kTrace[k] != i) continue;
    static std::atomic<uint32_t> traced[sizeof(kTrace) / sizeof(kTrace[0])];
    if (traced[k].fetch_add(1) >= kTraceCalls) break;
    std::string words;
    for (uint32_t p = before + 4; p <= after && p < before + 4 + 48 * 4; p += 4) { char b[12]; std::snprintf(b, sizeof(b), " %08X", be32(base, p)); words += b; }
    REXLOG_INFO("census3 trace {} r3 {:08X} r4 {:08X} r5 {:08X} r6 {:08X} before {:08X} after {:08X} end {:08X} limit {:08X} draw_end {:08X} words:{}", census_names[i], ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, before, after, end_before, be32(base, dev + 56), draw_end, words);
    break;
  }
  if (draw_end > after && draw_end <= end_before && draw_end - after <= (64u << 10)) after = draw_end;
  uint32_t incl = 0;
  if (after > before && after <= end_before && (after - before) / 4 <= max_words) {
    incl = (after - before) / 4;
    s.words.fetch_add(incl, std::memory_order_relaxed);
    s.self_words.fetch_add(incl > census_nested_words ? incl - census_nested_words : 0, std::memory_order_relaxed);
    census_pm4(s, base, before, after);
  } else if (after != before) {
    s.switches.fetch_add(1, std::memory_order_relaxed);
  }
  census_nested_words = saved_nested + incl;
}
}  // namespace
extern "C" {
'''

out = ['// Generated by tools/census_gen.py: call, caller and PM4 counters for %d functions in 0x%08X-0x%08X.' % (n, lo, hi)]
out.append(HEAD.replace('@N@', str(n)).replace('@SWAP@', str(swap_index)).replace('@DEVIDX@', str(device_index))
           .replace('@TRACE@', ', '.join(str(t) for t in trace_indices) or '-1')
           .replace('@NAMES@', '\n'.join('    "%s",' % name for name in names)))
for i, name in enumerate(names):
    out.append('void __imp__%s(PPCContext& __restrict ctx, uint8_t* base);' % name)
    out.append('void %s(PPCContext& __restrict ctx, uint8_t* base) { census_call<__imp__%s>(%d, ctx, base); }' % (name, name, i))
out.append('}')
open(out_cpp, 'w', newline='\n').write('\n'.join(out) + '\n')
print('%d wrappers, swap index %d, device from %s (index %d)' % (n, swap_index, device_fn, device_index))
