#!/usr/bin/env python3
"""Edits the ReXGlue SDK checkout for the Nintendo Switch build (devkitA64 GCC, libnx, static NVK).

    python3 switch/sdk-edits.py /root/cvhd/rexglue-sdk

Idempotent: every edit checks for its own marker first. Run it on the SDK with stuntdouble's
patches/sdk/*.patch already applied; `git -C <sdk> diff > patches/sdk-switch/0001-nintendo-switch.patch`
records the result. The host-side sources (memory, fibers, window, surface, exceptions) live in
switch/host/ of this repo and are handed to the SDK through REXGLUE_SWITCH_* CMake variables.
"""
import os, re, sys

SDK = sys.argv[1]
changed = []
NL = chr(10)


def edit(rel, pairs, must=True):
    path = os.path.join(SDK, rel)
    s = open(path, encoding='utf-8').read()
    orig = s
    for old, new in pairs:
        if new in s:
            continue
        if old not in s:
            if must:
                raise SystemExit(f'{rel}: anchor not found:\n{old}')
            continue
        s = s.replace(old, new, 1)
    if s != orig:
        open(path, 'w', encoding='utf-8', newline='\n').write(s)
        changed.append(rel)


def write(rel, text):
    path = os.path.join(SDK, rel)
    if os.path.exists(path) and open(path, encoding='utf-8').read() == text:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w', encoding='utf-8', newline='\n').write(text)
    changed.append(rel)


# --- CMake: toolchain, platform, library types -------------------------------------------------
edit('CMakeLists.txt', [
    ('if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")',
     'if(NOT NINTENDO_SWITCH AND NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")'),
    ('    -ffp-model=strict\n',
     '    # GCC (devkitA64) has no -ffp-model; keep mul+add unfused for PPC parity.\n'
     '    $<$<NOT:$<BOOL:${NINTENDO_SWITCH}>>:-ffp-model=strict>\n'
     '    $<$<BOOL:${NINTENDO_SWITCH}>:-ffp-contract=off>\n'),
    ('if(WIN32)\n    if(REX_TARGET_PROCESSOR MATCHES "AMD64|x86_64")\n        set(REX_PLATFORM "win-amd64")',
     'if(NINTENDO_SWITCH)\n    set(REX_PLATFORM "switch-arm64")\n'
     '    add_compile_definitions(REX_PLATFORM_SWITCH=1 REX_PLATFORM_LINUX=1)\n'
     'elseif(WIN32)\n    if(REX_TARGET_PROCESSOR MATCHES "AMD64|x86_64")\n        set(REX_PLATFORM "win-amd64")'),
    ('add_subdirectory(src/codegen)\nadd_subdirectory(src/rexglue)\n',
     'if(NOT NINTENDO_SWITCH)\n    # Host tools only.\n    add_subdirectory(src/codegen)\n    add_subdirectory(src/rexglue)\nendif()\n'),
])

edit('thirdparty/CMakeLists.txt', [
    ('add_subdirectory(sdl3)\n',
     'if(NINTENDO_SWITCH)\n    # No SDL3 for libnx: the Switch host layer provides the window, input and audio.\n'
     '    add_library(SDL3 INTERFACE)\n    add_library(SDL3::SDL3 ALIAS SDL3)\nelse()\n    add_subdirectory(sdl3)\nendif()\n'),
])

