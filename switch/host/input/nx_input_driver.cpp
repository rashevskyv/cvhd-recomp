#include <rex/input/nx/nx_input_driver.h>

#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include <rex/input/flags.h>
#include <rex/logging.h>
#include <rex/nx_menu.h>

namespace rex::input::nx {

namespace {

constexpr DeviceId kPad = static_cast<DeviceId>(0x4E580000);  // "NX"

// Positional mapping: the Switch's B/A/Y/X sit where the 360's A/B/X/Y sit.
struct ButtonMap {
  uint64_t nx;
  uint16_t x360;
};
constexpr ButtonMap kButtons[] = {
    {HidNpadButton_B, X_INPUT_GAMEPAD_A},
    {HidNpadButton_A, X_INPUT_GAMEPAD_B},
    {HidNpadButton_Y, X_INPUT_GAMEPAD_X},
    {HidNpadButton_X, X_INPUT_GAMEPAD_Y},
    {HidNpadButton_Plus, X_INPUT_GAMEPAD_START},
    {HidNpadButton_Minus, X_INPUT_GAMEPAD_BACK},
    {HidNpadButton_L, X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {HidNpadButton_R, X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {HidNpadButton_StickL, X_INPUT_GAMEPAD_LEFT_THUMB},
    {HidNpadButton_StickR, X_INPUT_GAMEPAD_RIGHT_THUMB},
    {HidNpadButton_Up, X_INPUT_GAMEPAD_DPAD_UP},
    {HidNpadButton_Down, X_INPUT_GAMEPAD_DPAD_DOWN},
    {HidNpadButton_Left, X_INPUT_GAMEPAD_DPAD_LEFT},
    {HidNpadButton_Right, X_INPUT_GAMEPAD_DPAD_RIGHT},
};

int16_t ClampStick(int32_t v) {
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return static_cast<int16_t>(v);
}

}  // namespace

NXInputDriver::NXInputDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {}

NXInputDriver::~NXInputDriver() = default;

X_STATUS NXInputDriver::Setup() {
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad_);
  rumble_enabled_ = access("sdmc:/switch/cvhd/norumble", F_OK) != 0;
  vib_ok_[0] = R_SUCCEEDED(hidInitializeVibrationDevices(vib_[0], 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
  // ponytail: the No1 handles are made for the style present at start (Pro Controller or a Joy-Con pair);
  // a controller swapped later keeps the handheld set or none until the next start.
  const u32 style1 = hidGetNpadStyleSet(HidNpadIdType_No1);
  vib_ok_[1] = R_SUCCEEDED(hidInitializeVibrationDevices(vib_[1], 2, HidNpadIdType_No1,
                                                          (style1 & HidNpadStyleTag_NpadFullKey) ? HidNpadStyleTag_NpadFullKey : HidNpadStyleTag_NpadJoyDual));
  REXLOG_INFO("[nx] input: libnx pad (handheld / Joy-Con pair / Pro Controller) as gamepad 0; rumble {} (handheld {}, No1 {})",
              rumble_enabled_ ? "on" : "off (norumble)", vib_ok_[0] ? "ok" : "no", vib_ok_[1] ? "ok" : "no");
  return X_STATUS_SUCCESS;
}

void NXInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  DeviceInfo info;
  info.id = kPad;
  info.name = "Switch pad";
  info.synthetic = false;
  out.push_back(info);
}

X_RESULT NXInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
  (void)flags;
  if (id != kPad) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
    out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

// Scheduled presses from sdmc:/switch/cvhd/pad.txt, one per line: "<seconds>:<BUTTON>[+BUTTON][:<hold ms>]"
// (START BACK A B X Y LB RB LT RT UP DOWN LEFT RIGHT LS RS, the sticks LSUP LSDOWN LSLEFT LSRIGHT RSUP
// RSDOWN RSLEFT RSRIGHT; the hold defaults to 200 ms), seconds
// from the driver's start. Test automation in Eden and on the console: the keyboard in Eden needs
// focus, this does not. A "#" starts a comment.
void NXInputDriver::LoadSchedule() {
  schedule_loaded_ = true;
  t0_tick_ = armGetSystemTick();
  FILE* fp = std::fopen("sdmc:/switch/cvhd/pad.txt", "r");
  if (!fp) return;
  struct Name {
    const char* name;
    uint16_t bit;
  };
  static const Name names[] = {{"START", X_INPUT_GAMEPAD_START}, {"BACK", X_INPUT_GAMEPAD_BACK},
                               {"A", X_INPUT_GAMEPAD_A},         {"B", X_INPUT_GAMEPAD_B},
                               {"X", X_INPUT_GAMEPAD_X},         {"Y", X_INPUT_GAMEPAD_Y},
                               {"LB", X_INPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", X_INPUT_GAMEPAD_RIGHT_SHOULDER},
                               {"UP", X_INPUT_GAMEPAD_DPAD_UP},  {"DOWN", X_INPUT_GAMEPAD_DPAD_DOWN},
                               {"LEFT", X_INPUT_GAMEPAD_DPAD_LEFT}, {"RIGHT", X_INPUT_GAMEPAD_DPAD_RIGHT},
                               {"LS", X_INPUT_GAMEPAD_LEFT_THUMB}, {"RS", X_INPUT_GAMEPAD_RIGHT_THUMB}};
  char line[128];
  while (std::fgets(line, sizeof(line), fp)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    char* p = line;
    double seconds = std::strtod(p, &p);
    if (*p != ':') continue;
    p++;
    Scheduled s{};
    s.start_tick = t0_tick_ + armNsToTicks(uint64_t(seconds * 1e9));
    uint32_t hold_ms = 200;
    char* buttons = p;
    char* colon = std::strchr(p, ':');
    if (colon) {
      *colon = 0;
      hold_ms = uint32_t(std::strtoul(colon + 1, nullptr, 10));
    }
    for (char* tok = std::strtok(buttons, "+ \r\n"); tok; tok = std::strtok(nullptr, "+ \r\n")) {
      for (const Name& n : names) {
        if (std::strcmp(tok, n.name) == 0) s.buttons |= n.bit;
      }
      if (std::strcmp(tok, "LT") == 0) s.left_trigger = true;
      if (std::strcmp(tok, "RT") == 0) s.right_trigger = true;
      if (std::strcmp(tok, "LSUP") == 0) s.ly = 32767;
      if (std::strcmp(tok, "LSDOWN") == 0) s.ly = -32767;
      if (std::strcmp(tok, "LSLEFT") == 0) s.lx = -32767;
      if (std::strcmp(tok, "LSRIGHT") == 0) s.lx = 32767;
      if (std::strcmp(tok, "RSUP") == 0) s.ry = 32767;
      if (std::strcmp(tok, "RSDOWN") == 0) s.ry = -32767;
      if (std::strcmp(tok, "RSLEFT") == 0) s.rx = -32767;
      if (std::strcmp(tok, "RSRIGHT") == 0) s.rx = 32767;
    }
    s.end_tick = s.start_tick + armNsToTicks(uint64_t(hold_ms) * 1000000ull);
    schedule_.push_back(s);
  }
  std::fclose(fp);
  std::printf("[nx] pad: %zu scheduled presses from pad.txt\n", schedule_.size());
  std::fflush(stdout);
}

X_RESULT NXInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (id != kPad) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!schedule_loaded_) LoadSchedule();
  padUpdate(&pad_);
  // How often the game polls the pad (every poll reads libnx's current HID state: no queue, no extra
  // latency on this side): "[nx] pad: N polls/s" once a second.
  {
    static uint64_t window_start = 0;
    static uint32_t polls = 0;
    const uint64_t now = armGetSystemTick();
    polls++;
    if (!window_start) window_start = now;
    if (now - window_start >= armNsToTicks(1000000000ull)) {
      std::printf("[nx] pad: %u polls/s\n", polls);
      window_start = now;
      polls = 0;
    }
  }
  uint64_t held = padGetButtons(&pad_);
  uint16_t scheduled = 0;
  bool scheduled_lt = false, scheduled_rt = false;
  int16_t slx = 0, sly = 0, srx = 0, sry = 0;
  if (!schedule_.empty()) {
    const uint64_t now = armGetSystemTick();
    for (Scheduled& s : schedule_) {
      if (now >= s.start_tick && now < s.end_tick) {
        scheduled |= s.buttons;
        scheduled_lt |= s.left_trigger;
        scheduled_rt |= s.right_trigger;
        if (s.lx) slx = s.lx;
        if (s.ly) sly = s.ly;
        if (s.rx) srx = s.rx;
        if (s.ry) sry = s.ry;
        if (!s.logged) {
          s.logged = true;
          std::printf("[nx] pad: scheduled press buttons 0x%04X at %llu s\n", s.buttons,
                      (unsigned long long)(armTicksToNs(now - t0_tick_) / 1000000000ull));
          std::fflush(stdout);
        }
      }
    }
    if (scheduled || scheduled_lt || scheduled_rt || slx || sly || srx || sry) held ^= 0x8000000000000000ull;  // a change: a new packet
  }
  const HidAnalogStickState left = padGetStickPos(&pad_, 0);
  const HidAnalogStickState right = padGetStickPos(&pad_, 1);
  if (out_state) {
    std::memset(out_state, 0, sizeof(*out_state));
    uint16_t buttons = 0;
    for (const ButtonMap& m : kButtons) {
      if (held & m.nx) buttons |= m.x360;
    }
    out_state->gamepad.buttons = buttons | scheduled;
    out_state->gamepad.left_trigger = (held & HidNpadButton_ZL) || scheduled_lt ? 0xFF : 0;
    out_state->gamepad.right_trigger = (held & HidNpadButton_ZR) || scheduled_rt ? 0xFF : 0;
    out_state->gamepad.thumb_lx = slx ? slx : ClampStick(left.x);
    out_state->gamepad.thumb_ly = sly ? sly : ClampStick(left.y);
    out_state->gamepad.thumb_rx = srx ? srx : ClampStick(right.x);
    out_state->gamepad.thumb_ry = sry ? sry : ClampStick(right.y);
    if (held != last_buttons_) {
      packet_number_++;
      last_buttons_ = held;
    }
    out_state->packet_number = packet_number_;
    // The settings menu (nx_menu.cpp): minus opens it; while it is open the game sees an idle pad.
    if (rex_nx_menu_input(out_state->gamepad.buttons, out_state->gamepad.thumb_lx, out_state->gamepad.thumb_ly)) {
      std::memset(&out_state->gamepad, 0, sizeof(out_state->gamepad));
    }
  }
  return X_ERROR_SUCCESS;
}

X_RESULT NXInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  if (id != kPad) return X_ERROR_DEVICE_NOT_CONNECTED;
  if (!vibration || !rumble_enabled_ || !rex_nx_setting_rumble()) return X_ERROR_SUCCESS;
  const uint16_t left = vibration->left_motor_speed, right = vibration->right_motor_speed;
  // The 360's left motor is the heavy low-frequency one, the right the light high-frequency one; the
  // Switch's actuators take an amplitude per frequency band. Both Joy-Cons get the same values.
  HidVibrationValue v{};
  v.amp_low = left / 65535.0f;
  v.freq_low = 160.0f;
  v.amp_high = right / 65535.0f;
  v.freq_high = 320.0f;
  HidVibrationValue values[2] = {v, v};
  const int set = padIsHandheld(&pad_) ? 0 : 1;
  if (vib_ok_[set]) hidSendVibrationValues(vib_[set], values, 2);
  if (++rumble_calls_ <= 5 || rumble_calls_ % 500 == 0) {
    std::printf("[nx] rumble #%u: left %u right %u (%s)\n", rumble_calls_, left, right, set ? "No1" : "handheld");
    std::fflush(stdout);
  }
  return X_ERROR_SUCCESS;
}

X_RESULT NXInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
  (void)flags;
  (void)out_keystroke;
  return id == kPad ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace rex::input::nx
