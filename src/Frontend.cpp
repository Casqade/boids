#include "Frontend.hpp"
#include "Logger.hpp"

#include <cstdint>
#include <fstream>
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

static void
glfwErrorCallback(
  int errorCode,
  const char* description )
{
  LOG_ERROR("[GLFW] Error {}: {}", errorCode, description);
}

static void
framebufferResizedCallback(
  GLFWwindow* window,
  int width, int height )
{
  const auto frontend = static_cast <Frontend*> (
    glfwGetWindowUserPointer(window) );

  assert(frontend != nullptr);
  assert(frontend->renderThreadData != nullptr);

  frontend->renderThreadData->swapchainRecreationRequested.store(true);
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
      .queueFamilyIndex = frontend.queues.graphics.familyIndex,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority,
    },
  };

  size_t queueCreateInfoCount {1};

  if ( frontend.queues.graphics.familyIndex != frontend.queues.presentation.familyIndex )
  {
    ++queueCreateInfoCount;
    queueCreateInfos[1] = queueCreateInfos[0];
    queueCreateInfos[1].queueFamilyIndex = frontend.queues.presentation.familyIndex;
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
    frontend.queues.graphics.familyIndex, 0,
    &frontend.queues.graphics.handle );

  vkGetDeviceQueue(
    frontend.device,
    frontend.queues.presentation.familyIndex, 0,
    &frontend.queues.presentation.handle );


  Result subroutineResult;


  subroutineResult = createSwapchain(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


  subroutineResult = createRenderPass(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


  subroutineResult = createGraphicsPipeline(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


  subroutineResult = createFramebuffers(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


  subroutineResult = createSyncObjects(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


  subroutineResult = createCommandPool(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


  subroutineResult = createCommandBuffers(frontend);

  if ( subroutineResult.success() == false )
    return subroutineResult;


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


  for ( const auto& physicalDevice : physicalDevices )
  {
    std::uint32_t extensionCount {};
    vkEnumerateDeviceExtensionProperties(
      physicalDevice, nullptr,
      &extensionCount, nullptr );

    std::vector <VkExtensionProperties> deviceExtensions(
      extensionCount );

    vkEnumerateDeviceExtensionProperties(
      physicalDevice, nullptr,
      &extensionCount, deviceExtensions.data() );


    bool extensionsSupported {true};

    for ( const auto& requiredExtension : RequiredDeviceExtensions )
    {
      const auto extensionIter = std::find_if(
        deviceExtensions.cbegin(), deviceExtensions.cend(),
        [requiredExtension] ( const VkExtensionProperties& extension )
        {
          return std::strcmp(requiredExtension, extension.extensionName) == 0;
        });

      if ( extensionIter == deviceExtensions.end() )
      {
        extensionsSupported = false;
        break;
      }
    }

    if ( extensionsSupported == false )
      continue;


    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(
      physicalDevice, &properties );


    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(
      physicalDevice, &features );


    std::uint32_t surfaceFormatCount;

    vkGetPhysicalDeviceSurfaceFormatsKHR(
      physicalDevice, frontend.windowSurface,
      &surfaceFormatCount, nullptr );

    std::uint32_t presentModeCount;

    vkGetPhysicalDeviceSurfacePresentModesKHR(
      physicalDevice, frontend.windowSurface,
      &presentModeCount, nullptr );


    const bool swapChainSupported =
      surfaceFormatCount != 0 &&
      presentModeCount != 0;


    if ( swapChainSupported == false )
      continue;


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
        frontend.queues.graphics.familyIndex = i;
      }

      if ( presentationSupported == true )
      {
        foundPresentationQueue = true;
        frontend.queues.presentation.familyIndex = i;
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
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  frontend.window = glfwCreateWindow(
    windowSize.width, windowSize.height,
    title.c_str(),
    nullptr, nullptr );

  if ( frontend.window == nullptr )
    return {"[GLFW] Failed to create window", VK_ERROR_UNKNOWN};


  glfwSetWindowUserPointer(
    frontend.window, &frontend );

  glfwSetFramebufferSizeCallback(
    frontend.window, framebufferResizedCallback );


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
createSwapchain(
  Frontend& frontend )
{
  VkResult result;


  std::uint32_t surfaceFormatCount;
  std::vector <VkSurfaceFormatKHR> surfaceFormats {};

  vkGetPhysicalDeviceSurfaceFormatsKHR(
    frontend.physicalDevice, frontend.windowSurface,
    &surfaceFormatCount, nullptr );

  surfaceFormats.resize(surfaceFormatCount);

  vkGetPhysicalDeviceSurfaceFormatsKHR(
    frontend.physicalDevice, frontend.windowSurface,
    &surfaceFormatCount, surfaceFormats.data() );


  std::uint32_t presentModeCount;
  std::vector <VkPresentModeKHR> presentModes {};

  vkGetPhysicalDeviceSurfacePresentModesKHR(
    frontend.physicalDevice, frontend.windowSurface,
    &presentModeCount, nullptr );

  presentModes.resize(presentModeCount);

  vkGetPhysicalDeviceSurfacePresentModesKHR(
    frontend.physicalDevice, frontend.windowSurface,
    &presentModeCount, presentModes.data() );


  VkSurfaceCapabilitiesKHR surfaceCapabilities {};

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    frontend.physicalDevice, frontend.windowSurface,
    &surfaceCapabilities );


  if ( surfaceFormatCount == 0 ||
       presentModeCount == 0 )
    return {"[Vk] Failed to create swapchain - ", VK_ERROR_UNKNOWN};


  auto surfaceFormat = surfaceFormats.front();;

  for ( const auto& supportedFormat : surfaceFormats )
  {
    if ( supportedFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
         supportedFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
    {
      surfaceFormat = supportedFormat;
      break;
    }
  }


  auto presentMode = VK_PRESENT_MODE_FIFO_KHR;

  for ( const auto& mode : presentModes )
  {
    if ( mode == VK_PRESENT_MODE_MAILBOX_KHR )
    {
      presentMode = mode;
      break;
    }
  }


  auto extent = surfaceCapabilities.currentExtent;

  if ( extent.width == std::numeric_limits <std::uint32_t>::max() )
  {
    int width;
    int height;

    glfwGetFramebufferSize(
      frontend.window,
      &width, &height );

    extent =
    {
      std::clamp(
        static_cast <std::uint32_t> (width),
        surfaceCapabilities.minImageExtent.width,
        surfaceCapabilities.maxImageExtent.width ),

      std::clamp(
        static_cast <std::uint32_t> (height),
        surfaceCapabilities.minImageExtent.height,
        surfaceCapabilities.maxImageExtent.height ),
    };
  }


  const auto requestedImageCount = std::clamp(
    surfaceCapabilities.minImageCount + 1,
    surfaceCapabilities.minImageCount,
    surfaceCapabilities.maxImageCount > 0
      ? surfaceCapabilities.maxImageCount
      : surfaceCapabilities.minImageCount + 1 );


  VkSwapchainCreateInfoKHR swapchainCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = frontend.windowSurface,
    .minImageCount = requestedImageCount,
    .imageFormat = surfaceFormat.format,
    .imageColorSpace = surfaceFormat.colorSpace,
    .imageExtent = extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = surfaceCapabilities.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = presentMode,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE,
  };

  const std::uint32_t queueFamilyIndices[]
  {
    frontend.queues.graphics.familyIndex,
    frontend.queues.presentation.familyIndex,
  };

  if ( frontend.queues.graphics.familyIndex == frontend.queues.presentation.familyIndex )
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

  else
  {
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchainCreateInfo.queueFamilyIndexCount = 2;
    swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
  }

  result = vkCreateSwapchainKHR(
    frontend.device,
    &swapchainCreateInfo,
    frontend.allocator,
    &frontend.swapchain.handle );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create swapchain", result};


  std::uint32_t actualImageCount;

  result = vkGetSwapchainImagesKHR(
    frontend.device, frontend.swapchain.handle,
    &actualImageCount, nullptr );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to get swapchain images", result};

  frontend.swapchain.images.resize(actualImageCount);
  frontend.swapchain.imageViews.resize(actualImageCount);

  result = vkGetSwapchainImagesKHR(
    frontend.device, frontend.swapchain.handle,
    &actualImageCount, frontend.swapchain.images.data() );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to get swapchain images", result};

  frontend.swapchain.extent = extent;
  frontend.swapchain.imageFormat = surfaceFormat.format;


  for ( size_t i {}; i < actualImageCount; ++i )
  {
    const VkImageViewCreateInfo imageViewCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = frontend.swapchain.images[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = frontend.swapchain.imageFormat,
      .components
      {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .subresourceRange
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      }
    };

    result = vkCreateImageView(
      frontend.device,
      &imageViewCreateInfo,
      frontend.allocator,
      &frontend.swapchain.imageViews[i] );

    if ( result != VK_SUCCESS )
      return {"[Vk] Failed to create image view", result};
  }


  return {};
}

Result
recreateSwapchain(
  Frontend& frontend )
{
  int width {};
  int height {};

  glfwGetFramebufferSize(
    frontend.window,
    &width, &height );

  while ( width == 0 || height == 0 )
  {
    glfwGetFramebufferSize(
      frontend.window,
      &width, &height );

    glfwWaitEvents();
  }


  vkDeviceWaitIdle(frontend.device);

  destroySwapchain(frontend);
  createSwapchain(frontend);
  createFramebuffers(frontend);


  return {};
}

Result
destroySwapchain(
  Frontend& frontend )
{
  for ( const auto& framebuffer : frontend.swapchain.framebuffers )
  {
    vkDestroyFramebuffer(
      frontend.device,
      framebuffer,
      frontend.allocator );
  }

  for ( const auto& imageView : frontend.swapchain.imageViews )
  {
    vkDestroyImageView(
      frontend.device, imageView,
      frontend.allocator );
  }

  if ( frontend.swapchain.handle != VK_NULL_HANDLE )
  {
    vkDestroySwapchainKHR(
      frontend.device,
      frontend.swapchain.handle,
      frontend.allocator );
  }


  frontend.swapchain.imageViews.clear();
  frontend.swapchain.framebuffers.clear();
  frontend.swapchain.handle = VK_NULL_HANDLE;


  return {};
}

Result
createRenderPass(
  Frontend& frontend )
{
  const VkAttachmentDescription colorAttachment
  {
    .format = frontend.swapchain.imageFormat,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  const VkAttachmentReference colorAttachmentReference
  {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  const VkSubpassDescription subpass
  {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorAttachmentReference,
  };

  const VkSubpassDependency subpassDependency
  {
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };

  const VkRenderPassCreateInfo renderPassCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .flags = {},
    .attachmentCount = 1,
    .pAttachments = &colorAttachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &subpassDependency,
  };

  const auto result = vkCreateRenderPass(
    frontend.device,
    &renderPassCreateInfo,
    frontend.allocator,
    &frontend.renderPass );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create render pass", result};


  return {};
}

static Result
readFile(
  const std::string& filename,
  std::string& buffer )
{
  std::ifstream file
  {
    filename,
    std::ios::ate | std::ios::binary
  };

  if ( file.is_open() == false )
    return {"[IO] Failed to open file '" + filename + "'", VK_ERROR_UNKNOWN};


  const size_t fileSize = file.tellg();

  buffer.resize(fileSize);

  file.seekg(0);
  file.read( buffer.data(), fileSize );
  file.close();


  return {};
}

Result
createGraphicsPipeline(
  Frontend& frontend )
{
  VkResult result;
  Result shaderModuleCreateResult;
  Result readFileResult;


  std::string vertexShaderCode {};

  readFileResult = readFile(
    "shaders/triangle_vs.spv",
    vertexShaderCode );

  if ( readFileResult.success() == false )
    return readFileResult;


  std::string fragmentShaderCode {};

  readFileResult = readFile(
    "shaders/triangle_fs.spv",
    fragmentShaderCode );

  if ( readFileResult.success() == false )
    return readFileResult;


  VkShaderModule vertexShaderModule;

  shaderModuleCreateResult = createShaderModule(
    frontend,
    vertexShaderModule,
    vertexShaderCode );

  if ( shaderModuleCreateResult.success() == false )
    return shaderModuleCreateResult;


  VkShaderModule fragmentShaderModule;

  shaderModuleCreateResult = createShaderModule(
    frontend,
    fragmentShaderModule,
    fragmentShaderCode );

  if ( shaderModuleCreateResult.success() == false )
    return shaderModuleCreateResult;


  const VkPipelineShaderStageCreateInfo shaderStages[]
  {
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vertexShaderModule,
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = fragmentShaderModule,
      .pName = "main",
    },
  };

  const VkVertexInputBindingDescription inputBindingDescriptions[]
  {
    {
      .binding = 0,
      .stride = 12,
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
    {
      .binding = 1,
      .stride = 12,
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
  };

  const VkVertexInputAttributeDescription inputAttributeDescriptions[]
  {
    {
      .location = 0,
      .binding = 0,
      .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = 0,
    },
    {
      .location = 1,
      .binding = 1,
      .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = 0,
    },
  };

  const VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 2,
    .pVertexBindingDescriptions = inputBindingDescriptions,
    .vertexAttributeDescriptionCount = 2,
    .pVertexAttributeDescriptions = inputAttributeDescriptions,
  };

  const VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };

  const VkViewport viewport
  {
    .x = 0.f,
    .y = 0.f,
    .width = static_cast <float> (frontend.swapchain.extent.width),
    .height = static_cast <float> (frontend.swapchain.extent.height),
    .minDepth = 0.f,
    .maxDepth = 1.f,
  };

  const VkRect2D scissor
  {
    .offset = {},
    .extent = frontend.swapchain.extent,
  };

  const VkDynamicState dynamicPipelineStates[]
  {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };

  const VkPipelineDynamicStateCreateInfo dynamicPipelineStateCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = dynamicPipelineStates,
  };

  const VkPipelineViewportStateCreateInfo viewportStateCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports = &viewport,
    .scissorCount = 1,
    .pScissors = &scissor,
  };

  const VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.f,
    .depthBiasClamp = 0.f,
    .depthBiasSlopeFactor = 0.f,
    .lineWidth = 1.f,
  };

  const VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 1.f,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE,
  };

  const VkPipelineColorBlendAttachmentState colorBlendAttachmentState
  {
    .blendEnable = VK_FALSE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT,
  };

  const VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .logicOp = VK_LOGIC_OP_COPY,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachmentState,
    .blendConstants = { 0.f, 0.f, 0.f, 0.f},
  };

  const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 0,
    .pSetLayouts = nullptr,
    .pushConstantRangeCount = 0,
    .pPushConstantRanges = nullptr,
  };

  result = vkCreatePipelineLayout(
    frontend.device,
    &pipelineLayoutCreateInfo,
    frontend.allocator,
    &frontend.pipelineLayout );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create pipeline layout", result};


  const VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .flags = {},
    .stageCount = 2,
    .pStages = shaderStages,
    .pVertexInputState = &vertexInputStateCreateInfo,
    .pInputAssemblyState = &inputAssemblyCreateInfo,
    .pViewportState = &viewportStateCreateInfo,
    .pRasterizationState = &rasterizationStateCreateInfo,
    .pMultisampleState = &multisampleStateCreateInfo,
    .pDepthStencilState = nullptr,
    .pColorBlendState = &colorBlendStateCreateInfo,
    .pDynamicState = &dynamicPipelineStateCreateInfo,
    .layout = frontend.pipelineLayout,
    .renderPass = frontend.renderPass,
    .subpass = 0,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1,
  };

  result = vkCreateGraphicsPipelines(
    frontend.device, VK_NULL_HANDLE, 1,
    &graphicsPipelineCreateInfo,
    frontend.allocator,
    &frontend.graphicsPipeline );


  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create graphics pipeline", result};


  vkDestroyShaderModule(
    frontend.device,
    vertexShaderModule,
    frontend.allocator );

  vkDestroyShaderModule(
    frontend.device,
    fragmentShaderModule,
    frontend.allocator );


  return {};
}

