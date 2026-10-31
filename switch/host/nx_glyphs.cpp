// Button glyphs for the Switch's button positions. The pad maps by position (Switch B = 360 A, A = B,
// Y = X, X = Y), so the game's A/B and X/Y artwork is swapped in memory when the game reads the file
// (the XFile::Read bounce copy, switch/sdk-edits.py): the game data on the card stays untouched.
// Cells were measured on the original files (2026-10-31): the menu and pause prompts come from
// data/menu/ranktex_0.dds (DXT3 256x384, a row of coloured circles); the 4bpp sprite pages
// (data/opd/**/*.csr, 128x128, 0x30 header, 64 bytes per row) hold the small white glyph sets. The
// setting BUTTON ICONS = XBOX (nx_menu.cpp) leaves the files as they are from the next start.
#include <rex/nx_menu.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Swap two byte ranges of equal length in every row y0..y1-1 of a row-major image.
void SwapColumns(uint8_t* d, size_t origin, size_t pitch, size_t a, size_t b, size_t n, int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    uint8_t* row = d + origin + size_t(y) * pitch;
    for (size_t i = 0; i < n; i++) {
      const uint8_t t = row[a + i];
      row[a + i] = row[b + i];
      row[b + i] = t;
    }
  }
}

// Swap two vertical ranges of rows (same height) of a row-major image.
void SwapRows(uint8_t* d, size_t origin, size_t pitch, int ya, int yb, int h) {
  for (int y = 0; y < h; y++) {
    uint8_t* ra = d + origin + size_t(ya + y) * pitch;
    uint8_t* rb = d + origin + size_t(yb + y) * pitch;
    for (size_t i = 0; i < pitch; i++) {
      const uint8_t t = ra[i];
      ra[i] = rb[i];
      rb[i] = t;
    }
  }
}

struct Csr {
  const char* name;
  size_t a, b, n;  // byte ranges of the two cells in a row (4bpp: 2 pixels per byte)
  int y0, y1;      // rows
};
// 4bpp pages: A<->B and X<->Y cells, byte-aligned around the measured glyph bounds.
const Csr kCsr[] = {
    {"f_ui_btn0.csr", 1, 13, 11, 9, 31},     // A (cols 3-22) <-> B (27-45)
    {"f_ui_btn0.csr", 24, 36, 12, 9, 31},    // X (cols 50-69) <-> Y (75-93)
    {"ui_obj01.csr", 0, 23, 22, 8, 52},      // X (cols 1-42) <-> Y (49-90); the A/B circles are cut by the page edge: not swapped
    {"f_ui_equip06.csr", 0, 24, 24, 8, 48},  // X <-> Y
    {"f_ui_equip06.csr", 0, 24, 24, 48, 96}, // A <-> B
    {"f_ui_equip07.csr", 0, 24, 24, 76, 112},// X <-> Y
};
constexpr size_t kCsrSize = 8752, kCsrOrigin = 0x30, kCsrPitch = 64;

bool g_logged[8] = {};

void LogOnce(int i, const char* name) {
  if (g_logged[i]) return;
  g_logged[i] = true;
  std::printf("[nx] glyphs: %s swapped for the Switch's button positions\n", name);
  std::fflush(stdout);
}

}  // namespace

extern "C" void rex_nx_patch_file_read(const char* name, uint8_t* data, uint64_t file_offset, size_t length) {
  if (!name || file_offset != 0 || !rex_nx_setting_button_icons()) return;
  const size_t n = std::strlen(name);
  if (n == 13 && !std::strcmp(name, "ranktex_0.dds") && length >= 98432) {
    // DXT3 256x384 (64 blocks per row, 16 bytes per block): circles A B X Y in block rows 8-18 at block
    // columns 0 / 12 / 24 / 36, 12 blocks wide. A<->B, X<->Y.
    SwapColumns(data, 128, 64 * 16, 0 * 16, 12 * 16, 12 * 16, 8, 19);
    SwapColumns(data, 128, 64 * 16, 24 * 16, 36 * 16, 12 * 16, 8, 19);
    LogOnce(0, name);
    return;
  }
  if (n == 17 && !std::strcmp(name, "f_ui_select00.csr") && length >= kCsrSize) {
    // A column of circles X (rows 2-28), Y (30-54), A (56-80), B (82-106) at cols 0-21 (bytes 0-10).
    SwapRows(data, kCsrOrigin, kCsrPitch, 2, 29, 26);   // X <-> Y (rows 2-27 <-> 29-54)
    SwapRows(data, kCsrOrigin, kCsrPitch, 55, 81, 26);  // A <-> B (rows 55-80 <-> 81-106)
    LogOnce(1, name);
    return;
  }
  int k = 2;
  for (const Csr& c : kCsr) {
    k++;
    if (std::strcmp(name, c.name) || length < kCsrSize) continue;
    SwapColumns(data, kCsrOrigin, kCsrPitch, c.a, c.b, c.n, c.y0, c.y1);
    LogOnce(k < 8 ? k : 7, name);
  }
}