edit('src/core/CMakeLists.txt', [
    ('elseif(UNIX)\n    # Linux and macOS/Darwin share the POSIX sources;',
     'elseif(UNIX OR NINTENDO_SWITCH)\n    # Linux and macOS/Darwin share the POSIX sources;'),
    ('# Xenos data-format layer (stateless; headers under include/rex/graphics/).',
     'if(NINTENDO_SWITCH)\n'
     '    # Horizon: no mmap, no signals, no ucontext. The replacements come from the port.\n'
     '    get_target_property(_rexcore_sources rexcore SOURCES)\n'
     '    list(FILTER _rexcore_sources EXCLUDE REGEX\n'
     '        "(exception_handler_posix|fiber_posix|mapped_memory_posix|memory_posix|seh_posix)\\\\.cpp$")\n'
     '    set_property(TARGET rexcore PROPERTY SOURCES ${_rexcore_sources})\n'
     '    target_sources(rexcore PRIVATE ${REXGLUE_SWITCH_CORE_SOURCES})\n'
     '    target_include_directories(rexcore PUBLIC ${REXGLUE_SWITCH_INCLUDE_DIR})\n'
     'endif()\n\n'
     '# Xenos data-format layer (stateless; headers under include/rex/graphics/).'),
    ('elseif(UNIX)\n    target_link_libraries(rexcore PRIVATE pthread rt dl)',
     'elseif(UNIX AND NOT NINTENDO_SWITCH)\n    target_link_libraries(rexcore PRIVATE pthread rt dl)'),
])

edit('src/system/CMakeLists.txt', [
    ('add_library(rexruntime SHARED ${REXSYSTEM_SOURCES})',
     'if(NINTENDO_SWITCH)\n    set(REXGLUE_RUNTIME_LIB_TYPE STATIC)\nelse()\n    set(REXGLUE_RUNTIME_LIB_TYPE SHARED)\nendif()\n'
     'add_library(rexruntime ${REXGLUE_RUNTIME_LIB_TYPE} ${REXSYSTEM_SOURCES})'),
])

edit('src/graphics/CMakeLists.txt', [
    ('add_library(rexgpu-xenos SHARED ${REXGPU_XENOS_SOURCES})',
     'if(NINTENDO_SWITCH)\n    set(REXGLUE_GPU_PLUGIN_LIB_TYPE STATIC)\nelse()\n    set(REXGLUE_GPU_PLUGIN_LIB_TYPE SHARED)\nendif()\n'
     'add_library(rexgpu-xenos ${REXGLUE_GPU_PLUGIN_LIB_TYPE} ${REXGPU_XENOS_SOURCES})'),
])

edit('src/ui/CMakeLists.txt', [
    ('else()\n    set(REXUI_PLATFORM_SOURCES\n        surface_gnulinux.cpp\n    )\nendif()',
     'elseif(NINTENDO_SWITCH)\n    set(REXUI_PLATFORM_SOURCES ${REXGLUE_SWITCH_UI_SOURCES})\n'
     'else()\n    set(REXUI_PLATFORM_SOURCES\n        surface_gnulinux.cpp\n    )\nendif()'),
    ('add_library(rexui OBJECT ${REXUI_SOURCES})',
     'if(NINTENDO_SWITCH)\n    list(FILTER REXUI_SOURCES EXCLUDE REGEX\n'
     '        "(window_sdl|windowed_app_context_sdl|sdl_virtual_key)\\\\.cpp$")\nendif()\n'
     'add_library(rexui OBJECT ${REXUI_SOURCES})'),
    ('if(UNIX AND NOT APPLE)\n    find_package(PkgConfig REQUIRED)',
     'if(UNIX AND NOT APPLE AND NOT NINTENDO_SWITCH)\n    find_package(PkgConfig REQUIRED)'),
])

edit('src/input/CMakeLists.txt', [
    ('else()\n    target_sources(rexinput PRIVATE sdl/sdl_input_driver.cpp)\nendif()',
     'elseif(NOT NINTENDO_SWITCH)\n    target_sources(rexinput PRIVATE sdl/sdl_input_driver.cpp)\nendif()'),
])

edit('src/audio/CMakeLists.txt', [
    ('    # SDL backend\n    sdl/sdl_audio_system.cpp\n    sdl/sdl_audio_driver.cpp\n)',
     ')\nif(NOT NINTENDO_SWITCH)\n    target_sources(rexaudio PRIVATE sdl/sdl_audio_system.cpp sdl/sdl_audio_driver.cpp)\nendif()'),
])