Result
createFramebuffers(
  Frontend& frontend )
{
  frontend.swapchain.framebuffers.resize(
    frontend.swapchain.imageViews.size() );

  for ( size_t i {}; i < frontend.swapchain.imageViews.size(); ++i )
  {
    const VkFramebufferCreateInfo framebufferCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .flags = {},
      .renderPass = frontend.renderPass,
      .attachmentCount = 1,
      .pAttachments = &frontend.swapchain.imageViews[i],
      .width = frontend.swapchain.extent.width,
      .height = frontend.swapchain.extent.height,
      .layers = 1,
    };

    const auto result = vkCreateFramebuffer(
      frontend.device,
      &framebufferCreateInfo,
      frontend.allocator,
      &frontend.swapchain.framebuffers[i] );

    if ( result != VK_SUCCESS )
      return {"[Vk] Failed to create framebuffer", result};
  }


  return {};
}

Result
createShaderModule(
  Frontend& frontend,
  VkShaderModule& shaderModule,
  const std::string& code )
{
  const VkShaderModuleCreateInfo shaderModuleCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = code.size(),
    .pCode = reinterpret_cast <const uint32_t*> (code.data()),
  };

  const auto result = vkCreateShaderModule(
    frontend.device,
    &shaderModuleCreateInfo,
    frontend.allocator,
    &shaderModule );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create shader module", result};


  return {};
}

