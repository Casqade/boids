#pragma once

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include <string>


struct Result
{
  std::string message {};
  VkResult code {VK_SUCCESS};


  Result() = default;
  Result( const std::string& message, VkResult = VK_SUCCESS );
  Result( const char* message, VkResult = VK_SUCCESS );

  bool success() const;
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
    struct Queue
    {
      size_t familyIndex {};
      VkQueue handle {};
    };

    Queue graphics {};
    Queue presentation {};

  } queues {};

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

Result deinitializeFrontend(
  Frontend& );