edit('cmake/rexglue_helpers.cmake', [
    ('    target_sources(${target_name} PRIVATE\n        ${REXGLUE_SHARE_DIR}/windowed_app_main_sdl.cpp\n        ${REXGLUE_SHARE_DIR}/rex_app.cpp)',
     '    if(NINTENDO_SWITCH)\n        target_sources(${target_name} PRIVATE\n            ${REXGLUE_SWITCH_MAIN_SOURCE}\n'
     '            ${REXGLUE_SHARE_DIR}/rex_app.cpp)\n    else()\n        target_sources(${target_name} PRIVATE\n'
     '            ${REXGLUE_SHARE_DIR}/windowed_app_main_sdl.cpp\n            ${REXGLUE_SHARE_DIR}/rex_app.cpp)\n    endif()'),
    ('        add_custom_command(TARGET ${target_name} POST_BUILD\n            COMMAND ${CMAKE_COMMAND} -E copy_if_different\n'
     '                $<TARGET_FILE:${_plugin_target}>\n                $<TARGET_FILE_DIR:${target_name}>\n            VERBATIM\n        )\n        unset(_plugin_target)',
     '        if(NINTENDO_SWITCH)\n            # Static plugin: linked in, created by gpu_plugin_loader.cpp directly.\n'
     '            target_link_libraries(${target_name} PRIVATE ${_plugin_target})\n        else()\n'
     '        add_custom_command(TARGET ${target_name} POST_BUILD\n            COMMAND ${CMAKE_COMMAND} -E copy_if_different\n'
     '                $<TARGET_FILE:${_plugin_target}>\n                $<TARGET_FILE_DIR:${target_name}>\n            VERBATIM\n        )\n'
     '        endif()\n        unset(_plugin_target)'),
])

# --- Headers --------------------------------------------------------------------------------------
edit('include/rex/platform.h', [
    ('#elif defined(__gnu_linux__)\n#define REX_PLATFORM_GNU_LINUX 1\n#define REX_PLATFORM_LINUX 1\n',
     '#elif defined(__SWITCH__)\n// Nintendo Switch (libnx, newlib): POSIX-like, not GNU/Linux.\n#define REX_PLATFORM_SWITCH 1\n'
     '#define REX_PLATFORM_LINUX 1\n#elif defined(__gnu_linux__)\n#define REX_PLATFORM_GNU_LINUX 1\n#define REX_PLATFORM_LINUX 1\n'),
    ('#ifndef REX_PLATFORM_GNU_LINUX\n#define REX_PLATFORM_GNU_LINUX 0\n#endif\n',
     '#ifndef REX_PLATFORM_GNU_LINUX\n#define REX_PLATFORM_GNU_LINUX 0\n#endif\n#ifndef REX_PLATFORM_SWITCH\n#define REX_PLATFORM_SWITCH 0\n#endif\n'),
])

edit('include/rex/thread/fiber.h', [
    ('#include <ucontext.h>\n', '#if !REX_PLATFORM_SWITCH\n#include <ucontext.h>\n#endif\n'),
    ('  ucontext_t context_{};\n',
     '#if REX_PLATFORM_SWITCH\n  // sp, lr, x19-x29, d8-d15: fiber_nx.cpp.\n  uint64_t context_[24] = {};\n#else\n  ucontext_t context_{};\n#endif\n'),
])

edit('include/rex/ui/vulkan/api.h', [
    ('#if REX_PLATFORM_GNU_LINUX\n#ifndef VK_USE_PLATFORM_XCB_KHR',
     '#if REX_PLATFORM_SWITCH\n#ifndef VK_USE_PLATFORM_VI_NN\n#define VK_USE_PLATFORM_VI_NN\n#endif\n#endif\n\n'
     '#if REX_PLATFORM_GNU_LINUX\n#ifndef VK_USE_PLATFORM_XCB_KHR'),
])

