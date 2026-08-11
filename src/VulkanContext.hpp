#pragma once

#include <vulkan/vulkan.h>
#include <libretro_vulkan.h>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <mutex>

namespace Xenu
{

class VulkanContext
{
public:
    /// `frame_w`/`frame_h` are the core's MAXIMUM frame size. They matter because
    /// a core that needs a surface (Dolphin) builds its swapchain to fit it and
    /// presents a frame of that size — so the surface has to be the size we
    /// intend to read back, not an arbitrary one. See the window creation in
    /// Init() for what went wrong when it was fixed at 1920x1080.
    ~VulkanContext();

    bool Init(retro_hw_render_context_negotiation_interface_vulkan* negotiation,
              int32_t frame_w, int32_t frame_h);
    /// Safe to call at any point, including on a half-built context after Init()
    /// returned false — it tears down whatever exists rather than gating on
    /// success, because a failed Init still leaves an instance, a surface and a
    /// platform window behind.
    void Destroy();

    void SetImage(const retro_vulkan_image* image, uint32_t num_semaphores,
                  const VkSemaphore* semaphores, uint32_t src_queue_family);
    /// False when no frame was produced — no image published, an allocation
    /// failed, or an earlier submit is still in flight. `out` is meaningless
    /// then and must not be uploaded: the caller pre-sizes it, so its contents
    /// are uninitialised rather than merely stale.
    [[nodiscard]] bool ReadbackToPixels(uint32_t width, uint32_t height, godot::PackedByteArray& out);

    retro_hw_render_interface_vulkan* GetInterface()
    {
        return m_initialized ? &m_interface : nullptr;
    }

    // Static callback trampolines for retro_hw_render_interface_vulkan
    static void     s_SetImage(void* handle, const retro_vulkan_image* image,
                               uint32_t n_sems, const VkSemaphore* sems, uint32_t src_family);
    static uint32_t s_GetSyncIndex(void* handle);
    static uint32_t s_GetSyncIndexMask(void* handle);
    static void     s_SetCommandBuffers(void* handle, uint32_t num_cmd, const VkCommandBuffer* cmd);
    static void     s_WaitSyncIndex(void* handle);
    static void     s_LockQueue(void* handle);
    static void     s_UnlockQueue(void* handle);
    static void     s_SetSignalSemaphore(void* handle, VkSemaphore semaphore);

private:
    bool CreateStagingBuffer(VkDeviceSize size);
    void DestroyStagingBuffer();
    uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties);

    /// Requires m_fence_mutex held. True when no readback submit is outstanding —
    /// the fence, the command buffer and the staging buffer are safe to reuse.
    /// False means one is STILL RUNNING and the caller must leave all three
    /// alone; m_readback_pending stays set so the next attempt waits again
    /// rather than resetting objects the GPU is reading.
    [[nodiscard]] bool WaitReadbackFenceLocked();

    /// Hand back whatever the core last passed to set_signal_semaphore, clearing
    /// it. Takes m_state_mutex.
    VkSemaphore TakePendingSemaphore();
    /// Signal a semaphore with an empty submit. Takes m_queue_mutex.
    void SignalSemaphoreNow(VkSemaphore semaphore);
    /// TakePendingSemaphore + SignalSemaphoreNow. A core that called
    /// set_signal_semaphore is waiting on it; bailing out of the readback
    /// without submitting would stall its queue for good.
    void SignalPendingSemaphore();

    /// Ask the driver what actually killed the device, via VK_EXT_device_fault.
    /// A bare VK_ERROR_DEVICE_LOST says only "something died"; this returns the
    /// driver's own description plus the addresses or instruction pointers that
    /// were live at the fault. Safe to call when the extension is absent — it
    /// resolves vkGetDeviceFaultInfoEXT and says so if that comes back null.
    void DumpDeviceFault();
    /// The device is lost for good, so the same fault would otherwise be printed
    /// on every dropped frame — this run produced ~1400 of them.
    bool m_fault_reported = false;

    bool m_initialized = false;

