// In-game settings menu (model: the Conker port's clx_menu.c): L3+R3 (both sticks pressed) opens and closes it
// (minus = the 360's BACK is the game's own menu: a conflict in 0.254), up/down pick a
// line, left/right change it, closing saves sdmc:/switch/cvhd/settings.txt. The game keeps running
// underneath and sees an idle pad. Drawn by the present pass as a text grid (rex_nx_menu_overlay).
#include <rex/nx_menu.h>

extern "C" [[noreturn]] void rex_nx_quick_exit(const char* why);  // switch/host/switch_nx.cpp

#include <switch.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace {

const char* const kSettingsPath = "sdmc:/switch/cvhd/settings.txt";

// 360 button bits as the pad driver reports them (rex/input/flags.h).
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kBack = 0x0020,
                   kLS = 0x0040, kRS = 0x0080, kA = 0x1000, kB = 0x2000;
constexpr uint16_t kChord = kLS | kRS;  // L3+R3 opens and closes the menu (minus = the 360's BACK opens the game's own menu)

std::atomic<int> g_fps_mode{0}, g_rumble{1}, g_volume{10}, g_fps_counter{0}, g_button_icons{1};  // volume in steps of 10 (the menu names index it)
// Diagnostics for the console A/B of the green halos (not saved: every start is back to the defaults).
std::atomic<int> g_tex_lod{0}, g_tex_filter{0}, g_blend{0}, g_reupload{0}, g_present{0};
std::atomic<int> g_record{0};  // RECORD 5 SEC: 1 = the backend records the shown frames for 5 s (frame_recorder.cpp), then sets 0

