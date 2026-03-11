#include "VulkanContext.hpp"
#include "Debug.hpp"

#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

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

        // Cores may advertise a low apiVersion (e.g. 1.0) but internally
        // compile shaders targeting a higher SPIR-V version.  Query the
        // driver's maximum supported instance version and use that so the
        // validation environment matches what the core actually needs.
        uint32_t max_api_version = VK_API_VERSION_1_2;
        auto enumVer = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        if (enumVer)
            enumVer(&max_api_version);

        if (app_info_ptr)
        {
            // Copy the core's app info but override the API version.
            app_info = *app_info_ptr;
            app_info.apiVersion = max_api_version;
            app_info_ptr = &app_info;
        }
        else
        {
            app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName   = "SKLibretro";
            app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            app_info.pEngineName        = "SKLibretro";
            app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
            app_info.apiVersion         = max_api_version;
            app_info_ptr = &app_info;
        }

        VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ici.pApplicationInfo = app_info_ptr;

        // Enable extensions needed by cores for Vulkan HW rendering.
        std::vector<const char*> inst_exts;
        inst_exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
        inst_exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
        inst_exts.push_back("VK_KHR_get_physical_device_properties2");
        inst_exts.push_back("VK_KHR_get_surface_capabilities2");
        inst_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        ici.enabledExtensionCount   = static_cast<uint32_t>(inst_exts.size());
        ici.ppEnabledExtensionNames = inst_exts.data();

        // Enable validation layers in debug builds to catch invalid Vulkan usage.
        const char* validation_layer = "VK_LAYER_KHRONOS_validation";
        ici.enabledLayerCount   = 1;
        ici.ppEnabledLayerNames = &validation_layer;

        VkResult r = vkCreateInstance(&ici, nullptr, &m_instance);
        if (r != VK_SUCCESS)
        {
            LogError("VulkanContext: vkCreateInstance failed: " + std::to_string(r));
            return false;
        }
    }

    LogOK("VulkanContext: VkInstance created.");

    // ---- Set up validation debug messenger ----
    {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger)
        {
            VkDebugUtilsMessengerCreateInfoEXT dbg_ci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            dbg_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                                   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            dbg_ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                   | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbg_ci.pfnUserCallback = [](
                VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                const VkDebugUtilsMessengerCallbackDataEXT* data,
                void* /*user*/) -> VkBool32
            {
                if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                    LogError("VkValidation: " + std::string(data->pMessage));
                else
                    LogWarning("VkValidation: " + std::string(data->pMessage));
                return VK_FALSE;
            };
            createMessenger(m_instance, &dbg_ci, nullptr, &m_debug_messenger);
            LogOK("VulkanContext: Validation layers enabled.");
        }
    }

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

    // ---- Create a surface for cores that require one (e.g. Dolphin) ----
#ifdef _WIN32
    {
        // Use WS_POPUP so the entire window is client area (no borders/title
        // bar that shrink it at high DPI).  Size must exceed the core's EFB
        // dimensions (640×528 at 1×, 1280×1056 at 2×, etc.).
        m_hidden_hwnd = CreateWindowExW(
            0, L"STATIC", L"SKLibretro_VkSurface", WS_POPUP,
            0, 0, 1920, 1080, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (m_hidden_hwnd)
        {
            auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
                vkGetInstanceProcAddr(m_instance, "vkCreateWin32SurfaceKHR"));
            if (createSurface)
            {
                VkWin32SurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
                sci.hinstance = GetModuleHandleW(nullptr);
                sci.hwnd      = static_cast<HWND>(m_hidden_hwnd);
                VkResult r = createSurface(m_instance, &sci, nullptr, &m_surface);
                if (r != VK_SUCCESS)
                    LogWarning("VulkanContext: vkCreateWin32SurfaceKHR failed: " + std::to_string(r));
                else
                    LogOK("VulkanContext: VkSurfaceKHR created.");
            }
        }
    }
#endif

    // ---- Create logical device ----
    retro_vulkan_context vk_ctx{};
    bool device_from_negotiation = false;

    if (neg)
    {
        if (neg->interface_version >= 2 && neg->create_device2)
        {
            device_from_negotiation = neg->create_device2(
                &vk_ctx, m_instance, m_gpu, m_surface,
                vkGetInstanceProcAddr, s_CreateDeviceWrapper, this);

            if (!device_from_negotiation)
                LogWarning("VulkanContext: create_device2 failed; using self-created device");
        }
        else if (neg->create_device)
        {
            device_from_negotiation = neg->create_device(
                &vk_ctx, m_instance, m_gpu, m_surface,
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

    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

#ifdef _WIN32
    if (m_hidden_hwnd)
    {
        DestroyWindow(static_cast<HWND>(m_hidden_hwnd));
        m_hidden_hwnd = nullptr;
    }
#endif

    if (m_debug_messenger != VK_NULL_HANDLE)
    {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger)
            destroyMessenger(m_instance, m_debug_messenger, nullptr);
        m_debug_messenger = VK_NULL_HANDLE;
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
