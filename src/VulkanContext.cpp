#include "VulkanContext.hpp"
#include "Debug.hpp"

#include <cstring>
#include <vector>

using namespace godot;

namespace SK
{

// ---------------------------------------------------------------------------
// Static callback trampolines
// ---------------------------------------------------------------------------

void VulkanContext::s_SetImage(void* handle, const retro_vulkan_image* image,
                               uint32_t n_sems, const VkSemaphore* sems, uint32_t src_family)
{
    static_cast<VulkanContext*>(handle)->SetImage(image, n_sems, sems, src_family);
}

uint32_t VulkanContext::s_GetSyncIndex(void* /*handle*/)
{
    return 0;
}

uint32_t VulkanContext::s_GetSyncIndexMask(void* /*handle*/)
{
    return 0x1;
}

void VulkanContext::s_SetCommandBuffers(void* /*handle*/, uint32_t /*num_cmd*/, const VkCommandBuffer* /*cmd*/)
{
    // Phase 1: not consumed; core command buffers submitted directly by the core
}

void VulkanContext::s_WaitSyncIndex(void* handle)
{
    auto* ctx = static_cast<VulkanContext*>(handle);
    if (ctx->m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(ctx->m_device);
}

void VulkanContext::s_LockQueue(void* handle)
{
    static_cast<VulkanContext*>(handle)->m_queue_mutex.lock();
}

void VulkanContext::s_UnlockQueue(void* handle)
{
    static_cast<VulkanContext*>(handle)->m_queue_mutex.unlock();
}

void VulkanContext::s_SetSignalSemaphore(void* handle, VkSemaphore semaphore)
{
    static_cast<VulkanContext*>(handle)->m_signal_semaphore = semaphore;
}

// ---------------------------------------------------------------------------
// Instance/device creation wrappers (passed to core's negotiation interface)
// ---------------------------------------------------------------------------

static VkInstance s_CreateInstanceWrapper(void* /*opaque*/, const VkInstanceCreateInfo* ci)
{
    VkInstance inst = VK_NULL_HANDLE;
    vkCreateInstance(ci, nullptr, &inst);
    return inst;
}

static VkDevice s_CreateDeviceWrapper(VkPhysicalDevice gpu, void* /*opaque*/, const VkDeviceCreateInfo* ci)
{
    VkDevice dev = VK_NULL_HANDLE;
    vkCreateDevice(gpu, ci, nullptr, &dev);
    return dev;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

bool VulkanContext::Init(retro_hw_render_context_negotiation_interface_vulkan* neg)
{
    m_negotiation = neg;

    // ---- Create VkInstance ----
    // If the core supplies v2 negotiation with create_instance, let it drive instance creation.
    if (neg && neg->interface_version >= 2 && neg->create_instance)
    {
        const VkApplicationInfo* app_info =
            (neg->get_application_info) ? neg->get_application_info() : nullptr;

        m_instance = neg->create_instance(vkGetInstanceProcAddr, app_info,
                                          s_CreateInstanceWrapper, this);

        if (!m_instance)
            LogWarning("VulkanContext: core's create_instance returned null; using self-created instance");
    }

    if (m_instance == VK_NULL_HANDLE)
    {
        const VkApplicationInfo* app_info_ptr = nullptr;
        VkApplicationInfo app_info{};

        if (neg && neg->get_application_info)
            app_info_ptr = neg->get_application_info();

        if (!app_info_ptr)
        {
            app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName   = "SKLibretro";
            app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            app_info.pEngineName        = "SKLibretro";
            app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
            app_info.apiVersion         = VK_API_VERSION_1_1;
            app_info_ptr = &app_info;
        }

        VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ici.pApplicationInfo = app_info_ptr;

        VkResult r = vkCreateInstance(&ici, nullptr, &m_instance);
        if (r != VK_SUCCESS)
        {
            LogError("VulkanContext: vkCreateInstance failed: " + std::to_string(r));
            return false;
        }
    }

    LogOK("VulkanContext: VkInstance created.");

    // ---- Select physical device ----
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(m_instance, &gpu_count, nullptr);
    if (gpu_count == 0)
    {
        LogError("VulkanContext: No GPUs found.");
        return false;
    }

    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(m_instance, &gpu_count, gpus.data());

    m_gpu = VK_NULL_HANDLE;
    for (auto& g : gpus)
    {
        uint32_t qfam_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qfam_count, nullptr);
        std::vector<VkQueueFamilyProperties> qfams(qfam_count);
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qfam_count, qfams.data());

        for (uint32_t i = 0; i < qfam_count; ++i)
        {
            constexpr VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((qfams[i].queueFlags & required) == required)
            {
                m_gpu          = g;
                m_queue_family = i;
                break;
            }
        }
        if (m_gpu != VK_NULL_HANDLE)
            break;
    }

    if (m_gpu == VK_NULL_HANDLE)
    {
        LogError("VulkanContext: No GPU with graphics+compute queue found.");
        return false;
    }

    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_gpu, &props);
        LogOK("VulkanContext: GPU: " + std::string(props.deviceName));
    }

    // ---- Create logical device ----
    retro_vulkan_context vk_ctx{};
    bool device_from_negotiation = false;

    if (neg)
    {
        if (neg->interface_version >= 2 && neg->create_device2)
        {
            device_from_negotiation = neg->create_device2(
                &vk_ctx, m_instance, m_gpu, VK_NULL_HANDLE,
                vkGetInstanceProcAddr, s_CreateDeviceWrapper, this);

            if (!device_from_negotiation)
                LogWarning("VulkanContext: create_device2 failed; using self-created device");
        }
        else if (neg->create_device)
        {
            device_from_negotiation = neg->create_device(
                &vk_ctx, m_instance, m_gpu, VK_NULL_HANDLE,
                vkGetInstanceProcAddr, nullptr, 0, nullptr, 0, nullptr);

            if (!device_from_negotiation)
                LogWarning("VulkanContext: create_device failed; using self-created device");
        }
    }

    if (device_from_negotiation)
    {
        m_device       = vk_ctx.device;
        m_queue        = vk_ctx.queue;
        m_queue_family = vk_ctx.queue_family_index;
        m_gpu          = vk_ctx.gpu;
    }
    else
    {
        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = m_queue_family;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &queue_priority;

        VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos    = &qci;

        VkResult r = vkCreateDevice(m_gpu, &dci, nullptr, &m_device);
        if (r != VK_SUCCESS)
        {
            LogError("VulkanContext: vkCreateDevice failed: " + std::to_string(r));
            return false;
        }

        vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);
    }

    LogOK("VulkanContext: VkDevice created.");

    // ---- Command pool + buffer ----
    VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.queueFamilyIndex = m_queue_family;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &cpci, nullptr, &m_cmd_pool) != VK_SUCCESS)
    {
        LogError("VulkanContext: vkCreateCommandPool failed.");
        return false;
    }

    VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool        = m_cmd_pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device, &cbai, &m_cmd_buf) != VK_SUCCESS)
    {
        LogError("VulkanContext: vkAllocateCommandBuffers failed.");
        return false;
    }

    // ---- Fence ----
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(m_device, &fci, nullptr, &m_fence) != VK_SUCCESS)
    {
        LogError("VulkanContext: vkCreateFence failed.");
        return false;
    }

    // ---- Fill retro_hw_render_interface_vulkan ----
    m_interface.interface_type       = RETRO_HW_RENDER_INTERFACE_VULKAN;
    m_interface.interface_version    = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    m_interface.handle               = this;
    m_interface.instance             = m_instance;
    m_interface.gpu                  = m_gpu;
    m_interface.device               = m_device;
    m_interface.get_device_proc_addr   = vkGetDeviceProcAddr;
    m_interface.get_instance_proc_addr = vkGetInstanceProcAddr;
    m_interface.queue                = m_queue;
    m_interface.queue_index          = m_queue_family;
    m_interface.set_image            = s_SetImage;
    m_interface.get_sync_index       = s_GetSyncIndex;
    m_interface.get_sync_index_mask  = s_GetSyncIndexMask;
    m_interface.set_command_buffers  = s_SetCommandBuffers;
    m_interface.wait_sync_index      = s_WaitSyncIndex;
    m_interface.lock_queue           = s_LockQueue;
    m_interface.unlock_queue         = s_UnlockQueue;
    m_interface.set_signal_semaphore = s_SetSignalSemaphore;

    m_initialized = true;
    LogOK("VulkanContext: initialized.");
    return true;
}

