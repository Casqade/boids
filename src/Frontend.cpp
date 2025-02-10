#include "Frontend.hpp"
#include "Logger.hpp"

#include <cstdint>
#include <vector>


Result::Result(
  const std::string& message,
  VkResult result )
  : message{message}
  , code{result}
{
}

Result::Result(
  const char* message,
  VkResult result )
  : Result(std::string{message}, result)
{}

bool
Result::success() const
{
  return message.empty() == true;
}


const std::vector <const char*> ValidationInstanceExtensions
{
  VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

const std::vector <const char*> ValidationInstanceLayers
{
  "VK_LAYER_KHRONOS_validation",
};

const std::vector <const char*> RequiredDeviceExtensions
{
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};


static
VKAPI_ATTR
VkBool32
VKAPI_CALL
DebugMessengerCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData )
{
  spdlog::level::level_enum logLevel {};

  std::string message {};

  switch (messageType)
  {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
      message = "[Vk-General]: {}";
      break;

    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
      message = "[Vk-Validation]: {}";
      break;

    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
      message = "[Vk-Performance]: {}";
      break;

    case VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT:
      message = "[Vk-AddressBinding]: {}";
      break;
  }

  switch (messageSeverity)
  {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      LOG_TRACE(message, pCallbackData->pMessage);
      break;

    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      LOG_INFO(message, pCallbackData->pMessage);
      break;

    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      LOG_WARN(message, pCallbackData->pMessage);
      break;

    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      LOG_ERROR(message, pCallbackData->pMessage);
      break;

    default:
      break;
  }

  return VK_FALSE;
}

void
glfwErrorCallback(
  int errorCode,
  const char* description )
{
  LOG_ERROR("[GLFW] Error {}: {}", errorCode, description);
}

Result
initializeFrontend(
  Frontend& frontend,
  VkApplicationInfo applicationInfo,
  const VkExtent2D& windowSize )
{
  glfwSetErrorCallback(glfwErrorCallback);


  if ( glfwInit() != GLFW_TRUE )
    return {"[GLFW] Failed to initialize", VK_ERROR_UNKNOWN};


  VkResult result;

  std::vector <const char*> instanceLayers {};
  std::vector <const char*> instanceExtensions {};

  {
    instanceLayers.reserve(ValidationInstanceLayers.size());

    for ( const auto& layer : ValidationInstanceLayers )
      instanceLayers.push_back(layer);
  }


  {
    std::uint32_t glfwExtensionCount;
    const auto glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    instanceExtensions.resize(
      glfwExtensionCount );

    for ( size_t i {}; i < glfwExtensionCount; ++i )
      instanceExtensions[i] = glfwExtensions[i];

    {
      instanceExtensions.reserve(
        instanceExtensions.size() + ValidationInstanceExtensions.size() );

      for ( const auto& extension : ValidationInstanceExtensions )
        instanceExtensions.push_back(extension);
    }
  }


  const VkDebugUtilsMessageSeverityFlagsEXT messageSeverities
  {
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
  };

  const VkDebugUtilsMessageTypeFlagsEXT messageTypes
  {
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT
  };

  const VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext = nullptr,
    .messageSeverity = messageSeverities,
    .messageType = messageTypes,
    .pfnUserCallback = DebugMessengerCallback,
    .pUserData = nullptr,
  };

  applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

  const VkInstanceCreateInfo instanceCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = &debugMessengerCreateInfo,
    .flags = {},
    .pApplicationInfo = &applicationInfo,
    .enabledLayerCount = instanceLayers.size(),
    .ppEnabledLayerNames = instanceLayers.data(),
    .enabledExtensionCount = instanceExtensions.size(),
    .ppEnabledExtensionNames = instanceExtensions.data(),
  };

  result = vkCreateInstance(
    &instanceCreateInfo,
    frontend.allocator,
    &frontend.instance );


  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create instance", result};


  frontend.vkCreateDebugUtilsMessengerEXT =
    (PFN_vkCreateDebugUtilsMessengerEXT)
    vkGetInstanceProcAddr(
      frontend.instance,
      "vkCreateDebugUtilsMessengerEXT" );

  frontend.vkDestroyDebugUtilsMessengerEXT =
    (PFN_vkDestroyDebugUtilsMessengerEXT)
    vkGetInstanceProcAddr(
      frontend.instance,
      "vkDestroyDebugUtilsMessengerEXT" );


  if ( frontend.vkCreateDebugUtilsMessengerEXT == nullptr ||
       frontend.vkDestroyDebugUtilsMessengerEXT == nullptr )
    return {"[Vk] Failed to create debug utils messenger: no validation extnsion present", VK_ERROR_UNKNOWN};


  result = frontend.vkCreateDebugUtilsMessengerEXT(
    frontend.instance,
    &debugMessengerCreateInfo,
    frontend.allocator,
    &frontend.debugMessenger );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to set up debug utils messenger", result};


  const auto windowInitResult = initializeWindow(
    frontend,
    applicationInfo.pApplicationName,
    windowSize );

  if ( windowInitResult.success() == false )
    return windowInitResult;


  const auto suitableDeviceFound =
    findSuitablePhysicalDevice(frontend);

  if ( suitableDeviceFound.success() == false )
    return suitableDeviceFound;


  float queuePriority {1.f};

  VkDeviceQueueCreateInfo queueCreateInfos[2]
  {
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .flags = {},
      .queueFamilyIndex = frontend.queues.graphics.index,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority,
    },
  };

  size_t queueCreateInfoCount {1};

  if ( frontend.queues.graphics.index != frontend.queues.presentation.index )
  {
    ++queueCreateInfoCount;
    queueCreateInfos[1] = queueCreateInfos[0];
    queueCreateInfos[1].queueFamilyIndex = frontend.queues.presentation.index;
  }

  const VkDeviceCreateInfo deviceCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .flags = {},
    .queueCreateInfoCount = queueCreateInfoCount,
    .pQueueCreateInfos = queueCreateInfos,
    .enabledExtensionCount = RequiredDeviceExtensions.size(),
    .ppEnabledExtensionNames = RequiredDeviceExtensions.data(),
    .pEnabledFeatures = nullptr,
  };

  result = vkCreateDevice(
    frontend.physicalDevice,
    &deviceCreateInfo,
    frontend.allocator,
    &frontend.device );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create device", result};


  vkGetDeviceQueue(
    frontend.device,
    frontend.queues.graphics.index, 1,
    &frontend.queues.graphics.handle );

  vkGetDeviceQueue(
    frontend.device,
    frontend.queues.presentation.index, 1,
    &frontend.queues.presentation.handle );

  return {};
}