struct Item {
  const char* label;
  std::atomic<int>* value;
  int count;               // values 0..count-1; 0 = an action (EXIT)
  const char* names[11];   // text per value, or null: the number
};
Item kItems[] = {
    {"FRAME RATE", &g_fps_mode, 3, {"60", "30", "30 SKIP"}},
    {"RUMBLE", &g_rumble, 2, {"OFF", "ON"}},
    {"VOLUME", &g_volume, 11, {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"}},
    {"FPS COUNTER", &g_fps_counter, 2, {"OFF", "ON"}},
    {"BUTTON ICONS *", &g_button_icons, 2, {"XBOX", "SWITCH"}},  // * = after a restart (nx_glyphs.cpp)
    {"DBG TEX LOD", &g_tex_lod, 2, {"ZERO", "AUTO"}},   // ZERO = textureLod 0 (0.257 fix), AUTO = implicit LOD (0.256)
    {"DBG TEX FILTER", &g_tex_filter, 2, {"GAME", "POINT"}},
    {"DBG BLEND", &g_blend, 2, {"GAME", "OFF"}},
    {"DBG REUPLOAD", &g_reupload, 2, {"CHANGED", "ALL"}},  // ALL = every texture uploaded again each frame
    {"DBG PRESENT", &g_present, 3, {"AREA", "LINEAR", "POINT"}},  // POINT = the 0.265 scaling
    {"RECORD 5 SEC", nullptr, 0, {}},  // A: closes the menu and records 5 s to sdmc:/switch/cvhd/rec/
    {"EXIT GAME", nullptr, 0, {}},
};
constexpr int kItemCount = int(sizeof(kItems) / sizeof(kItems[0]));

std::mutex g_mutex;
bool g_open = false, g_loaded = false, g_armed = false;
int g_sel = 0;
uint16_t g_prev = 0;
uint64_t g_repeat_at = 0;

uint64_t NowMs() { return armTicksToNs(armGetSystemTick()) / 1000000ull; }

void Load() {
  g_loaded = true;
  if (FILE* fp = std::fopen(kSettingsPath, "r")) {
    char line[64];
    while (std::fgets(line, sizeof(line), fp)) {
      char* eq = std::strchr(line, '=');
      if (!eq) continue;
      *eq = 0;
      const int v = std::atoi(eq + 1);
      if (!std::strcmp(line, "fps")) g_fps_mode = v < 0 ? 0 : v > 2 ? 2 : v;
      if (!std::strcmp(line, "rumble")) g_rumble = v ? 1 : 0;
      if (!std::strcmp(line, "volume")) g_volume = v < 0 ? 0 : v > 100 ? 10 : v / 10;
      if (!std::strcmp(line, "fps_counter")) g_fps_counter = v ? 1 : 0;
      if (!std::strcmp(line, "button_icons")) g_button_icons = v ? 1 : 0;
    }
    std::fclose(fp);
    std::printf("[nx] settings: fps %d rumble %d volume %d fps_counter %d (%s)\n", g_fps_mode.load(), g_rumble.load(),
                g_volume.load() * 10, g_fps_counter.load(), kSettingsPath);
    return;
  }
  // No settings file yet: import the marker files the builds before the menu used.
  if (FILE* fp = std::fopen("sdmc:/switch/cvhd/fps", "r")) {
    char text[16] = {};
    if (std::fgets(text, sizeof(text), fp)) g_fps_mode = !std::strncmp(text, "30skip", 6) ? 2 : std::atoi(text) == 30 ? 1 : 0;
    std::fclose(fp);
  }
  if (access("sdmc:/switch/cvhd/mute", F_OK) == 0) g_volume = 0;
  if (access("sdmc:/switch/cvhd/norumble", F_OK) == 0) g_rumble = 0;
  std::printf("[nx] settings: no %s; from the marker files: fps %d rumble %d volume %d\n", kSettingsPath, g_fps_mode.load(),
              g_rumble.load(), g_volume.load() * 10);
}

void Save() {
  FILE* fp = std::fopen(kSettingsPath, "w");
  if (!fp) {
    std::printf("[nx] settings: cannot write %s\n", kSettingsPath);
    return;
  }
  std::fprintf(fp, "fps=%d\nrumble=%d\nvolume=%d\nfps_counter=%d\nbutton_icons=%d\n", g_fps_mode.load(), g_rumble.load(),
               g_volume.load() * 10, g_fps_counter.load(), g_button_icons.load());
  std::fclose(fp);
  std::printf("[nx] settings: saved fps %d rumble %d volume %d fps_counter %d\n", g_fps_mode.load(), g_rumble.load(),
              g_volume.load() * 10, g_fps_counter.load());
  std::fflush(stdout);
}

// 5x7 glyphs, 7 rows of 5 ('#' = on). Unknown characters draw nothing.
struct Glyph {
  char c;
  const char* rows;
};
const Glyph kFont[] = {
    {'A', ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'B', "####." "#...#" "#...#" "####." "#...#" "#...#" "####."},
    {'C', ".###." "#...#" "#...." "#...." "#...." "#...#" ".###."},
    {'D', "###.." "#..#." "#...#" "#...#" "#...#" "#..#." "###.."},
    {'E', "#####" "#...." "#...." "####." "#...." "#...." "#####"},
    {'F', "#####" "#...." "#...." "####." "#...." "#...." "#...."},
    {'G', ".###." "#...#" "#...." "#.###" "#...#" "#...#" ".####"},
    {'H', "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'I', ".###." "..#.." "..#.." "..#.." "..#.." "..#.." ".###."},
    {'J', "..###" "...#." "...#." "...#." "...#." "#..#." ".##.."},
    {'K', "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#"},
    {'L', "#...." "#...." "#...." "#...." "#...." "#...." "#####"},
    {'M', "#...#" "##.##" "#.#.#" "#.#.#" "#...#" "#...#" "#...#"},
    {'N', "#...#" "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#"},
    {'O', ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'P', "####." "#...#" "#...#" "####." "#...." "#...." "#...."},
    {'Q', ".###." "#...#" "#...#" "#...#" "#.#.#" "#..#." ".##.#"},
    {'R', "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#"},
    {'S', ".####" "#...." "#...." ".###." "....#" "....#" "####."},
    {'T', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.."},
    {'U', "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'V', "#...#" "#...#" "#...#" "#...#" "#...#" ".#.#." "..#.."},
    {'W', "#...#" "#...#" "#...#" "#.#.#" "#.#.#" "#.#.#" ".#.#."},
    {'X', "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#"},
    {'Y', "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#.."},
    {'Z', "#####" "....#" "...#." "..#.." ".#..." "#...." "#####"},
    {'0', ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###."},
    {'1', "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###."},
    {'2', ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####"},
    {'3', "#####" "...#." "..#.." "...#." "....#" "#...#" ".###."},
    {'4', "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#."},
    {'5', "#####" "#...." "####." "....#" "....#" "#...#" ".###."},
    {'6', "..##." ".#..." "#...." "####." "#...#" "#...#" ".###."},
    {'7', "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#..."},
    {'8', ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###."},
    {'9', ".###." "#...#" "#...#" ".####" "....#" "...#." ".##.."},
    {'-', "....." "....." "....." "#####" "....." "....." "....."},
    {'<', "...#." "..#.." ".#..." "#...." ".#..." "..#.." "...#."},
    {'>', ".#..." "..#.." "...#." "....#" "...#." "..#.." ".#..."},
    {':', "....." "..#.." "....." "....." "....." "..#.." "....."},
    {'/', "....." "....#" "...#." "..#.." ".#..." "#...." "....."},
    {'.', "....." "....." "....." "....." "....." ".##.." ".##.."},
    {'*', "....." ".#.#." "..#.." "#####" "..#.." ".#.#." "....."},
    {'%', "##..." "##..#" "...#." "..#.." ".#..." "#..##" "...##"},
    {'+', "....." "..#.." "..#.." "#####" "..#.." "..#.." "....."},
};

// The two floats of a character's glyph: rows 0-3 (20 bits) and rows 4-6 (15 bits).
void GlyphBits(char c, float* out) {
  out[0] = out[1] = 0.0f;
  for (const Glyph& g : kFont) {
    if (g.c != c) continue;
    uint32_t hi = 0, lo = 0;
    for (int r = 0; r < 7; r++) {
      uint32_t row = 0;
      for (int x = 0; x < 5; x++) row = (row << 1) | (g.rows[r * 5 + x] == '#');
      if (r < 4) hi |= row << (5 * (3 - r)); else lo |= row << (5 * (6 - r));
    }
    out[0] = float(hi);
    out[1] = float(lo);
    return;
  }
}

void PutText(NxOverlay* ov, int row, int col, const char* text) {
  for (; *text && col < ov->cols; text++, col++) GlyphBits(*text, ov->cells + (row * ov->cols + col) * 2);
}

void SetRowColor(NxOverlay* ov, int row, float r, float g, float b, float highlight) {
  ov->row_rgb[row][0] = r; ov->row_rgb[row][1] = g; ov->row_rgb[row][2] = b; ov->row_rgb[row][3] = highlight;
}

void Step(int dir) {
  Item& it = kItems[g_sel];
  if (!it.value) return;
  const int v = (it.value->load() + dir + it.count) % it.count;
  it.value->store(v);
  std::printf("[nx] menu: %s = %s\n", it.label, it.names[v]);
}

void Act() {
  if (kItems[g_sel].value) return;
  if (std::strcmp(kItems[g_sel].label, "RECORD 5 SEC") == 0) {
    g_record.store(1);
    g_open = false;  // the recording shows the game, not the menu
    std::printf("[nx] menu: record 5 s\n");
    std::fflush(stdout);
    return;
  }
  if (!g_armed) {
    g_armed = true;
    return;
  }
  Save();
  rex_nx_quick_exit("the settings menu's EXIT GAME");  // switch_nx.cpp: log, clocks back, svcExitProcess
}

}  // namespace

extern "C" int rex_nx_setting_fps_mode(void) { return g_fps_mode.load(); }
extern "C" int rex_nx_setting_rumble(void) { return g_rumble.load(); }
extern "C" int rex_nx_setting_volume(void) { return g_volume.load() * 10; }
extern "C" int rex_nx_setting_fps_counter(void) { return g_fps_counter.load(); }
extern "C" int rex_nx_setting_button_icons(void) { return g_button_icons.load(); }
extern "C" int rex_nx_debug_tex_lod(void) { return g_tex_lod.load(); }
extern "C" int rex_nx_debug_tex_filter(void) { return g_tex_filter.load(); }
extern "C" int rex_nx_debug_blend(void) { return g_blend.load(); }
extern "C" int rex_nx_debug_reupload(void) { return g_reupload.load(); }
extern "C" int rex_nx_debug_present(void) { return g_present.load(); }
extern "C" int rex_nx_debug_record(void) { return g_record.load(); }
extern "C" void rex_nx_debug_record_set(int on) { g_record.store(on); }

extern "C" int rex_nx_menu_input(uint16_t buttons, int16_t lx, int16_t ly) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_loaded) Load();
  uint16_t b = buttons;
  if (ly > 16000) b |= kUp;
  if (ly < -16000) b |= kDown;
  if (lx < -16000) b |= kLeft;
  if (lx > 16000) b |= kRight;
  uint16_t edge = b & ~g_prev;
  g_prev = b;
  const uint64_t now = NowMs();
  if ((b & kChord) == kChord && (edge & kChord)) {
    g_open = !g_open;
    g_armed = false;
    if (g_open) {
      std::printf("[nx] menu: open\n");
    } else {
      Save();
      std::printf("[nx] menu: closed\n");
    }
    std::fflush(stdout);
    return g_open;
  }
  if (!g_open) return 0;
  // Directions repeat while held: first after 350 ms, then every 120 ms.
  const uint16_t dirs = kUp | kDown | kLeft | kRight;
  if (edge & dirs) {
    g_repeat_at = now + 350;
  } else if ((b & dirs) && now >= g_repeat_at) {
    edge |= b & dirs;
    g_repeat_at = now + 120;
  }
  const int sel = g_sel;
  if (edge & kUp) g_sel = (g_sel + kItemCount - 1) % kItemCount;
  if (edge & kDown) g_sel = (g_sel + 1) % kItemCount;
  if (g_sel != sel) g_armed = false;
  if (edge & kLeft) Step(-1);
  if (edge & (kRight | kA)) {
    if (kItems[g_sel].value) Step(1); else Act();
  }
  if (edge & (kB | kStart)) {
    g_open = false;
    Save();
    std::printf("[nx] menu: closed\n");
    std::fflush(stdout);
  }
  return g_open;
}

extern "C" int rex_nx_menu_overlay(NxOverlay* ov, const char* fps_text, int out_w, int out_h) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::memset(ov, 0, sizeof(*ov));
  if (!g_open) {
    if (!fps_text || !*fps_text) return 0;
    ov->cols = int(std::strlen(fps_text)) > NX_OVERLAY_COLS ? NX_OVERLAY_COLS : int(std::strlen(fps_text));
    ov->rows = 1;
    ov->scale = 2;
    ov->x = 8;
    ov->y = 8;
    ov->box = 0;
    SetRowColor(ov, 0, 1.0f, 1.0f, 1.0f, 0.0f);
    PutText(ov, 0, 0, fps_text);
    return 1;
  }
  ov->cols = 40;
  ov->rows = 4 + kItemCount;
  ov->scale = 3;
  ov->x = (out_w - ov->cols * 6 * ov->scale) / 2;
  ov->y = (out_h - ov->rows * 8 * ov->scale) / 2;
  ov->box = 1;
  const char* title = "SETTINGS";
  PutText(ov, 0, (ov->cols - int(std::strlen(title))) / 2, title);
  SetRowColor(ov, 0, 1.0f, 0.82f, 0.24f, 0.0f);
  for (int i = 0; i < kItemCount; i++) {
    const Item& it = kItems[i];
    const int row = 2 + i;
    const bool sel = i == g_sel;
    PutText(ov, row, 2, it.label);
    char value[24] = "";
    if (it.value) {
      std::snprintf(value, sizeof(value), sel ? "< %s >" : "%s", it.names[it.value->load()]);
    } else if (sel && g_armed) {
      std::snprintf(value, sizeof(value), "PRESS AGAIN");
    }
    PutText(ov, row, ov->cols - 2 - int(std::strlen(value)), value);
    if (sel) SetRowColor(ov, row, 1.0f, 0.82f, 0.24f, 1.0f); else SetRowColor(ov, row, 0.92f, 0.92f, 0.92f, 0.0f);
  }
  const char* help = "UP/DOWN PICK  LEFT/RIGHT SET  L3+R3 CLOSE  * RESTART";
  PutText(ov, ov->rows - 1, (ov->cols - int(std::strlen(help))) / 2, help);
  SetRowColor(ov, ov->rows - 1, 0.6f, 0.6f, 0.65f, 0.0f);
  return 1;
}
