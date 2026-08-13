# MoltenVK

This directory vendors the official macOS dynamic library from
KhronosGroup/MoltenVK v1.4.2:

https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2

Source archive: `MoltenVK-macos.tar`

- Archive SHA-256: `f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e`
- `libMoltenVK.dylib` SHA-256: `aef00b13bcc808adf15b85bef9ae67393d92be7ed5dfe41cad16fa809e4a4c5f`
- Architectures: arm64 and x86_64 (universal binary)
- Minimum macOS: 12.0

The Apache-2.0 licence from the release is preserved as `LICENSE`.

Godot's macOS executable already statically embeds MoltenVK and exports the
Vulkan entry points used by GDExtensions. `libretro-godot` therefore binds to
that process-wide copy with dynamic lookup. It does not load this dylib into
Godot as a second MoltenVK runtime, because duplicate Objective-C classes from
two copies can crash the process. The dylib is retained here as the pinned
upstream artifact for provenance and standalone Vulkan diagnostics.