Result
createCommandPool(
  Frontend& frontend )
{
  const VkCommandPoolCreateInfo cmdPoolCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = frontend.queues.graphics.familyIndex,
  };

  const auto result = vkCreateCommandPool(
    frontend.device,
    &cmdPoolCreateInfo,
    frontend.allocator,
    &frontend.commandPool );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create command pool", result};


  return {};
}

Result
createCommandBuffers(
  Frontend& frontend )
{
  frontend.commandBuffers.resize(frontend.swapchain.maxConcurrentFrames);

  const VkCommandBufferAllocateInfo cmdBufferAllocateInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = frontend.commandPool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = static_cast <std::uint32_t> (frontend.commandBuffers.size()),
  };

  const auto result = vkAllocateCommandBuffers(
    frontend.device,
    &cmdBufferAllocateInfo,
    frontend.commandBuffers.data() );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to allocate command buffers", result};


  return {};
}

Result
createVertexBuffer(
  Frontend& frontend,
  const std::uint64_t bufferSize,
  VkBuffer& buffer,
  VkDeviceMemory& bufferMemory )
{
  VkResult result;


  const VkBufferCreateInfo bufferCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = bufferSize,
    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  result = vkCreateBuffer(
    frontend.device,
    &bufferCreateInfo,
    frontend.allocator,
    &buffer );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to create vertex buffer", result};


  VkMemoryRequirements memoryRequirements;
  vkGetBufferMemoryRequirements(
    frontend.device, buffer,
    &memoryRequirements );

  const auto memoryPropertyFlags =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;


  VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(
    frontend.physicalDevice,
    &memoryProperties );

  bool memoryTypeAvailable {};
  size_t memoryTypeIndex {};

  for ( size_t i {}; i < memoryProperties.memoryTypeCount; ++i )
  {
    if ( memoryRequirements.memoryTypeBits & (1 << i) &&
         (memoryProperties.memoryTypes[i].propertyFlags & memoryPropertyFlags ) ==
           memoryPropertyFlags )
    {
      memoryTypeAvailable = true;
      memoryTypeIndex = i;
      break;
    }
  }

  if ( memoryTypeAvailable == false )
    return {"[Vk] Failed to create vertex buffer - requested memory type is not available", VK_ERROR_UNKNOWN};

  const VkMemoryAllocateInfo allocateInfo
  {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = memoryRequirements.size,
    .memoryTypeIndex = memoryTypeIndex,
  };

  result = vkAllocateMemory(
    frontend.device, &allocateInfo,
    frontend.allocator, &bufferMemory );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to allocate vertex buffer memory", result};


  result = vkBindBufferMemory(
    frontend.device,
    buffer, bufferMemory,
    0 );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to bind vertex buffer memory", result};


  return {};
}

