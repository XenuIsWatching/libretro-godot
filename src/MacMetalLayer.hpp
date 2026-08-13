#pragma once

#ifdef __APPLE__

namespace Xenu
{

/// Create a retained, standalone CAMetalLayer with a fixed drawable size. It is
/// deliberately not attached to an NSView or NSWindow, so it can follow the
/// libretro context's emulation-thread lifetime without an AppKit main-thread
/// rendezvous.
void* CreateMacMetalLayer(int width, int height);

/// Release a layer returned by CreateMacMetalLayer.
void DestroyMacMetalLayer(void* layer);

} // namespace Xenu

#endif
