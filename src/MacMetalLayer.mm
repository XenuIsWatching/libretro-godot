#include "MacMetalLayer.hpp"

#ifdef __APPLE__

#import <QuartzCore/CAMetalLayer.h>

namespace Xenu
{
void* CreateMacMetalLayer(int width, int height)
{
    @autoreleasepool
    {
        CAMetalLayer* layer = [[CAMetalLayer alloc] init];
        // Leave device nil. MoltenVK assigns the MTLDevice belonging to the
        // selected VkPhysicalDevice when it creates a swapchain; pinning the
        // system default here can mismatch an eGPU selected by a core.
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;
        layer.contentsScale = 1.0;
        layer.drawableSize = CGSizeMake(width, height);
        layer.frame = CGRectMake(0, 0, width, height);
        return layer;
    }
}

void DestroyMacMetalLayer(void* opaque_layer)
{
    if (!opaque_layer)
        return;

    @autoreleasepool
    {
        CAMetalLayer* layer = static_cast<CAMetalLayer*>(opaque_layer);
        [layer release];
    }
}

} // namespace Xenu

#endif