edit('include/rex/ui/vulkan/instance.h', [
    ('    bool ext_KHR_xcb_surface = false;  // #6' + NL + '    bool ext_NN_vi_surface = false;    // #63, Nintendo Switch' + NL + '#endif' + NL,
     '    bool ext_KHR_xcb_surface = false;  // #6' + NL + '#endif' + NL + '#ifdef VK_USE_PLATFORM_VI_NN' + NL + '    bool ext_NN_vi_surface = false;  // #63, Nintendo Switch' + NL + '#endif' + NL),
    ('    bool ext_KHR_xcb_surface = false;  // #6' + NL + '#endif' + NL,
     '    bool ext_KHR_xcb_surface = false;  // #6' + NL + '#endif' + NL + '#ifdef VK_USE_PLATFORM_VI_NN' + NL + '    bool ext_NN_vi_surface = false;  // #63, Nintendo Switch' + NL + '#endif' + NL),
    ('#ifdef VK_USE_PLATFORM_XCB_KHR\n#include <rex/ui/vulkan/functions/instance_khr_xcb_surface.inc>\n#endif\n',
     '#ifdef VK_USE_PLATFORM_XCB_KHR\n#include <rex/ui/vulkan/functions/instance_khr_xcb_surface.inc>\n#endif\n'
     '#ifdef VK_USE_PLATFORM_VI_NN\n#include <rex/ui/vulkan/functions/instance_nn_vi_surface.inc>\n#endif\n'),
])
write('include/rex/ui/vulkan/functions/instance_nn_vi_surface.inc', 'XE_UI_VULKAN_FUNCTION(vkCreateViSurfaceNN)\n')

edit('include/rex/ui/surface.h', [
    ('    // macOS — CAMetalLayer presented via MoltenVK (VK_EXT_metal_surface).\n    kTypeIndex_CAMetalLayer,\n',
     '    // macOS — CAMetalLayer presented via MoltenVK (VK_EXT_metal_surface).\n    kTypeIndex_CAMetalLayer,\n'
     '    // Nintendo Switch (VK_NN_vi_surface on an nwindow).\n    kTypeIndex_NnVi,\n'),
    ('    kTypeFlag_CAMetalLayer = TypeFlags(1) << kTypeIndex_CAMetalLayer,\n',
     '    kTypeFlag_CAMetalLayer = TypeFlags(1) << kTypeIndex_CAMetalLayer,\n    kTypeFlag_NnVi = TypeFlags(1) << kTypeIndex_NnVi,\n'),
])

# renderdoc_app.h (vendored): no platform branch for the Switch.
edit('thirdparty/renderdoc/renderdoc_app.h', [
    ('#elif defined(__linux__)', '#elif defined(__linux__) || defined(__SWITCH__)'),
])