Result
findSuitablePhysicalDevice(
  Frontend& frontend )
{
  VkResult result;

  std::uint32_t physicalDeviceCount;
  std::vector <VkPhysicalDevice> physicalDevices {};

  result = vkEnumeratePhysicalDevices(
    frontend.instance,
    &physicalDeviceCount,
    nullptr );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to enumerate physical devices", result};

  physicalDevices.resize(physicalDeviceCount);

  result = vkEnumeratePhysicalDevices(
    frontend.instance,
    &physicalDeviceCount,
    physicalDevices.data() );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to enumerate physical devices", result};


  for ( auto&& physicalDevice : physicalDevices )
  {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(
      physicalDevice, &properties );


    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(
      physicalDevice, &features );


    std::uint32_t queueFamilyCount;
    std::vector <VkQueueFamilyProperties> queueFamilies {};

    vkGetPhysicalDeviceQueueFamilyProperties(
      physicalDevice,
      &queueFamilyCount,
      nullptr );

    queueFamilies.resize(queueFamilyCount);

    vkGetPhysicalDeviceQueueFamilyProperties(
      physicalDevice,
      &queueFamilyCount,
      queueFamilies.data() );


    bool foundGraphicsQueue {};
    bool foundPresentationQueue {};

    for ( size_t i {}; i < queueFamilies.size(); ++i )
    {
      VkBool32 graphicsSupported {};
      VkBool32 presentationSupported {};


      auto& flags = queueFamilies[i].queueFlags;

      if ( flags & VK_QUEUE_GRAPHICS_BIT )
        graphicsSupported = true;


      vkGetPhysicalDeviceSurfaceSupportKHR(
        physicalDevice,
        i, frontend.windowSurface,
        &presentationSupported );


      if ( graphicsSupported == true )
      {
        foundGraphicsQueue = true;
        frontend.queues.graphics.index = i;
      }

      if ( presentationSupported == true )
      {
        foundPresentationQueue = true;
        frontend.queues.presentation.index = i;
      }

      if ( foundGraphicsQueue == true &&
           foundPresentationQueue == true )
      {
        frontend.physicalDevice = physicalDevice;
        break;
      }
    }

    if ( frontend.physicalDevice == physicalDevice )
      return {};
  }

  return {"[Vk] Failed to find suitable physical device"};
}

Result
initializeWindow(
  Frontend& frontend,
  const std::string& title,
  const VkExtent2D& windowSize )
{
  VkResult result;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  frontend.window = glfwCreateWindow(
    windowSize.width, windowSize.height,
    title.c_str(),
    nullptr, nullptr );

  if ( frontend.window == nullptr )
    return {"[GLFW] Failed to create window", VK_ERROR_UNKNOWN};


  result = glfwCreateWindowSurface(
    frontend.instance,
    frontend.window,
    frontend.allocator,
    &frontend.windowSurface );

  if ( result != VK_SUCCESS )
    return {"[GLFW] Failed to create window surface", result};

  return {};
}

Result
deinitializeFrontend(
  Frontend& frontend )
{
  if ( frontend.windowSurface != VK_NULL_HANDLE )
  {
    vkDestroySurfaceKHR(
      frontend.instance,
      frontend.windowSurface,
      frontend.allocator );

    frontend.windowSurface = {};
  }

  if ( frontend.window != nullptr )
  {
    glfwDestroyWindow(frontend.window);

    frontend.window = {};
  }


  if ( frontend.device != VK_NULL_HANDLE )
  {
    vkDestroyDevice(
      frontend.device,
      frontend.allocator );

    frontend.device = {};
  }


  if ( frontend.debugMessenger != VK_NULL_HANDLE &&
       frontend.vkDestroyDebugUtilsMessengerEXT != nullptr &&
       frontend.instance != VK_NULL_HANDLE )
  {
    frontend.vkDestroyDebugUtilsMessengerEXT(
      frontend.instance,
      frontend.debugMessenger,
      frontend.allocator );

    frontend.debugMessenger = {};

    frontend.vkCreateDebugUtilsMessengerEXT = {};
    frontend.vkDestroyDebugUtilsMessengerEXT = {};
  }


  if ( frontend.instance != VK_NULL_HANDLE )
  {
    vkDestroyInstance(
      frontend.instance,
      frontend.allocator );

    frontend.instance = {};
  }


  glfwSetErrorCallback(nullptr);


  return {};
}
