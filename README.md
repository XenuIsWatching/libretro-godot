# libretro-godot

A GDExtension (C++) that runs libretro emulator cores inside Godot 4.5+. Bridges Godot's scene system with the libretro API, enabling retro game emulation within Godot projects.

Originally forked from [Skurdt/libretro-godot](https://github.com/Skurdt/libretro-godot).

## Prerequisites

- **SCons** — build system
- **MSVC** (Windows), **GCC/Clang** (Linux), or **Android NDK r27d+** (Android/Quest)
- **Python 3** — required by SCons
- **Vulkan loader, SDL3, OpenGL** (Linux) — linked by soname at build time
  (`libvulkan.so.1`, `libSDL3.so.0`, `libGL.so.1`); these ship with the distro's Vulkan
  runtime, SDL3 package, and Mesa. The `-dev`/`-devel` packages are not required.

## Setup

```bash
git clone --recursive https://github.com/XenuIsWatching/libretro-godot.git
cd libretro-godot
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

## Building

### Windows (x86_64)

```powershell
scons platform=windows arch=x86_64 target=template_debug dev_build=yes
scons platform=windows arch=x86_64 target=template_release
```

### Linux (x86_64)

```bash
scons platform=linux arch=x86_64 target=template_debug
scons platform=linux arch=x86_64 target=template_release
```

Links the Vulkan loader, SDL3, and the GL loader by soname (`libvulkan.so.1`,
`libSDL3.so.0`, `libGL.so.1`), so no `-dev`/`-devel` packages are needed. Software,
OpenGL, and Vulkan hardware-render cores all work. OpenGL uses SDL3 to create a hidden
GL context (the same code path as Windows), so a display server (X11/Wayland) must be
available when a GL core runs — headless GL init fails gracefully.

### Android / Quest (arm64)

Requires `ANDROID_NDK_ROOT` to be set:

```bash
ANDROID_NDK_ROOT="/path/to/android-ndk-r27d" ANDROID_HOME="" \
  scons platform=android arch=arm64 target=template_debug
```

### Output

By default, compiled libraries are placed in `bin/`.

To override the output directory, set the `LIBRETRO_GODOT_OUTPUT_DIR` environment variable:

```bash
# Output to a Godot project directory
LIBRETRO_GODOT_OUTPUT_DIR="../MyProject/addons/libretro_godot" scons platform=windows arch=x86_64 target=template_debug
```

## Using as a Submodule

When embedding this repo as a submodule in a Godot project, the parent repo's `SConstruct` can override the output directory directly:

```python
# Parent SConstruct example
VariantDir('libretro-godot/Temp', 'libretro-godot', duplicate=0)
env = Environment()
output_dir = '#path/to/godot/project/libretro-godot'

SConscript('libretro-godot/Temp/SConscript', exports=['env', 'output_dir'])
```

The `#` prefix makes the path relative to the parent project root.

## Supported Platforms

All platforms also support software-rendered cores (the common case). The table below
covers **hardware**-render support:

| Platform | Arch | Compiler | HW Rendering |
|----------|------|----------|--------------|
| Windows | x86_64 | MSVC (C++latest) | Vulkan + OpenGL (SDL3) |
| Linux | x86_64 | GCC/Clang (C++20) | Vulkan + OpenGL (SDL3) |
| Android | arm64 | Clang/NDK (C++20) | Vulkan + OpenGL ES 3.0 (EGL) |

## Dependencies

All dependencies are included in `external/` or as submodules:

- **godot-cpp** (submodule, 4.5 branch) — Godot C++ bindings
- **SDL3** — DLL loading and HW render context on Windows
- **libretro-common** — VFS, audio conversion, pixel format conversion
- **moodycamel::ReaderWriterQueue** — Lock-free SPSC queue for cross-thread communication