# --- Sources ----------------------------------------------------------------------------------------
edit('src/ui/vulkan/vulkan_instance.cpp', [
    ('#ifdef VK_USE_PLATFORM_XCB_KHR\n    // #6.\n    requested_extensions.emplace("VK_KHR_xcb_surface",\n                                 &vulkan_instance->extensions_.ext_KHR_xcb_surface);\n#endif\n',
     '#ifdef VK_USE_PLATFORM_XCB_KHR\n    // #6.\n    requested_extensions.emplace("VK_KHR_xcb_surface",\n                                 &vulkan_instance->extensions_.ext_KHR_xcb_surface);\n#endif\n'
     '#ifdef VK_USE_PLATFORM_VI_NN\n    // #63.\n    requested_extensions.emplace("VK_NN_vi_surface",\n                                 &vulkan_instance->extensions_.ext_NN_vi_surface);\n#endif\n'),
    ('#else\n  loader_loaded = vulkan_instance->loader_.Load(platform::lib_names::kVulkanLoader);\n  if (!loader_loaded) {\n    REXLOG_ERROR("Failed to load {}", platform::lib_names::kVulkanLoader);\n    return nullptr;\n  }\n#endif\n',
     '#elif REX_PLATFORM_SWITCH\n  // No loader: the driver (mesa-switch NVK, libvulkan.a) is linked in.\n  loader_loaded = true;\n#else\n'
     '  loader_loaded = vulkan_instance->loader_.Load(platform::lib_names::kVulkanLoader);\n  if (!loader_loaded) {\n    REXLOG_ERROR("Failed to load {}", platform::lib_names::kVulkanLoader);\n    return nullptr;\n  }\n#endif\n'),
    ('#define XE_VULKAN_LOAD_LOADER_FUNCTION(name) \\\n  functions_loaded &= (ifn.name = vulkan_instance->loader_.GetSymbol<PFN_##name>(#name)) != nullptr;\n  XE_VULKAN_LOAD_LOADER_FUNCTION(vkGetInstanceProcAddr);\n  XE_VULKAN_LOAD_LOADER_FUNCTION(vkDestroyInstance);\n#undef XE_VULKAN_LOAD_LOADER_FUNCTION\n',
     '#if REX_PLATFORM_SWITCH\n  ifn.vkGetInstanceProcAddr = vk_icdGetInstanceProcAddr;\n'
     '  ifn.vkDestroyInstance = PFN_vkDestroyInstance(vk_icdGetInstanceProcAddr(nullptr, "vkDestroyInstance"));\n'
     '  functions_loaded &= ifn.vkGetInstanceProcAddr != nullptr;\n#else\n'
     '#define XE_VULKAN_LOAD_LOADER_FUNCTION(name) \\\n  functions_loaded &= (ifn.name = vulkan_instance->loader_.GetSymbol<PFN_##name>(#name)) != nullptr;\n  XE_VULKAN_LOAD_LOADER_FUNCTION(vkGetInstanceProcAddr);\n  XE_VULKAN_LOAD_LOADER_FUNCTION(vkDestroyInstance);\n#undef XE_VULKAN_LOAD_LOADER_FUNCTION\n#endif\n'),
    ('#include <rex/ui/vulkan/instance.h>\n',
     '#include <rex/ui/vulkan/instance.h>\n#if REX_PLATFORM_SWITCH\nextern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);\n#endif\n'),
])

edit('src/ui/vulkan/vulkan_presenter.cpp', [
    ('    type_flags |= Surface::kTypeFlag_XcbWindow;\n',
     '    type_flags |= Surface::kTypeFlag_XcbWindow;\n  }\n#endif\n#ifdef VK_USE_PLATFORM_VI_NN\n  if (instance_extensions.ext_NN_vi_surface) {\n    type_flags |= Surface::kTypeFlag_NnVi;\n'),
    ('#if REX_PLATFORM_MAC\n      case Surface::kTypeIndex_CAMetalLayer: {',
     '#if REX_PLATFORM_SWITCH\n      case Surface::kTypeIndex_NnVi: {\n'
     '        auto& vi_surface = static_cast<const NnViSurface&>(new_surface);\n'
     '        VkViSurfaceCreateInfoNN surface_create_info;\n'
     '        surface_create_info.sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN;\n'
     '        surface_create_info.pNext = nullptr;\n        surface_create_info.flags = 0;\n'
     '        surface_create_info.window = vi_surface.window();\n'
     '        vulkan_surface_create_result = ifn.vkCreateViSurfaceNN(\n'
     '            instance, &surface_create_info, nullptr, &paint_context_.vulkan_surface);\n      } break;\n#endif\n'
     '#if REX_PLATFORM_MAC\n      case Surface::kTypeIndex_CAMetalLayer: {'),
])

edit('src/ui/vulkan/vulkan_presenter.cpp', [
    ('#include <rex/ui/vulkan/presenter.h>' + NL,
     '#include <rex/ui/vulkan/presenter.h>' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     '#include <rex/ui/surface_nx.h>' + NL + '#endif' + NL),
])

