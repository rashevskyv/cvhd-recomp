// Checks cvhd::UntileFast against the SDK's texture_conversion::Untile + CopySwapBlock (random data, every block
// size and endian mode, odd sizes). Build and run: tests/run-untile-test.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>

#include "../src/gpu/untile_fast.h"

using namespace rex::graphics;

int main() {
  struct Fmt { xenos::TextureFormat f; uint32_t bpb; } fmts[] = {
      {xenos::TextureFormat::k_8, 1}, {xenos::TextureFormat::k_8_8, 2}, {xenos::TextureFormat::k_8_8_8_8, 4},
      {xenos::TextureFormat::k_DXT1, 8}, {xenos::TextureFormat::k_DXT4_5, 16}};
  const xenos::Endian endians[] = {xenos::Endian::kNone, xenos::Endian::k8in16, xenos::Endian::k8in32, xenos::Endian::k16in32};
  const uint32_t sizes[][2] = {{1, 1}, {7, 5}, {32, 32}, {33, 31}, {64, 40}, {100, 70}, {135, 76}, {512, 33}};
  srand(1234);
  int bad = 0, total = 0;
  for (auto& fm : fmts) {
    const FormatInfo* fi = FormatInfo::Get(fm.f);
    if (fi->bytes_per_block() != fm.bpb) { std::printf("format %s: %u bytes per block, expected %u\n", fi->name, fi->bytes_per_block(), fm.bpb); return 2; }
    for (auto e : endians) {
      for (auto& sz : sizes) {
        uint32_t w = sz[0], h = sz[1];
        uint32_t pitch = (w + 31) & ~31u, rows = (h + 31) & ~31u;
        std::vector<uint8_t> src(size_t(pitch) * rows * fm.bpb + 4096);
        for (auto& b : src) b = uint8_t(rand());
        std::vector<uint8_t> ref(size_t(w) * h * fm.bpb), fast(ref.size());
        // The oracle: swap every dword of the tiled source by the endian mode (what the GPU does on load), then
        // untile with the SDK's Untile without any swap. (The SDK's per-block CopySwapBlock is wrong for blocks
        // below a dword and overflows the heap for k16in32.)
        std::vector<uint8_t> swapped = src;
        for (size_t k = 0; k + 4 <= swapped.size(); k += 4) {
          uint32_t v;
          std::memcpy(&v, &swapped[k], 4);
          switch (e) {
            case xenos::Endian::k8in16: v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu); break;
            case xenos::Endian::k8in32: v = __builtin_bswap32(v); break;
            case xenos::Endian::k16in32: v = (v << 16) | (v >> 16); break;
            default: break;
          }
          std::memcpy(&swapped[k], &v, 4);
        }
        texture_conversion::UntileInfo u{};
        u.width = w; u.height = h; u.input_pitch = pitch; u.output_pitch = w;
        u.input_format_info = fi; u.output_format_info = fi;
        u.copy_callback = [](void* o, const void* i, size_t n) { std::memcpy(o, i, n); };
        texture_conversion::Untile(ref.data(), swapped.data(), &u);
        cvhd::UntileFast(fast.data(), src.data(), w, h, pitch, fm.bpb, cvhd::SwapMode(uint32_t(e)));
        total++;
        if (ref != fast) { bad++; std::printf("MISMATCH %s endian %u %ux%u\n", fi->name, uint32_t(e), w, h); }
      }
    }
  }
  // A base level in the packed mip tail: a 16x24 image at block 16,0 of its 32x32 tile (SDK offset_x/offset_y).
  for (auto& fm : fmts) {
    const FormatInfo* fi = FormatInfo::Get(fm.f);
    const uint32_t org[][2] = {{16, 0}, {0, 16}, {8, 0}};
    for (auto& o : org) {
      const uint32_t w = o[0] == 8 ? 8 : 16, h = o[1] ? 16 : 24, pitch = 32;
      std::vector<uint8_t> src(size_t(32) * 32 * fm.bpb + 4096);
      for (auto& b : src) b = uint8_t(rand());
      std::vector<uint8_t> ref(size_t(w) * h * fm.bpb), fast(ref.size());
      texture_conversion::UntileInfo u{};
      u.offset_x = o[0]; u.offset_y = o[1];
      u.width = w; u.height = h; u.input_pitch = pitch; u.output_pitch = w;
      u.input_format_info = fi; u.output_format_info = fi;
      u.copy_callback = [](void* d, const void* i, size_t n) { std::memcpy(d, i, n); };
      texture_conversion::Untile(ref.data(), src.data(), &u);
      cvhd::UntileFast(fast.data(), src.data(), w, h, pitch, fm.bpb, cvhd::SwapMode::kNone, o[0], o[1]);
      total++;
      if (ref != fast) { bad++; std::printf("MISMATCH %s origin %u,%u\n", fi->name, o[0], o[1]); }
    }
  }
  std::printf("%d of %d cases equal\n", total - bad, total);
  return bad ? 1 : 0;
}
