# libretro-godot

A GDExtension (C++) that runs libretro emulator cores inside Godot 4.5+. Bridges Godot's scene system with the libretro API, enabling retro game emulation within Godot projects.

Originally forked from [Skurdt/libretro-godot](https://github.com/Skurdt/libretro-godot).

## Prerequisites

- **SCons** — build system
- **MSVC** (Windows) or **Android NDK r27d+** (Android/Quest)
- **Python 3** — required by SCons

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

| Platform | Arch | Compiler | HW Rendering |
|----------|------|----------|--------------|
| Windows | x86_64 | MSVC (C++latest) | SDL3 + OpenGL |
| Android | arm64 | Clang/NDK (C++20) | EGL + OpenGL ES 3.0 |

## Dependencies

All dependencies are included in `external/` or as submodules:

- **godot-cpp** (submodule, 4.5 branch) — Godot C++ bindings
- **SDL3** — DLL loading and HW render context on Windows
- **libretro-common** — VFS, audio conversion, pixel format conversion
- **moodycamel::ReaderWriterQueue** — Lock-free SPSC queue for cross-thread communication
