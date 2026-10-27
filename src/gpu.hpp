#pragma once
#include <volk.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

void vkCheck(VkResult value);
struct Buffer {
    VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{};
};
struct Image {
    VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{};
};
class Gpu {
public:
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t queueFamily{};
    VkPhysicalDeviceProperties properties{};
    VkDescriptorPool descriptors{};
    VkCommandPool utilities{};
    VkPipelineCache pipelineCache{};std::filesystem::path pipelineCachePath;
    ImGui_ImplVulkanH_Window swap{};
    GLFWwindow* window{};
    bool rebuild{};
    bool timestamps{};
    uint32_t validationErrors{};
    bool validationActive{};
    bool memoryBudgetAvailable{};
    VkDebugUtilsMessengerEXT debugMessenger{};
    std::filesystem::path shaderRoot;

    void initialize(GLFWwindow* nativeWindow);
    void initializeGui();
    VkCommandBuffer begin();
    void end(ImDrawData* ui);
    void destroy();
    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);
    Image image(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void release(Buffer& value);
    void release(Image& value);
    void immediate(const std::function<void(VkCommandBuffer)>& record);
    VkShaderModule shader(const std::filesystem::path& file);
    void screenshotUi(ImDrawData* ui,const std::filesystem::path& file);
    uint32_t memoryType(uint32_t mask, VkMemoryPropertyFlags flags);
    VkDeviceSize availableDeviceMemory() const;
private:
    void resizeSwapchain(int width,int height);
};