edit('src/system/gpu_plugin_loader.cpp', [
    ('std::unique_ptr<IGraphicsSystem> LoadGpuPlugin(std::string_view name, std::string_view backend) {\n',
     '#if REX_PLATFORM_SWITCH\n// The plugin is a static library (no dlopen on Horizon): call its factory directly.\n'
     'extern "C" uint32_t rex_gpu_abi_version(void);\n'
     'extern "C" rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,\n'
     '                                                      const rex::system::GpuCreateInfo* info);\n#endif\n\n'
     'std::unique_ptr<IGraphicsSystem> LoadGpuPlugin(std::string_view name, std::string_view backend) {\n'
     '#if REX_PLATFORM_SWITCH\n  std::string nx_backend(backend);\n  GpuCreateInfo nx_info{};\n  nx_info.struct_size = sizeof(GpuCreateInfo);\n'
     '  nx_info.backend = nx_backend.c_str();\n  IGraphicsSystem* nx_system = rex_gpu_create(kGpuPluginAbiVersion, &nx_info);\n'
     '  if (!nx_system) {\n    REXSYS_ERROR("GPU plugin \'{}\' (static) returned no graphics system (backend \'{}\')", name,\n'
     '                 nx_backend);\n    return nullptr;\n  }\n  return std::unique_ptr<IGraphicsSystem>(nx_system);\n#endif\n'),
])

edit('src/input/input_system.cpp', [
    ('REXCVAR_DEFINE_STRING(input_backend, "sdl", "Input", "Input backend: sdl, xinput")\n    .allowed({"sdl", "xinput"});',
     '#if REX_PLATFORM_SWITCH\nREXCVAR_DEFINE_STRING(input_backend, "nop", "Input", "Input backend: nop")\n    .allowed({"nop"});\n#else\n'
     'REXCVAR_DEFINE_STRING(input_backend, "sdl", "Input", "Input backend: sdl, xinput")\n    .allowed({"sdl", "xinput"});\n#endif'),
    ('    if (REXCVAR_GET(input_backend) == "sdl") {\n      auto sdl_driver',
     '#if !REX_PLATFORM_SWITCH\n    if (REXCVAR_GET(input_backend) == "sdl") {\n      auto sdl_driver'),
])
# Close the #if after the sdl block: the block ends before the MnK driver comment.
edit('src/input/input_system.cpp', [
    ('    // MnK driver (keyboard/mouse -> controller emulation)\n',
     '#endif\n\n    // MnK driver (keyboard/mouse -> controller emulation)\n'),
])

edit('src/ui/rex_app.cpp', [
    ('#include <rex/system/gpu_plugin.h>' + NL,
     '#include <rex/system/gpu_plugin.h>' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     '#include <rex/audio/nop/nop_audio_system.h>' + NL + '#endif' + NL),
    ('  config_.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);',
     '#if REX_PLATFORM_SWITCH\n  config_.audio_factory = REX_AUDIO_BACKEND(rex::audio::nop::NopAudioSystem);\n#else\n'
     '  config_.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);\n#endif'),
])

edit('src/core/filesystem_posix.cpp', [
    ('#define rex_off64_t off64_t\n',
     '#define rex_off64_t off64_t\n#elif REX_PLATFORM_SWITCH\n// newlib: off_t is 64-bit already.\n#define rex_fseeko64 fseeko\n#define rex_ftello64 ftello\n'
     '#define rex_ftruncate64 ftruncate\n#define rex_off64_t off_t\n'),
], must=False)

# FFmpeg: a config for the Switch (the Linux aarch64 one without the glibc-only calls).
cfg = os.path.join(SDK, 'thirdparty/FFmpeg/config_linux_aarch64.h')
sw = os.path.join(SDK, 'thirdparty/FFmpeg/config_switch_aarch64.h')
if os.path.exists(cfg) and not os.path.exists(sw):
    text = open(cfg, encoding='utf-8').read()
    for name in ('HAVE_ARC4RANDOM', 'HAVE_ARC4RANDOM_BUF', 'HAVE_GETAUXVAL', 'HAVE_SCHED_GETAFFINITY', 'HAVE_MMAP',
                 'HAVE_SYSCTL', 'HAVE_PTHREAD_SETNAME_NP', 'HAVE_GETRUSAGE', 'HAVE_MKSTEMP', 'HAVE_DOS_PATHS',
                 'HAVE_ISATTY', 'HAVE_SETMODE', 'HAVE_GETPROCESSMEMORYINFO', 'HAVE_FCNTL', 'HAVE_GLOB', 'HAVE_SYSCONF',
                 'HAVE_STRERROR_R', 'HAVE_SYS_MMAN_H', 'HAVE_SYS_UN_H', 'HAVE_FORK', 'HAVE_MPROTECT', 'HAVE_PTHREAD_CANCEL',
                 'HAVE_SYS_RESOURCE_H', 'HAVE_GETRUSAGE', 'HAVE_SECURE_GETENV'):
        text = re.sub(rf'#define {name} 1', f'#define {name} 0', text)
    open(sw, 'w', encoding='utf-8', newline='\n').write(text)
    changed.append('thirdparty/FFmpeg/config_switch_aarch64.h')
