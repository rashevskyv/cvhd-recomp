// cvhd - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#if !defined(__SWITCH__)
#include <filesystem>
#include <rex/cvar.h>
REXCVAR_DECLARE(uint32_t, license_mask);
REXCVAR_DECLARE(std::string, gpu_plugin);
REXCVAR_DECLARE(bool, mnk_mode);
#endif

class CvhdApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<CvhdApp>(new CvhdApp(ctx, "cvhd",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
#if !defined(__SWITCH__)
  // PC defaults (as the Guardian Heroes port; the Switch build injects them in windowed_app_main_nx.cpp).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const std::filesystem::path dir = paths.config_path.parent_path();
    // No --game_data_root: the "game" folder next to the executable (default.xex + data/ inside).
    if (paths.game_data_root.empty() && std::filesystem::exists(dir / "game" / "default.xex"))
      paths.game_data_root = dir / "game";
    // The full game, not the XBLA trial; a config file or --license_mask still wins.
    if (REXCVAR_GET(license_mask) == 0) REXCVAR_SET(license_mask, 0xFFFFFFFFu);
    if (REXCVAR_GET(gpu_plugin).empty()) REXCVAR_SET(gpu_plugin, "xenos");
    // First start (no cvhd.toml yet): the keyboard plays as pad 1.
    if (!std::filesystem::exists(paths.config_path)) REXCVAR_SET(mnk_mode, true);
  }
#endif
};