void
destroyVertexBuffer(
  Frontend& frontend,
  VkBuffer& buffer,
  VkDeviceMemory& bufferMemory )
{
  vkDestroyBuffer(
    frontend.device,
    buffer,
    frontend.allocator );

  vkFreeMemory(
    frontend.device,
    bufferMemory,
    frontend.allocator );

  buffer = {};
  bufferMemory = {};
}

Result
writeVertexBuffer(
  Frontend& frontend,
  void* data,
  const std::uint64_t offset,
  const std::uint64_t size,
  const VkDeviceMemory deviceMemory )
{
  VkResult result;


  void* mappedMemory;

  result = vkMapMemory(
    frontend.device, deviceMemory,
    offset, size, 0,
    &mappedMemory );

  if ( result != VK_SUCCESS )
    return {"[Vk] Failed to map vertex buffer memory", result};


  std::memcpy(
    mappedMemory,
    data,
    size );

  vkUnmapMemory(frontend.device, deviceMemory);


  return {};
}

Result
createSyncObjects(
  Frontend& frontend )
{
  VkResult result;


  const auto maxConcurrentFrames = frontend.swapchain.maxConcurrentFrames;

  frontend.swapchain.imageReadySignals.resize(maxConcurrentFrames);
  frontend.gpuCmdExecutedSignals.resize(maxConcurrentFrames);
  frontend.cpuCmdExecutedSignals.resize(maxConcurrentFrames);


  const VkSemaphoreCreateInfo semaphoreCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  const VkFenceCreateInfo fenceCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };


  for ( size_t i {}; i < maxConcurrentFrames; ++i )
  {
    result = vkCreateSemaphore(
      frontend.device, &semaphoreCreateInfo,
      frontend.allocator, &frontend.swapchain.imageReadySignals[i] );

    if ( result != VK_SUCCESS )
      return {"[Vk] Failed to create semaphore", result};


    result = vkCreateSemaphore(
      frontend.device, &semaphoreCreateInfo,
      frontend.allocator, &frontend.gpuCmdExecutedSignals[i] );

    if ( result != VK_SUCCESS )
      return {"[Vk] Failed to create semaphore", result};


    result = vkCreateFence(
      frontend.device, &fenceCreateInfo,
      frontend.allocator, &frontend.cpuCmdExecutedSignals[i] );

    if ( result != VK_SUCCESS )
      return {"[Vk] Failed to create fence", result};
  }


  return {};
}