edit('thirdparty/FFmpeg/config.h', [
    ('#elif defined(__gnu_linux__)\n',
     '#elif defined(__SWITCH__)\n#include "config_switch_aarch64.h"\n#elif defined(__gnu_linux__)\n'),
])


# --- Guest memory: one R/W view, the other views folded in software (CLAUDE.md "Switch memory design")
edit('include/rex/system/xmemory.h', [
    ('  inline T TranslateVirtual(uint32_t guest_address) const {\n    uint8_t* host_address = virtual_membase_ + guest_address;\n',
     '  inline T TranslateVirtual(uint32_t guest_address) const {\n'
     '#if REX_PLATFORM_SWITCH\n'
     '    // One R/W view of the physical arena (the 0xA0000000 block) and of the XEX (0x80000000):\n'
     '    // 0xC0000000, 0xE0000000 (+4 KB) and 0x90000000 fold onto them.\n'
     '    if (guest_address >= 0xE0000000u) {\n'
     '      guest_address = 0xA0000000u + (guest_address - 0xE0000000u) + 0x1000u;\n'
     '    } else if (guest_address >= 0xC0000000u) {\n'
     '      guest_address -= 0x20000000u;\n'
     '    } else if (guest_address >= 0x90000000u && guest_address < 0xA0000000u) {\n'
     '      guest_address -= 0x10000000u;\n'
     '    }\n'
     '#endif\n'
     '    uint8_t* host_address = virtual_membase_ + guest_address;\n'),
])

edit('src/system/xmemory.cpp', [
    ('#include <rex/system/mmio_handler.h>\n',
     '#include <rex/system/mmio_handler.h>\n#if REX_PLATFORM_SWITCH\nnamespace rex::memory::nx {\nuint8_t* arena_base();\n}\n#endif\n'),
    ('#else\n  // Attempt to create our views. This may fail at the first address\n  // we pick, so try a few times.\n  mapping_base_ = 0;\n',
     '#elif REX_PLATFORM_SWITCH\n  // The arena was reserved by CreateFileMappingHandle (memory_nx.cpp); the views are blocks in it.\n'
     '  mapping_base_ = rex::memory::nx::arena_base();\n  if (!mapping_base_ || MapViews(mapping_base_)) {\n'
     '    REXSYS_ERROR("Unable to map the guest memory blocks.");\n    assert_always();\n    return false;\n  }\n'
     '#else\n  // Attempt to create our views. This may fail at the first address\n  // we pick, so try a few times.\n  mapping_base_ = 0;\n'),
    ('  physical_membase_ = mapping_base_ + 0x100000000ull;\n',
     '#if REX_PLATFORM_SWITCH\n  // The raw physical view is the 0xA0000000 block.\n  physical_membase_ = mapping_base_ + 0xA0000000ull;\n#else\n'
     '  physical_membase_ = mapping_base_ + 0x100000000ull;\n#endif\n'),
    ('  uint32_t max_base_page_number = high_page_number + 1 - page_count;\n  if (top_down) {\n',
     '  uint32_t max_base_page_number = high_page_number + 1 - page_count;\n'
     '#if REX_PLATFORM_SWITCH\n  // Only the bottom of each virtual range is backed by RAM (memory_nx.cpp blocks).\n'
     '  if (heap_type_ != HeapType::kGuestPhysical) {\n    top_down = false;\n  }\n#endif\n  if (top_down) {\n'),
])