// ---------------------------------------------------------------------------
// Destroy
// ---------------------------------------------------------------------------

void VulkanContext::Destroy()
{
    if (!m_initialized)
        return;

    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

    DestroyStagingBuffer();

    if (m_fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(m_device, m_fence, nullptr);
        m_fence = VK_NULL_HANDLE;
    }

    if (m_cmd_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_device, m_cmd_pool, nullptr);
        m_cmd_pool = VK_NULL_HANDLE;
        m_cmd_buf  = VK_NULL_HANDLE;
    }

    // Let the negotiation interface clean up any auxiliary resources it owns.
    if (m_negotiation && m_negotiation->destroy_device)
        m_negotiation->destroy_device();

    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_initialized = false;
}

// ---------------------------------------------------------------------------
// SetImage — called by the core before retro_video_refresh
// ---------------------------------------------------------------------------

void VulkanContext::SetImage(const retro_vulkan_image* image,
                             uint32_t n_sems, const VkSemaphore* sems,
                             uint32_t src_family)
{
    if (!image)
    {
        m_current_vk_image = VK_NULL_HANDLE;
        return;
    }

    // Copy only the fields we need — do NOT deep-copy pNext chains.
    m_current_vk_image          = image->create_info.image;
    m_current_format            = image->create_info.format;
    m_current_layout            = image->image_layout;
    m_current_subresource_range = image->create_info.subresourceRange;
    m_src_queue_family          = src_family;

    // Wait on any semaphores the core provided (submit a no-op with them as wait sems).
    if (n_sems > 0 && sems)
    {
        std::vector<VkPipelineStageFlags> wait_stages(n_sems,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.waitSemaphoreCount = n_sems;
        si.pWaitSemaphores    = sems;
        si.pWaitDstStageMask  = wait_stages.data();

        m_queue_mutex.lock();
        vkResetFences(m_device, 1, &m_fence);
        vkQueueSubmit(m_queue, 1, &si, m_fence);
        m_queue_mutex.unlock();

        vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    }
}

// ---------------------------------------------------------------------------
// ReadbackToPixels — blits the Vulkan image to a staging buffer and copies
// to a PackedByteArray.  Replaces glReadPixels for Vulkan cores.
// ---------------------------------------------------------------------------

void VulkanContext::ReadbackToPixels(uint32_t width, uint32_t height, PackedByteArray& out)
{
    if (m_current_vk_image == VK_NULL_HANDLE)
    {
        LogError("VulkanContext::ReadbackToPixels: no current image set");
        return;
    }

    const VkDeviceSize needed = (VkDeviceSize)width * height * 4u;
    if (m_staging_size < needed)
    {
        DestroyStagingBuffer();
        if (!CreateStagingBuffer(needed))
            return;
    }

    // ---- Record command buffer ----
    VkCommandBufferBeginInfo cbbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResetCommandBuffer(m_cmd_buf, 0);
    vkBeginCommandBuffer(m_cmd_buf, &cbbi);

    // Transition: current layout → TRANSFER_SRC_OPTIMAL
    const bool need_layout_transition =
        (m_current_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    if (need_layout_transition)
    {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                      VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout           = m_current_layout;
        barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_current_vk_image;
        barrier.subresourceRange    = m_current_subresource_range;

        vkCmdPipelineBarrier(m_cmd_buf,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Copy image → staging buffer
    VkBufferImageCopy copy{};
    copy.bufferOffset      = 0;
    copy.bufferRowLength   = 0; // tightly packed
    copy.bufferImageHeight = 0; // tightly packed
    copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel       = 0;
    copy.imageSubresource.baseArrayLayer = m_current_subresource_range.baseArrayLayer;
    copy.imageSubresource.layerCount     = 1;
    copy.imageOffset = { 0, 0, 0 };
    copy.imageExtent = { width, height, 1 };

    vkCmdCopyImageToBuffer(m_cmd_buf, m_current_vk_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_staging_buf, 1, &copy);

    // Transition image back to its original layout
    if (need_layout_transition)
    {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                      VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout           = m_current_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_current_vk_image;
        barrier.subresourceRange    = m_current_subresource_range;

        vkCmdPipelineBarrier(m_cmd_buf,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Staging buffer host-read barrier
    VkBufferMemoryBarrier buf_barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    buf_barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    buf_barrier.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
    buf_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.buffer              = m_staging_buf;
    buf_barrier.offset              = 0;
    buf_barrier.size                = needed;

    vkCmdPipelineBarrier(m_cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &buf_barrier, 0, nullptr);

    vkEndCommandBuffer(m_cmd_buf);

    // ---- Submit + wait ----
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_cmd_buf;

    // Signal the semaphore requested by the core (if any)
    if (m_signal_semaphore != VK_NULL_HANDLE)
    {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &m_signal_semaphore;
        m_signal_semaphore      = VK_NULL_HANDLE;
    }

    m_queue_mutex.lock();
    vkResetFences(m_device, 1, &m_fence);
    vkQueueSubmit(m_queue, 1, &si, m_fence);
    m_queue_mutex.unlock();

    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);

    // ---- Map and copy to output ----
    void* mapped = nullptr;
    vkMapMemory(m_device, m_staging_mem, 0, needed, 0, &mapped);

    out.resize((int64_t)needed);
    uint8_t*       dst = out.ptrw();
    const uint8_t* src = static_cast<const uint8_t*>(mapped);

    // Normalize BGRA formats to RGBA (Godot Image::FORMAT_RGBA8 expects R first)
    if (m_current_format == VK_FORMAT_B8G8R8A8_UNORM ||
        m_current_format == VK_FORMAT_B8G8R8A8_SRGB  ||
        m_current_format == VK_FORMAT_B8G8R8A8_SNORM)
    {
        const uint32_t pixel_count = width * height;
        for (uint32_t i = 0; i < pixel_count; ++i)
        {
            dst[i * 4 + 0] = src[i * 4 + 2]; // R ← B
            dst[i * 4 + 1] = src[i * 4 + 1]; // G ← G
            dst[i * 4 + 2] = src[i * 4 + 0]; // B ← R
            dst[i * 4 + 3] = src[i * 4 + 3]; // A ← A
        }
    }
    else
    {
        memcpy(dst, src, (size_t)needed);
    }

    vkUnmapMemory(m_device, m_staging_mem);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool VulkanContext::CreateStagingBuffer(VkDeviceSize size)
{
    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size        = size;
    bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bci, nullptr, &m_staging_buf) != VK_SUCCESS)
    {
        LogError("VulkanContext: vkCreateBuffer (staging) failed.");
        return false;
    }

    VkMemoryRequirements mem_req{};
    vkGetBufferMemoryRequirements(m_device, m_staging_buf, &mem_req);

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize  = mem_req.size;
    mai.memoryTypeIndex = FindMemoryType(mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &mai, nullptr, &m_staging_mem) != VK_SUCCESS)
    {
        LogError("VulkanContext: vkAllocateMemory (staging) failed.");
        vkDestroyBuffer(m_device, m_staging_buf, nullptr);
        m_staging_buf = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(m_device, m_staging_buf, m_staging_mem, 0);
    m_staging_size = size;
    return true;
}

void VulkanContext::DestroyStagingBuffer()
{
    if (m_device == VK_NULL_HANDLE)
        return;

    if (m_staging_buf != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_device, m_staging_buf, nullptr);
        m_staging_buf = VK_NULL_HANDLE;
    }
    if (m_staging_mem != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_device, m_staging_mem, nullptr);
        m_staging_mem = VK_NULL_HANDLE;
    }
    m_staging_size = 0;
}

uint32_t VulkanContext::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(m_gpu, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
    {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }

    LogError("VulkanContext: FindMemoryType: no suitable memory type found");
    return 0;
}

} // namespace SK
