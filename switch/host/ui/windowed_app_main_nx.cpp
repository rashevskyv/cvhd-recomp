// main() for the Switch build: the SDK's windowed_app_main_sdl.cpp without SDL. The game data and
// user data live in sdmc:/switch/cvhd/.
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <switch.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window_nx.h>
#include <rex/ui/windowed_app.h>

int main(int argc, char* argv[]) {
  // Defaults for the console; a cvhd.toml next to the NRO or arguments from hbmenu override them.
  std::vector<std::string> args(argv, argv + argc);
  const char* defaults[] = {
      "--game_data_root=sdmc:/switch/cvhd/game",
      "--user_data_root=sdmc:/switch/cvhd/user",
      "--gpu_plugin=xenos",
      "--fullscreen=true",
      "--log_file=sdmc:/switch/cvhd/cvhd_rex_log.txt",
  };
  for (const char* d : defaults) {
    std::string key(d, std::string(d).find('='));
    bool given = false;
    for (const auto& a : args) {
      if (a.compare(0, key.size(), key) == 0) given = true;
    }
    if (!given) args.push_back(d);
  }
  std::vector<char*> argv_ptrs;
  for (auto& a : args) argv_ptrs.push_back(a.data());

  auto remaining = rex::cvar::Init(int(argv_ptrs.size()), argv_ptrs.data());
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  int result;
  {
    rex::ui::NXWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }
    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));
    result = app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;
    app->InvokeOnDestroy();
  }
  return result;
}
