#pragma once

#include <vulkan/vulkan.h>
#include <libretro_vulkan.h>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <mutex>

namespace SK
{

class VulkanContext
{
public:
    bool Init(retro_hw_render_context_negotiation_interface_vulkan* negotiation);
    void Destroy();

    void SetImage(const retro_vulkan_image* image, uint32_t num_semaphores,
                  const VkSemaphore* semaphores, uint32_t src_queue_family);
    void ReadbackToPixels(uint32_t width, uint32_t height, godot::PackedByteArray& out);

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

    bool m_initialized = false;

    VkInstance       m_instance     = VK_NULL_HANDLE;
    VkPhysicalDevice m_gpu          = VK_NULL_HANDLE;
    VkDevice         m_device       = VK_NULL_HANDLE;
    VkQueue          m_queue        = VK_NULL_HANDLE;
    uint32_t         m_queue_family = 0;
    VkCommandPool    m_cmd_pool     = VK_NULL_HANDLE;
    VkCommandBuffer  m_cmd_buf      = VK_NULL_HANDLE;
    VkFence          m_fence        = VK_NULL_HANDLE;

    VkBuffer       m_staging_buf  = VK_NULL_HANDLE;
    VkDeviceMemory m_staging_mem  = VK_NULL_HANDLE;
    VkDeviceSize   m_staging_size = 0;

    // Current image state (populated by SetImage)
    VkImage                 m_current_vk_image         = VK_NULL_HANDLE;
    VkFormat                m_current_format           = VK_FORMAT_UNDEFINED;
    VkImageLayout           m_current_layout           = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageSubresourceRange m_current_subresource_range{};
    uint32_t                m_src_queue_family         = VK_QUEUE_FAMILY_IGNORED;
    VkSemaphore             m_signal_semaphore         = VK_NULL_HANDLE;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
#ifdef _WIN32
    void* m_hidden_hwnd = nullptr;
#endif

    std::mutex m_queue_mutex;

    retro_hw_render_context_negotiation_interface_vulkan* m_negotiation = nullptr;
    retro_hw_render_interface_vulkan m_interface{};
};

} // namespace SK
