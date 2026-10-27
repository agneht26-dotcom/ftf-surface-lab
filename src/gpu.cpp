#ifdef _WIN32
#include <windows.h>
#endif
#include "gpu.hpp"
#include <imgui_impl_glfw.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <cstring>

void vkCheck(VkResult value) {
    if(value<0)throw std::runtime_error("Vulkan error " + std::to_string(value));
}
static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* info,void* data){
    if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++static_cast<Gpu*>(data)->validationErrors;
    std::fprintf(stderr,"Vulkan diagnostic: %s\n",info->pMessage);return VK_FALSE;
}
void Gpu::initialize(GLFWwindow* nativeWindow) {
    window=nativeWindow;vkCheck(volkInitialize());
    std::array<wchar_t,32768> executable{};
    if(!GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size())))throw std::runtime_error("Cannot locate application shaders");
    shaderRoot=std::filesystem::path(executable.data()).parent_path()/"shaders";
    uint32_t extCount{};const char** glfwExt=glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> extensions(glfwExt,glfwExt+extCount);
    uint32_t count{};vkCheck(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr));
    std::vector<VkExtensionProperties> available(count);vkCheck(vkEnumerateInstanceExtensionProperties(nullptr,&count,available.data()));
    bool debug=std::any_of(available.begin(),available.end(),[](auto& e){return std::strcmp(e.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME)==0;});
    if(debug)extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    vkCheck(vkEnumerateInstanceLayerProperties(&count,nullptr));std::vector<VkLayerProperties> layers(count);
    vkCheck(vkEnumerateInstanceLayerProperties(&count,layers.data()));
    validationActive=std::any_of(layers.begin(),layers.end(),[](auto& l){return std::strcmp(l.layerName,"VK_LAYER_KHRONOS_validation")==0;});
    const char* validation="VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="FTF Surface Lab";app.applicationVersion=VK_MAKE_VERSION(0,1,0);app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());ci.ppEnabledExtensionNames=extensions.data();
    if(validationActive){ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&validation;}
    vkCheck(vkCreateInstance(&ci,nullptr,&instance));volkLoadInstance(instance);
    if(debug){VkDebugUtilsMessengerCreateInfoEXT dc{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dc.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dc.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dc.pfnUserCallback=debugMessage;dc.pUserData=this;vkCheck(vkCreateDebugUtilsMessengerEXT(instance,&dc,nullptr,&debugMessenger));}
    vkCheck(glfwCreateWindowSurface(instance,window,nullptr,&swap.Surface));
    vkCheck(vkEnumeratePhysicalDevices(instance,&count,nullptr));std::vector<VkPhysicalDevice> devices(count);
    vkCheck(vkEnumeratePhysicalDevices(instance,&count,devices.data()));int bestScore=-1;
    for(auto candidate:devices){
        VkPhysicalDeviceProperties prop{};vkGetPhysicalDeviceProperties(candidate,&prop);
        uint32_t families{};vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
        for(uint32_t i=0;i<families;++i){VkBool32 present{};vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,swap.Surface,&present));
            if(present&&(queues[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){
                int score=(prop.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?100:0)+(prop.vendorID==0x10de?10:0);
                if(score>bestScore){bestScore=score;physical=candidate;properties=prop;queueFamily=i;timestamps=queues[i].timestampValidBits!=0;}
            }
        }
    }
    if(!physical)throw std::runtime_error("No graphics/compute/present Vulkan device found");
    float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=queueFamily;qc.queueCount=1;qc.pQueuePriorities=&priority;
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t deviceExtensionCount{};vkCheck(vkEnumerateDeviceExtensionProperties(physical,nullptr,&deviceExtensionCount,nullptr));
    std::vector<VkExtensionProperties> deviceAvailable(deviceExtensionCount);vkCheck(vkEnumerateDeviceExtensionProperties(physical,nullptr,&deviceExtensionCount,deviceAvailable.data()));
    memoryBudgetAvailable=std::any_of(deviceAvailable.begin(),deviceAvailable.end(),[](auto& e){return std::strcmp(e.extensionName,VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)==0;});
    if(memoryBudgetAvailable)deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.enabledExtensionCount=static_cast<uint32_t>(deviceExtensions.size());dc.ppEnabledExtensionNames=deviceExtensions.data();
    VkPhysicalDeviceFeatures supported{};vkGetPhysicalDeviceFeatures(physical,&supported);
    if(!supported.shaderFloat64||!supported.shaderInt64)throw std::runtime_error("This NVIDIA prototype requires shaderFloat64 and shaderInt64 for validated coordinates and hydrology");
    VkPhysicalDeviceFeatures enabled{};enabled.shaderFloat64=VK_TRUE;enabled.shaderInt64=VK_TRUE;dc.pEnabledFeatures=&enabled;
    vkCheck(vkCreateDevice(physical,&dc,nullptr,&device));volkLoadDevice(device);vkGetDeviceQueue(device,queueFamily,0,&queue);
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,64},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,384},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,64},{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,256}};
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pc.maxSets=128;pc.poolSizeCount=4;pc.pPoolSizes=sizes;vkCheck(vkCreateDescriptorPool(device,&pc,nullptr,&descriptors));
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=queueFamily;cp.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCheck(vkCreateCommandPool(device,&cp,nullptr,&utilities));
    // Persisted pipeline cache: survives a cleared or full driver shader cache, so only changed shaders recompile.
    pipelineCachePath=shaderRoot.parent_path()/"state"/"pipeline-cache.bin";
    {std::vector<char> blob;std::ifstream cacheIn(pipelineCachePath,std::ios::binary|std::ios::ate);if(cacheIn){auto bytes=static_cast<size_t>(cacheIn.tellg());if(bytes>32&&bytes<(1ull<<30)){blob.resize(bytes);cacheIn.seekg(0);cacheIn.read(blob.data(),bytes);if(!cacheIn)blob.clear();}}
     VkPipelineCacheCreateInfo cc{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};cc.initialDataSize=blob.size();cc.pInitialData=blob.empty()?nullptr:blob.data();
     if(vkCreatePipelineCache(device,&cc,nullptr,&pipelineCache)!=VK_SUCCESS){cc.initialDataSize=0;cc.pInitialData=nullptr;vkCheck(vkCreatePipelineCache(device,&cc,nullptr,&pipelineCache));}}
    const VkFormat requested[]={VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM};
    swap.SurfaceFormat=ImGui_ImplVulkanH_SelectSurfaceFormat(physical,swap.Surface,requested,2,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    const VkPresentModeKHR modes[]={VK_PRESENT_MODE_MAILBOX_KHR,VK_PRESENT_MODE_FIFO_KHR};
    swap.PresentMode=ImGui_ImplVulkanH_SelectPresentMode(physical,swap.Surface,modes,2);
    int width{},height{};glfwGetFramebufferSize(window,&width,&height);resizeSwapchain(width,height);
    std::printf("GPU: %s | timestamp period %.3f ns | validation %s\n",properties.deviceName,properties.limits.timestampPeriod,validationActive?"on":"unavailable");
}
void Gpu::initializeGui(){
    ImGui_ImplGlfw_InitForVulkan(window,true);ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion=VK_API_VERSION_1_1;ii.Instance=instance;ii.PhysicalDevice=physical;ii.Device=device;
    ii.QueueFamily=queueFamily;ii.Queue=queue;ii.DescriptorPool=descriptors;ii.RenderPass=swap.RenderPass;
    ii.MinImageCount=2;ii.ImageCount=swap.ImageCount;ii.MSAASamples=VK_SAMPLE_COUNT_1_BIT;ii.CheckVkResultFn=vkCheck;
    if(!ImGui_ImplVulkan_Init(&ii))throw std::runtime_error("ImGui Vulkan initialization failed");
}
void Gpu::resizeSwapchain(int width,int height){
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance,physical,device,&swap,queueFamily,nullptr,width,height,2);
    swap.FrameIndex=0;swap.SemaphoreIndex=0;rebuild=false;
}
VkCommandBuffer Gpu::begin(){
    int width{},height{};glfwGetFramebufferSize(window,&width,&height);
    if(width<=0||height<=0)return VK_NULL_HANDLE;
    if(rebuild||swap.Width!=width||swap.Height!=height)resizeSwapchain(width,height);
    auto& sem=swap.FrameSemaphores[swap.SemaphoreIndex];
    auto result=vkAcquireNextImageKHR(device,swap.Swapchain,UINT64_MAX,sem.ImageAcquiredSemaphore,VK_NULL_HANDLE,&swap.FrameIndex);
    if(result==VK_ERROR_OUT_OF_DATE_KHR){rebuild=true;return VK_NULL_HANDLE;}
    if(result==VK_SUBOPTIMAL_KHR)rebuild=true;else vkCheck(result);
    auto& frame=swap.Frames[swap.FrameIndex];
    vkCheck(vkWaitForFences(device,1,&frame.Fence,VK_TRUE,UINT64_MAX));
    vkCheck(vkResetFences(device,1,&frame.Fence));vkCheck(vkResetCommandPool(device,frame.CommandPool,0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(frame.CommandBuffer,&bi));return frame.CommandBuffer;
}
void Gpu::end(ImDrawData* ui){
    auto& frame=swap.Frames[swap.FrameIndex];auto& sem=swap.FrameSemaphores[swap.SemaphoreIndex];
    swap.ClearValue.color={{.035f,.045f,.06f,1}};
    VkRenderPassBeginInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};ri.renderPass=swap.RenderPass;ri.framebuffer=frame.Framebuffer;
    ri.renderArea.extent={static_cast<uint32_t>(swap.Width),static_cast<uint32_t>(swap.Height)};ri.clearValueCount=1;ri.pClearValues=&swap.ClearValue;
    vkCmdBeginRenderPass(frame.CommandBuffer,&ri,VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ui,frame.CommandBuffer);vkCmdEndRenderPass(frame.CommandBuffer);
    vkCheck(vkEndCommandBuffer(frame.CommandBuffer));
    VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.waitSemaphoreCount=1;si.pWaitSemaphores=&sem.ImageAcquiredSemaphore;
    si.pWaitDstStageMask=&waitStage;si.commandBufferCount=1;si.pCommandBuffers=&frame.CommandBuffer;
    si.signalSemaphoreCount=1;si.pSignalSemaphores=&sem.RenderCompleteSemaphore;vkCheck(vkQueueSubmit(queue,1,&si,frame.Fence));
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.waitSemaphoreCount=1;pi.pWaitSemaphores=&sem.RenderCompleteSemaphore;
    pi.swapchainCount=1;pi.pSwapchains=&swap.Swapchain;pi.pImageIndices=&swap.FrameIndex;
    auto result=vkQueuePresentKHR(queue,&pi);
    if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR)rebuild=true;else vkCheck(result);
    swap.SemaphoreIndex=(swap.SemaphoreIndex+1)%swap.SemaphoreCount;
}
uint32_t Gpu::memoryType(uint32_t mask,VkMemoryPropertyFlags flags){
    VkPhysicalDeviceMemoryProperties mp{};vkGetPhysicalDeviceMemoryProperties(physical,&mp);
    for(uint32_t i=0;i<mp.memoryTypeCount;++i)if((mask&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&flags)==flags)return i;
    throw std::runtime_error("No compatible GPU memory type");
}
VkDeviceSize Gpu::availableDeviceMemory() const {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    if(memoryBudgetAvailable)properties2.pNext=&budget;
    vkGetPhysicalDeviceMemoryProperties2(physical,&properties2);VkDeviceSize available=0;
    for(uint32_t i=0;i<properties2.memoryProperties.memoryHeapCount;++i){auto& heap=properties2.memoryProperties.memoryHeaps[i];
        if(heap.flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT){auto free=memoryBudgetAvailable?(budget.heapBudget[i]>budget.heapUsage[i]?budget.heapBudget[i]-budget.heapUsage[i]:0):heap.size*7/10;available=std::max(available,free);}}
    return available;
}
Buffer Gpu::buffer(VkDeviceSize bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags){
    Buffer b{};b.size=bytes;try{VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=bytes;bc.usage=usage;bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(device,&bc,nullptr,&b.handle));VkMemoryRequirements mr{};vkGetBufferMemoryRequirements(device,b.handle,&mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=mr.size;ma.memoryTypeIndex=memoryType(mr.memoryTypeBits,flags);
    vkCheck(vkAllocateMemory(device,&ma,nullptr,&b.memory));vkCheck(vkBindBufferMemory(device,b.handle,b.memory,0));return b;
    }catch(...){release(b);throw;}
}
Image Gpu::image(uint32_t width,uint32_t height,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect){
    Image result{};try{VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ic.imageType=VK_IMAGE_TYPE_2D;ic.format=format;ic.extent={width,height,1};
    ic.mipLevels=1;ic.arrayLayers=1;ic.samples=VK_SAMPLE_COUNT_1_BIT;ic.tiling=VK_IMAGE_TILING_OPTIMAL;ic.usage=usage;ic.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateImage(device,&ic,nullptr,&result.handle));VkMemoryRequirements mr{};vkGetImageMemoryRequirements(device,result.handle,&mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=mr.size;ma.memoryTypeIndex=memoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(device,&ma,nullptr,&result.memory));vkCheck(vkBindImageMemory(device,result.handle,result.memory,0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=result.handle;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;
    vi.subresourceRange.aspectMask=aspect;vi.subresourceRange.levelCount=1;vi.subresourceRange.layerCount=1;vkCheck(vkCreateImageView(device,&vi,nullptr,&result.view));return result;
    }catch(...){release(result);throw;}
}
void Gpu::release(Buffer& b){if(b.handle)vkDestroyBuffer(device,b.handle,nullptr);if(b.memory)vkFreeMemory(device,b.memory,nullptr);b={};}
void Gpu::release(Image& i){if(i.view)vkDestroyImageView(device,i.view,nullptr);if(i.handle)vkDestroyImage(device,i.handle,nullptr);if(i.memory)vkFreeMemory(device,i.memory,nullptr);i={};}
void Gpu::immediate(const std::function<void(VkCommandBuffer)>& record){
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=utilities;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cmd{};vkCheck(vkAllocateCommandBuffers(device,&ai,&cmd));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd,&bi));record(cmd);vkCheck(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};vkCheck(vkCreateFence(device,&fc,nullptr,&fence));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;vkCheck(vkQueueSubmit(queue,1,&si,fence));
    vkCheck(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));vkDestroyFence(device,fence,nullptr);vkFreeCommandBuffers(device,utilities,1,&cmd);
}
VkShaderModule Gpu::shader(const std::filesystem::path& file){
    auto resolved=shaderRoot/file;
    std::ifstream in(resolved,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("Shader missing: "+resolved.string());
    auto size=static_cast<size_t>(in.tellg());std::vector<uint32_t> words((size+3)/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),size);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=size;ci.pCode=words.data();VkShaderModule module{};vkCheck(vkCreateShaderModule(device,&ci,nullptr,&module));return module;
}
void Gpu::screenshotUi(ImDrawData* ui,const std::filesystem::path& file){
    vkCheck(vkDeviceWaitIdle(device));uint32_t w=swap.Width,h=swap.Height;
    auto color=image(w,h,swap.SurfaceFormat.format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    auto staging=buffer(static_cast<VkDeviceSize>(w)*h*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkAttachmentDescription attachment{};attachment.format=swap.SurfaceFormat.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
    VkSubpassDependency deps[2]{};deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rc.attachmentCount=1;rc.pAttachments=&attachment;rc.subpassCount=1;rc.pSubpasses=&sub;rc.dependencyCount=2;rc.pDependencies=deps;
    VkRenderPass pass{};vkCheck(vkCreateRenderPass(device,&rc,nullptr,&pass));
    VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=pass;fc.attachmentCount=1;fc.pAttachments=&color.view;fc.width=w;fc.height=h;fc.layers=1;
    VkFramebuffer framebuffer{};vkCheck(vkCreateFramebuffer(device,&fc,nullptr,&framebuffer));
    immediate([&](VkCommandBuffer cmd){VkClearValue clear{};clear.color={{.035f,.045f,.06f,1}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=pass;rp.framebuffer=framebuffer;rp.renderArea.extent={w,h};rp.clearValueCount=1;rp.pClearValues=&clear;
        vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);ImGui_ImplVulkan_RenderDrawData(ui,cmd);vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};vkCmdCopyImageToBuffer(cmd,color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging.handle,1,&copy);
    });
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);if(!out)throw std::runtime_error("Cannot write UI screenshot");
    out<<"P6\n"<<w<<" "<<h<<"\n255\n";void* data{};vkCheck(vkMapMemory(device,staging.memory,0,staging.size,0,&data));auto* bytes=static_cast<unsigned char*>(data);
    bool bgra=swap.SurfaceFormat.format==VK_FORMAT_B8G8R8A8_UNORM;
    for(size_t i=0;i<static_cast<size_t>(w)*h;++i){char rgb[]={static_cast<char>(bytes[i*4+(bgra?2:0)]),static_cast<char>(bytes[i*4+1]),static_cast<char>(bytes[i*4+(bgra?0:2)])};out.write(rgb,3);}
    vkUnmapMemory(device,staging.memory);vkDestroyFramebuffer(device,framebuffer,nullptr);vkDestroyRenderPass(device,pass,nullptr);release(staging);release(color);
}
void Gpu::destroy(){
    if(device&&pipelineCache){size_t bytes=0;if(vkGetPipelineCacheData(device,pipelineCache,&bytes,nullptr)==VK_SUCCESS&&bytes>32){std::vector<char> blob(bytes);if(vkGetPipelineCacheData(device,pipelineCache,&bytes,blob.data())==VK_SUCCESS){try{std::filesystem::create_directories(pipelineCachePath.parent_path());auto temporary=pipelineCachePath;temporary+=".tmp";{std::ofstream out(temporary,std::ios::binary);out.write(blob.data(),static_cast<std::streamsize>(bytes));}std::filesystem::rename(temporary,pipelineCachePath);}catch(...){}}}vkDestroyPipelineCache(device,pipelineCache,nullptr);pipelineCache={};}
    if(device){vkDeviceWaitIdle(device);ImGui_ImplVulkanH_DestroyWindow(instance,device,&swap,nullptr);vkDestroyCommandPool(device,utilities,nullptr);vkDestroyDescriptorPool(device,descriptors,nullptr);vkDestroyDevice(device,nullptr);device={};}
    if(debugMessenger)vkDestroyDebugUtilsMessengerEXT(instance,debugMessenger,nullptr);
    if(instance)vkDestroyInstance(instance,nullptr);instance={};
}