# The codegen pch (embedded in the rexglue CLI: rebuild it and rerun codegen after this edit).
edit('resources/templates/codegen/pch_h.inja', [
    ('#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)\n#define REX_PHYS_HOST_OFFSET(addr) (((u32)(addr) >= 0xE0000000u) ? 0x1000u : 0u)\n#else\n',
     '#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)\n#define REX_PHYS_HOST_OFFSET(addr) (((u32)(addr) >= 0xE0000000u) ? 0x1000u : 0u)\n'
     '#elif defined(__SWITCH__)\n'
     '// One R/W view of the physical arena (0xA0000000) and of the XEX (0x80000000): the 0xC0000000,\n'
     '// 0xE0000000 (+4 KB) and 0x90000000 views fold onto them (switch/host/memory_nx.cpp).\n'
     'static inline s64 rex_nx_host_offset(u32 a) {\n'
     '  return a >= 0xE0000000u ? -0x40000000ll + 0x1000 : a >= 0xC0000000u ? -0x20000000ll\n'
     '       : (a >= 0x90000000u && a < 0xA0000000u) ? -0x10000000ll : 0;\n}\n'
     '#define REX_PHYS_HOST_OFFSET(addr) rex_nx_host_offset((u32)(addr))\n#else\n'),
])

print('changed:', *changed, sep='\n  ') if changed else print('nothing to change')

# --- Added later: SDL include guard, threading without signals (appended by the author) ---------
edit('src/input/input_system.cpp', [
    ('#include <rex/input/sdl/sdl_input_driver.h>' + NL,
     '#if !REX_PLATFORM_SWITCH' + NL + '#include <rex/input/sdl/sdl_input_driver.h>' + NL + '#endif' + NL),
])

# threading_switch.cpp: threading_posix.cpp without signals, robust mutexes and Linux thread ids.
src = open(os.path.join(SDK, 'src/core/threading_posix.cpp'), encoding='utf-8').read()
reps = [
    ('  auto result = SIGRTMIN + static_cast<int>(num);' + NL + '  assert_true(result < SIGRTMAX);' + NL + '  return result;',
     '  (void)num;' + NL + '  return 0;  // Horizon: no signals'),
    ('  return static_cast<SignalType>(num - SIGRTMIN);', '  (void)num;' + NL + '  return SignalType::kThreadSuspend;'),
    ('  struct sigaction action{};' + NL + '  action.sa_flags = SA_SIGINFO | SA_RESTART;' + NL +
     '  action.sa_sigaction = signal_handler;' + NL + '  sigemptyset(&action.sa_mask);' + NL +
     '  if (sigaction(GetSystemSignal(type), &action, nullptr) != 0) {' + NL +
     '    signal_handler_installed[static_cast<size_t>(type)] = false;' + NL + '  }',
     '  // Horizon: no signals, so thread suspend and user callbacks are not delivered.' + NL + '  (void)signal_handler;'),
    ('  return static_cast<uint32_t>(syscall(SYS_gettid));',
     '  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));'),
    ('  uint32_t system_id() const { return static_cast<uint32_t>(thread_); }',
     '  uint32_t system_id() const { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread_)); }'),
    ('#if REX_PLATFORM_LINUX' + NL, '#if REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL),
]
for old, new in reps:
    if old not in src and new not in src:
        raise SystemExit('threading_posix.cpp: anchor not found: ' + old[:60])
    src = src.replace(old, new)
write('src/core/threading_switch.cpp',
      '// Generated by switch/sdk-edits.py from threading_posix.cpp for Horizon: no signals, no robust' + NL +
      '// mutexes, pthread_t-based thread ids. Do not edit.' + NL + src)
edit('src/core/CMakeLists.txt', [
    ('|memory_posix|seh_posix)', '|memory_posix|seh_posix|threading_posix)'),
    ('    target_sources(rexcore PRIVATE ${REXGLUE_SWITCH_CORE_SOURCES})' + NL,
     '    target_sources(rexcore PRIVATE ${REXGLUE_SWITCH_CORE_SOURCES} threading_switch.cpp)' + NL),
])
print('changed (2):', *changed, sep=NL + '  ')