void
deinitializeFrontend(
  Frontend& frontend )
{
  for ( const auto& semaphore : frontend.swapchain.imageReadySignals )
  {
    vkDestroySemaphore(
      frontend.device,
      semaphore,
      frontend.allocator);
  }

  for ( const auto& semaphore : frontend.gpuCmdExecutedSignals )
  {
    vkDestroySemaphore(
      frontend.device,
      semaphore,
      frontend.allocator);
  }

  for ( const auto& fence : frontend.cpuCmdExecutedSignals )
  {
    vkDestroyFence(
      frontend.device,
      fence,
      frontend.allocator);
  }


  if ( frontend.commandBuffers.empty() == false )
  {
    vkFreeCommandBuffers(
      frontend.device,
      frontend.commandPool,
      frontend.commandBuffers.size(),
      frontend.commandBuffers.data() );
  }


  if ( frontend.commandPool != VK_NULL_HANDLE )
  {
    vkDestroyCommandPool(
      frontend.device,
      frontend.commandPool,
      frontend.allocator );
  }


  if ( frontend.graphicsPipeline != VK_NULL_HANDLE )
  {
    vkDestroyPipeline(
      frontend.device,
      frontend.graphicsPipeline,
      frontend.allocator );
  }


  if ( frontend.pipelineLayout != VK_NULL_HANDLE )
  {
    vkDestroyPipelineLayout(
      frontend.device,
      frontend.pipelineLayout,
      frontend.allocator );
  }


  if ( frontend.renderPass != VK_NULL_HANDLE )
  {
    vkDestroyRenderPass(
      frontend.device,
      frontend.renderPass,
      frontend.allocator );
  }


  destroySwapchain(frontend);


  if ( frontend.windowSurface != VK_NULL_HANDLE )
  {
    vkDestroySurfaceKHR(
      frontend.instance,
      frontend.windowSurface,
      frontend.allocator );
  }


  if ( frontend.window != nullptr )
    glfwDestroyWindow(frontend.window);


  if ( frontend.device != VK_NULL_HANDLE )
  {
    vkDestroyDevice(
      frontend.device,
      frontend.allocator );
  }


  if ( frontend.debugMessenger != VK_NULL_HANDLE &&
       frontend.vkDestroyDebugUtilsMessengerEXT != nullptr &&
       frontend.instance != VK_NULL_HANDLE )
  {
    frontend.vkDestroyDebugUtilsMessengerEXT(
      frontend.instance,
      frontend.debugMessenger,
      frontend.allocator );
  }


  if ( frontend.instance != VK_NULL_HANDLE )
  {
    vkDestroyInstance(
      frontend.instance,
      frontend.allocator );
  }


  frontend = {};

  glfwSetErrorCallback(nullptr);
}
