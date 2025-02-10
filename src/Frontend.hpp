#pragma once

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>
#include <atomic>


struct Result
{
  std::string message {};
  VkResult code {VK_SUCCESS};


  Result() = default;
  Result( const std::string& message, VkResult = VK_SUCCESS );
  Result( const char* message, VkResult = VK_SUCCESS );

  bool success() const;
};


struct RenderThreadData
{
  Result result {};

  std::atomic_bool shutdownRequested {};
  std::atomic_bool errorCaught {};
  std::atomic_bool swapchainRecreationRequested {};
};


struct Frontend
{
  VkInstance instance {};

  VkDebugUtilsMessengerEXT debugMessenger {};
  PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT {};
  PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT {};

  VkPhysicalDevice physicalDevice {};
  VkDevice device {};

  GLFWwindow* window {};
  VkSurfaceKHR windowSurface {};

  struct
  {
    std::vector <VkImage> images {};
    std::vector <VkImageView> imageViews {};
    std::vector <VkFramebuffer> framebuffers {};

    std::vector <VkSemaphore> imageReadySignals {};

    VkExtent2D extent {};
    VkFormat imageFormat {};

    size_t maxConcurrentFrames {};

    VkSwapchainKHR handle {};

  } swapchain {};

  struct
  {
    struct Queue
    {
      size_t familyIndex {};
      VkQueue handle {};
    };

    Queue graphics {};
    Queue presentation {};

  } queues {};


  VkRenderPass renderPass {};

  VkPipelineLayout pipelineLayout {};
  VkPipeline graphicsPipeline {};

  VkCommandPool commandPool {};
  std::vector <VkCommandBuffer> commandBuffers {};

  std::vector <VkSemaphore> gpuCmdExecutedSignals {};
  std::vector <VkFence> cpuCmdExecutedSignals {};

  RenderThreadData* renderThreadData {};

  const VkAllocationCallbacks* allocator {};
};


Result initializeFrontend(
  Frontend&,
  VkApplicationInfo,
  const VkExtent2D& windowSize );

Result initializeWindow(
  Frontend&,
  const std::string& title,
  const VkExtent2D& windowSize );

Result findSuitablePhysicalDevice(
  Frontend& );

Result createSwapchain(
  Frontend& );

Result recreateSwapchain(
  Frontend& );

Result destroySwapchain(
  Frontend& );

Result createRenderPass(
  Frontend& );

Result createGraphicsPipeline(
  Frontend& );

Result createFramebuffers(
  Frontend& );

Result createSyncObjects(
  Frontend& );

Result createShaderModule(
  Frontend&,
  VkShaderModule&,
  const std::string& code );

Result createCommandPool(
  Frontend& );

Result createCommandBuffers(
  Frontend& );

Result createVertexBuffer(
  Frontend&,
  const std::uint64_t bufferSize,
  VkBuffer&,
  VkDeviceMemory& );

Result writeVertexBuffer(
  Frontend&,
  void* data,
  const std::uint64_t offset,
  const std::uint64_t size,
  const VkDeviceMemory );

void destroyVertexBuffer(
  Frontend&,
  VkBuffer&,
  VkDeviceMemory& );

void deinitializeFrontend(
  Frontend& );