    VkInstance       m_instance     = VK_NULL_HANDLE;
    VkPhysicalDevice m_gpu          = VK_NULL_HANDLE;
    VkDevice         m_device       = VK_NULL_HANDLE;
    VkQueue          m_queue        = VK_NULL_HANDLE;
    uint32_t         m_queue_family = 0;
    VkCommandPool    m_cmd_pool     = VK_NULL_HANDLE;
    VkCommandBuffer  m_cmd_buf      = VK_NULL_HANDLE;
    VkFence          m_fence        = VK_NULL_HANDLE;
    VkFence          m_sem_fence    = VK_NULL_HANDLE;

    // Guards every m_fence reset/submit/wait as one unit. Resetting a fence
    // while another thread sits in vkWaitForFences on it is undefined, and
    // Adreno drops the wakeup — the waiter then never returns.
    // Lock order is m_fence_mutex -> m_queue_mutex, never the reverse.
    std::mutex m_fence_mutex;
    bool       m_readback_pending = false;

    VkBuffer       m_staging_buf  = VK_NULL_HANDLE;
    VkDeviceMemory m_staging_mem  = VK_NULL_HANDLE;
    VkDeviceSize   m_staging_size = 0;

    // Guards the current image state below. Both SetImage and ReadbackToPixels
    // normally run on the emulation thread (ReadbackToPixels is called from
    // inside video_refresh), but libretro_vulkan.h explicitly allows a core to
    // build and submit command buffers from any thread, and Dolphin does spawn
    // its own — so these are genuinely shared.
    // Leaf lock: never acquire another mutex while holding it, or it deadlocks
    // against SetImage's queue submit. Order is m_fence_mutex -> m_state_mutex
    // -> m_queue_mutex.
    std::mutex m_state_mutex;

    // Current image state (populated by SetImage)
    VkImage                 m_current_vk_image         = VK_NULL_HANDLE;
    VkFormat                m_current_format           = VK_FORMAT_UNDEFINED;
    VkImageLayout           m_current_layout           = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageSubresourceRange m_current_subresource_range{};
    /// The queue family that owns the image, per set_image. Anything other than
    /// this context's own family or VK_QUEUE_FAMILY_IGNORED means the core
    /// released ownership to us and ReadbackToPixels owes it a matching acquire
    /// and a release back (libretro_vulkan.h: "the frontend will always release
    /// ownership back to src_queue_family").
    uint32_t                m_src_queue_family         = VK_QUEUE_FAMILY_IGNORED;
    VkSemaphore             m_signal_semaphore         = VK_NULL_HANDLE;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;

    /// The core's max frame size, which is also the size of the surface we make
    /// for it on the platforms that build one. See Init().
    ///
    /// NOT inside the _WIN32 block below, even though only Windows sizes a window
    /// with it: Init() and ReadbackToPixels are compiled for every platform, so a
    /// member they touch has to exist for every platform. Guarded, this built on
    /// MSVC and failed the NDK outright.
    int32_t m_frame_w = 640;
    int32_t m_frame_h = 480;
    /// The format already reported as unconvertible, so the warning fires once
    /// per format rather than once per frame. Signed because VK_FORMAT_UNDEFINED
    /// is 0 and would otherwise read as "already warned".
    mutable int64_t m_warned_format = -1;
    /// One-shot latches for core behaviour we do not implement, so a core that
    /// relies on it says so once instead of failing silently.
    bool m_warned_set_command_buffers = false;
    bool m_logged_src_queue_family    = false;
#ifdef _WIN32
    void* m_hidden_hwnd = nullptr;
#endif
#ifdef __ANDROID__
    void* m_mediandk       = nullptr;  // libmediandk.so handle
    void* m_android_reader = nullptr;  // AImageReader* backing m_surface
#endif

    std::mutex m_queue_mutex;

    retro_hw_render_context_negotiation_interface_vulkan* m_negotiation = nullptr;
    /// Set once we have actually called one of the negotiation create entry
    /// points, which is the condition libretro_vulkan.h attaches to
    /// destroy_device — it must run even when creation failed, but must not run
    /// for a core we never asked to create anything.
    bool m_negotiation_engaged = false;
    retro_hw_render_interface_vulkan m_interface{};
};

} // namespace Xenu
