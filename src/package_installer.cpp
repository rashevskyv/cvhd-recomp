// Installs Xbox 360 packages that the user dropped into the "install" folder, at start, before the XEX loads:
//   DLC (STFS, content type 2)  -> <user>/0000000000000000/<title>/00000002/<name>/ + Headers/00000002/<name>.header
//   title update (type 0xB0000) -> every file into <update>/, default.xexp into <game>/, *.dllp into <game>/data/player_dll/
// A package that installed moves to install/done/. Same layout and folder names as tools/dlc_install.py.
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/xam/content_manager.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// tools/dlc_install.py folder_name(): runs of non-alphanumerics become "_", trimmed, at most 40 characters.
std::string FolderName(const std::u16string& display, std::set<std::string>& used) {
  std::string base;
  bool gap = false;
  for (char16_t c : display) {
    const bool alnum = (c < 0x80) && std::isalnum(static_cast<unsigned char>(c));
    if (alnum) {
      if (gap && !base.empty()) base += '_';
      base += static_cast<char>(c);
      gap = false;
    } else {
      gap = true;
    }
  }
  if (base.size() > 40) base.resize(40);
  while (!base.empty() && base.back() == '_') base.pop_back();
  if (base.empty()) base = "dlc";
  std::string name = base;
  for (int i = 2; used.count(name); ++i) name = base.substr(0, 38) + "_" + std::to_string(i);
  used.insert(name);
  return name;
}

bool CopyOut(rex::filesystem::Entry* entry, const fs::path& dst) {
  std::error_code ec;
  fs::create_directories(dst.parent_path(), ec);
  rex::filesystem::File* in = nullptr;
  if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in) != 0) return false;  // X_STATUS_SUCCESS
  FILE* out = std::fopen(dst.string().c_str(), "wb");
  if (!out) {
    in->Destroy();
    return false;
  }
  std::vector<uint8_t> buf(1 << 20);
  size_t offset = 0;
  for (;;) {
    size_t got = 0;
    in->ReadSync(std::span<uint8_t>(buf.data(), buf.size()), offset, &got);
    if (got == 0) break;
    std::fwrite(buf.data(), 1, got, out);
    offset += got;
  }
  std::fclose(out);
  in->Destroy();
  return true;
}

// Every file of the package, with its path inside the package ("data/sound/x.xwb").
void ForEachFile(rex::filesystem::Entry* dir, const std::function<void(rex::filesystem::Entry*, const std::string&)>& fn,
                 const std::string& prefix = "") {
  for (const auto& child : dir->children()) {
    const std::string rel = prefix.empty() ? child->name() : prefix + "/" + child->name();
    if (child->attributes() & rex::filesystem::kFileAttributeDirectory)
      ForEachFile(child.get(), fn, rel);
    else
      fn(child.get(), rel);
  }
}

}  // namespace

void CvhdInstallDroppedPackages(const fs::path& install_dir, const fs::path& game, const fs::path& user,
                                const fs::path& update) {
  std::error_code ec;
  if (!fs::is_directory(install_dir, ec)) return;
  std::set<std::string> used;
  std::vector<fs::path> packages;
  for (const auto& e : fs::directory_iterator(install_dir, ec))
    if (e.is_regular_file(ec)) packages.push_back(e.path());
  for (const fs::path& pkg : packages) {
    rex::filesystem::StfsContainerDevice device("", pkg);
    if (!device.Initialize()) {
      std::printf("[install] %s: not an STFS package, left in place\n", pkg.filename().string().c_str());
      continue;
    }
    const auto& meta = device.header().metadata;
    const uint32_t type = static_cast<uint32_t>(meta.content_type.get());
    const uint32_t title = meta.execution_info.title_id;
    auto* root = device.ResolvePath("");
    if (!root || title != 0x58410A7A || (type != 0x2 && type != 0xB0000)) {
      std::printf("[install] %s: title %08X type %X is not Castlevania HD DLC or title update, left in place\n",
                  pkg.filename().string().c_str(), title, type);
      continue;
    }
    bool ok = true;
    size_t count = 0;
    std::string what;
    if (type == 0x2) {
      const std::u16string display = meta.display_name(rex::system::XLanguage::kEnglish);
      const std::string name = FolderName(display.empty() ? std::u16string(u"dlc") : display, used);
      const fs::path base = user / "0000000000000000" / "58410A7A";
      ForEachFile(root, [&](rex::filesystem::Entry* f, const std::string& rel) {
        ok &= CopyOut(f, base / "00000002" / name / rel);
        ++count;
      });
      // The SDK's XCONTENT_AGGREGATE_DATA + the licence mask (ContentPackage::LoadPackageLicenseMask reads it).
      rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
      data.device_id = 1;  // DummyDeviceId::HDD
      data.content_type = rex::system::XContentType::kMarketplaceContent;
      data.set_display_name(display);
      data.set_file_name(name);
      data.xuid = 0;
      data.title_id = title;
      const fs::path header = base / "Headers" / "00000002" / (name + ".header");
      fs::create_directories(header.parent_path(), ec);
      if (FILE* h = std::fopen(header.string().c_str(), "wb")) {
        const uint32_t licence = 0xFFFFFFFFu;
        std::fwrite(&data, 1, sizeof(data), h);
        std::fwrite(&licence, 1, sizeof(licence), h);
        std::fclose(h);
      } else {
        ok = false;
      }
      what = "DLC " + name;
    } else {
      ForEachFile(root, [&](rex::filesystem::Entry* f, const std::string& rel) {
        ok &= CopyOut(f, update / rel);
        if (rel == "default.xexp") ok &= CopyOut(f, game / rel);
        if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".dllp") == 0) ok &= CopyOut(f, game / rel);
        ++count;
      });
      what = "title update";
    }
    if (!ok) {
      std::printf("[install] %s: %s FAILED (write error), package left in place\n", pkg.filename().string().c_str(),
                  what.c_str());
      continue;
    }
    fs::create_directories(install_dir / "done", ec);
    fs::rename(pkg, install_dir / "done" / pkg.filename(), ec);
    std::printf("[install] %s: %s, %zu files%s\n", pkg.filename().string().c_str(), what.c_str(), count,
                ec ? " (could not move it to done/)" : ", package moved to done/");
  }
  std::fflush(stdout);
}
