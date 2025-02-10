#include "Allocators.hpp"
#include "Containers.hpp"
#include "Frontend.hpp"
#include "Vector.hpp"
#include "Logger.hpp"
#include "ThreadPool.hpp"
#include "ThreadAffinity.hpp"
#include "PerformanceCounter.hpp"

#include <cassert>
#include <chrono>

#include <random>
#include <iostream>
#include <functional>


struct BoidRuleset
{
  struct
  {
    float alignment {0.1f};
    float coherence {0.1f};
    float separation {0.1f};

  } weights {};

  float obstacleAvoidanceDistance {0.15f};
  float maxSpeed {0.1f};
};

struct FrameData
{
  Array <Vector3> position {};
  Array <Vector3> velocity {};
};

struct BoidData
{
  Array <std::size_t> cellId {};

  Array <Vector3> position {};
  Array <Vector3> velocity {};

  Array <Vector3> obstacleAvoidance {};
  Array <Vector3> alignment {};
  Array <Vector3> coherence {};
  Array <Vector3> separation {};
};

struct CellData
{
  Array <std::size_t> boidCount {};
  Array <Vector3> averagePosition {};
  Array <Vector3> averageVelocity {};
};


std::size_t
hashPos(
  const Vector3& pos,
  const std::size_t cellCount )
{
  return
    static_cast <std::size_t> (pos.x * cellCount) +
    static_cast <std::size_t> (pos.y * cellCount) * cellCount +
    static_cast <std::size_t> (pos.z * cellCount) * cellCount * cellCount;
}


Vector3::value_type
getAvoidance(
  const Vector3::value_type coordinate,
  const Vector3::value_type margin )
{
  if ( coordinate > 1 - margin )
    return -1;

  if ( coordinate < margin )
    return 1;

  return {};
}

using Clock = std::chrono::high_resolution_clock;


namespace
{
enum PerfMarker : size_t
{
  ResetTask,
  HashPosTask,
  Summing,
  RulesCalc,
  Transform,
  Rendering,
  Total,

  PositionSumTask,
  VelocitySumTask,
  BoidCountSumTask,

  AlignmentTask,
  CoherenceTask,
  SeparationTask,
  ObstacleAvoidanceTask,

  TransformBoidsTask,

  Count,
};

TimePerfCounter timeCounter [PerfMarker::Count] {};
CyclePerfCounter cycleCounter [PerfMarker::Count] {};
}

void
printElapsedTime(
  const Clock::time_point& from,
  const Clock::time_point& to,
  const std::string& name )
{
  const auto elapsedUs =
    std::chrono::duration_cast <std::chrono::microseconds> (
      to - from).count();

  std::cout << name + " took " + std::to_string(elapsedUs) + "us\n";
}

void
printElapsedTime(
  const PerfMarker markerId,
  const std::string& name )
{
  const auto elapsedUs =
    timeCounter[markerId].average.count();

  std::cout <<
    name + " took " +
    std::to_string(elapsedUs) + " us\n";
}

static int
handleFrontendError(
  Frontend& frontend,
  const Result& result )
{
  if ( result.code != VK_ERROR_UNKNOWN )
    LOG_ERROR("Vulkan error {}: {}", (int) result.code, result.message);
  else
    LOG_ERROR(result.message);

  deinitializeFrontend(frontend);

  destroyLogger();

  return result.code;
}


int
main(
  int argc,
  char* argv[] )
{
  createLogger("Boids");

  cqdeVk::Allocator vkAllocator {};

  const VkAllocationCallbacks vkAllocatorCallbacks
  {
    .pUserData = &vkAllocator,
    .pfnAllocation = cqdeVk::allocate,
    .pfnReallocation = cqdeVk::reallocate,
    .pfnFree = cqdeVk::free,
    .pfnInternalAllocation = cqdeVk::internalAllocate,
    .pfnInternalFree = cqdeVk::internalFree,
  };

  RenderThreadData renderThreadData {};

  Frontend frontend
  {
    .swapchain = {.maxConcurrentFrames = 3},
    .renderThreadData = &renderThreadData,
    .allocator = &vkAllocatorCallbacks,
  };

  const VkApplicationInfo applicationInfo
  {
    .pApplicationName = "Boids",
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName = "Boids",
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_3,
  };

  auto result = initializeFrontend(
    frontend,
    applicationInfo,
    VkExtent2D{800, 600} );

  if ( result.success() == false )
    return handleFrontendError(frontend, result);


  const std::size_t threadCount {5};
  const std::size_t taskBufferSize = threadCount * 3; // we don't have more than 3 concurrent parallel_fors
  const std::size_t boidCount {400'000};
  const std::size_t cellPerAxisCount {100};
  const std::size_t cellCount =
    std::pow(cellPerAxisCount, std::size_t{3});

  const std::size_t maxOccupiedCellCount =
    std::min(boidCount, cellCount);

  const auto threadPoolMemoryFootprint =
    sizeof(ThreadPool::ThreadEntry) * threadCount +
    CacheLineSize +
    sizeof(ThreadPool::TaskStorage) * taskBufferSize;

  const auto frameDataMemoryFootprint =
    sizeof(Vector3) +
    sizeof(Vector3);

  const auto boidMemoryFootprint =
    sizeof(std::size_t) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3);

  const auto cellMemoryFootprint =
    sizeof(std::size_t) +
    sizeof(Vector3) +
    sizeof(Vector3);

  const auto sparseCellMemoryFootprint =
    sizeof(std::size_t);

  const auto expectedAllocationsCount =
    sizeof(std::size_t) * 19;


  std::vector <VkBuffer> vertexBuffer(
    frontend.swapchain.maxConcurrentFrames );

  std::vector <VkDeviceMemory> vertexBufferMemory(
    frontend.swapchain.maxConcurrentFrames );

  for ( size_t i {}; i < vertexBuffer.size(); ++i )
  {
    result = createVertexBuffer(
      frontend, sizeof(Vector3) * boidCount,
      vertexBuffer[i],
      vertexBufferMemory[i] );

    if ( result.success() == false )
      return handleFrontendError(frontend, result);
  }


  AllocatorArena allocator {};
  allocator.reserve(
    threadPoolMemoryFootprint +
    frameDataMemoryFootprint * boidCount * 3 +
    boidMemoryFootprint * boidCount +
    cellMemoryFootprint * maxOccupiedCellCount +
    sparseCellMemoryFootprint * cellCount +
    expectedAllocationsCount );


  static float deltaTime;

  {
    auto mask = initAffinityMask();
    addCpuToAffinityMask(mask, 0);
    setThreadAffinity(mask);


    ThreadPool threadPool {};
    threadPool.init(
      allocator, taskBufferSize, threadCount, 2 );


    Swapchain boidSwapChain {};

    FrameData frameData[3]
    {
      {
        {allocator, boidCount},
        {allocator, boidCount},
      },
      {
        {allocator, boidCount},
        {allocator, boidCount},
      },
      {
        {allocator, boidCount},
        {allocator, boidCount},
      },
    };


    BoidData boids
    {
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
    };

    CellData occupiedCells
    {
      {allocator, maxOccupiedCellCount},
      {allocator, maxOccupiedCellCount},
      {allocator, maxOccupiedCellCount},
    };


    Array <std::size_t> cells {allocator, cellCount};

    BoidRuleset rules {};

    std::random_device rd {};
    std::uniform_real_distribution dist(0.f, 1.f);
    std::minstd_rand0 engine
    {
//      rd()
    };

    const auto posInitTask =
    [&boids, &dist, &engine] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        boids.position[i] = { dist(engine), dist(engine), dist(engine) };
//        boids.velocity[i] = { dist(engine), dist(engine), dist(engine) };
      }
    };


    const auto copyPositionsTask =
    [&boids, &frameData, &boidSwapChain]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      const auto bufferIndex = boidSwapChain.back();
      auto& framePositions = frameData[bufferIndex].position;

      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        framePositions[i] = boids.position[i];
    };

    const auto copyVelocitiesTask =
    [&boids, &frameData, &boidSwapChain]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      const auto bufferIndex = boidSwapChain.back();
      auto& frameVelocities = frameData[bufferIndex].velocity;

      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        frameVelocities[i] = boids.velocity[i];
    };


    const auto resetCellsTask =
    [&cells, tombstone = maxOccupiedCellCount]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      std::fill_n(
        cells.data() + rangeStart,
        rangeEnd - rangeStart,
        tombstone );
    };

    const auto resetAveragePositionTask =
    [&occupiedCells]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        occupiedCells.averagePosition[i] = {};
    };

    const auto resetAverageVelocityTask =
    [&occupiedCells]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        occupiedCells.averageVelocity[i] = {};
    };

    const auto resetBoidCountTask =
    [&occupiedCells]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        occupiedCells.boidCount[i] = {};
    };


    const auto hashPosTask =
    [&boids, &cells, tombstone = maxOccupiedCellCount] ()
    {
      size_t occupiedCellCount {};

      for ( std::size_t i {}; i < boidCount; ++i )
      {
        const auto& boidPosition = boids.position[i];

//        index into sparse array
        const auto cellSparseIdx = hashPos(
          boidPosition, cellPerAxisCount );

//        index into dense array
        auto& cellDenseIdx = cells[cellSparseIdx];

        if ( cellDenseIdx == tombstone )
          cellDenseIdx = occupiedCellCount++;

        boids.cellId[i] = cellDenseIdx;
      }
    };


    const auto averagePositionSumTask =
    [&boids, &occupiedCells] ()
    {
      PERF_TIME_BEGIN(PerfMarker::PositionSumTask);

      for ( std::size_t i {}; i < boidCount; ++i )
      {
        const auto cellId = boids.cellId[i];

        const auto& boidPosition = boids.position[i];

        occupiedCells.averagePosition[cellId] += boidPosition;
      }

      PERF_TIME_END(PerfMarker::PositionSumTask);
    };

    const auto averageVelocitySumTask =
    [&boids, &occupiedCells] ()
    {
      PERF_TIME_BEGIN(PerfMarker::VelocitySumTask);

      for ( std::size_t i {}; i < boidCount; ++i )
      {
        const auto cellId = boids.cellId[i];

        const auto& boidVelocity = boids.velocity[i];

        occupiedCells.averageVelocity[cellId] += boidVelocity;
      }

      PERF_TIME_END(PerfMarker::VelocitySumTask);
    };

    const auto boidCountSumTask =
    [&boids, &occupiedCells] ()
    {
      PERF_TIME_BEGIN(PerfMarker::BoidCountSumTask);

      for ( std::size_t i {}; i < boidCount; ++i )
      {
        const auto cellId = boids.cellId[i];

        occupiedCells.boidCount[cellId] += 1;
      }

      PERF_TIME_END(PerfMarker::BoidCountSumTask);
    };


    const auto calcObstacleAvoidanceTask =
    [&boids, &rules]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      PERF_TIME_BEGIN(PerfMarker::ObstacleAvoidanceTask);

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        const auto& position = boids.position[i];

        boids.obstacleAvoidance[i] =
        {
          getAvoidance(position.x, rules.obstacleAvoidanceDistance),
          getAvoidance(position.y, rules.obstacleAvoidanceDistance),
          getAvoidance(position.z, rules.obstacleAvoidanceDistance)
        };
      }

      PERF_TIME_END(PerfMarker::ObstacleAvoidanceTask);
    };

    const auto calcAlignmentTask =
    [&boids, &occupiedCells, &weights = rules.weights]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
//      PERF_TIME_BEGIN(PerfMarker::AlignmentTask);

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        const auto cellId = boids.cellId[i];

        const auto neighborCount =
          occupiedCells.boidCount[cellId];

//        assert(neighborCount > 0);

        const auto& velocity = boids.velocity[i];

        const auto& averageVelocity =
          occupiedCells.averageVelocity[cellId];

        const auto alignment =
          averageVelocity / neighborCount - velocity;

        boids.alignment[i] =
          weights.alignment *
          alignment.normalized();

        assert(boids.alignment[i].x >= -1.f);
        assert(boids.alignment[i].y >= -1.f);
        assert(boids.alignment[i].z >= -1.f);
        assert(boids.alignment[i].x <= 1.f);
        assert(boids.alignment[i].y <= 1.f);
        assert(boids.alignment[i].z <= 1.f);
      }

//      PERF_TIME_END(PerfMarker::AlignmentTask);
    };

    const auto calcCoherenceTask =
    [&boids, &occupiedCells, &weights = rules.weights]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
//      PERF_TIME_BEGIN(PerfMarker::CoherenceTask);

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        const auto cellId = boids.cellId[i];
        const auto neighborCount =
          occupiedCells.boidCount[cellId];

//        assert(neighborCount > 0);

        const auto& position = boids.position[i];

        const auto& averagePosition =
          occupiedCells.averagePosition[cellId];

        const auto coherence =
          averagePosition / neighborCount - position;

        boids.coherence[i] =
          weights.coherence *
          coherence.normalized();

        assert(boids.coherence[i].x >= -1.f);
        assert(boids.coherence[i].y >= -1.f);
        assert(boids.coherence[i].z >= -1.f);
        assert(boids.coherence[i].x <= 1.f);
        assert(boids.coherence[i].y <= 1.f);
        assert(boids.coherence[i].z <= 1.f);
      }

//      PERF_TIME_END(PerfMarker::CoherenceTask);
    };

    const auto calcSeparationTask =
    [&boids, &occupiedCells, &weights = rules.weights]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
//      PERF_TIME_BEGIN(PerfMarker::SeparationTask);

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        const auto cellId = boids.cellId[i];
        const auto neighborCount =
          occupiedCells.boidCount[cellId];

//        assert(neighborCount > 0);

        const auto& position = boids.position[i];

        const auto& averagePosition =
          occupiedCells.averagePosition[cellId];

        const auto separation =
          position - averagePosition / neighborCount;

        boids.separation[i] =
//          averagePosition;
          weights.separation *
          separation.normalized();

        assert(boids.separation[i].x >= -1.f);
        assert(boids.separation[i].y >= -1.f);
        assert(boids.separation[i].z >= -1.f);
        assert(boids.separation[i].x <= 1.f);
        assert(boids.separation[i].y <= 1.f);
        assert(boids.separation[i].z <= 1.f);
      }

//      PERF_TIME_END(PerfMarker::SeparationTask);
    };


    const auto transformBoidsTask =
    [&boids, &rules]
    ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
//      PERF_TIME_BEGIN(PerfMarker::TransformBoidsTask);

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        auto& velocity = boids.velocity[i];
        auto& position = boids.position[i];

        const auto& alignment = boids.alignment[i];
        const auto& coherence = boids.coherence[i];
        const auto& separation = boids.separation[i];
        const auto& obstacleAvoidance = boids.obstacleAvoidance[i];

        const auto heading =
          alignment + coherence + separation;

        const auto desiredVelocity =
          obstacleAvoidance.length_squared() > 0.f
            ? obstacleAvoidance.normalized()
            : heading.normalized();

        const auto prevVelocity = velocity;

        velocity =
          (velocity + (desiredVelocity - velocity) * deltaTime).normalized();

        assert(velocity.x >= -1.f);
        assert(velocity.y >= -1.f);
        assert(velocity.z >= -1.f);
        assert(velocity.x <= 1.f);
        assert(velocity.y <= 1.f);
        assert(velocity.z <= 1.f);

        assert(prevVelocity.x >= -1.f);
        assert(prevVelocity.y >= -1.f);
        assert(prevVelocity.z >= -1.f);
        assert(prevVelocity.x <= 1.f);
        assert(prevVelocity.y <= 1.f);
        assert(prevVelocity.z <= 1.f);

        position += velocity * rules.maxSpeed * deltaTime;

        assert(position.x >= 0.f);
        assert(position.y >= 0.f);
        assert(position.z >= 0.f);
        assert(position.x <= 1.f);
        assert(position.y <= 1.f);
        assert(position.z <= 1.f);
        continue;

        boids.position[i] =
        {
          std::fmod(boids.position[i].x + velocity.x * deltaTime, 1.f),
          std::fmod(boids.position[i].y + velocity.y * deltaTime, 1.f),
          std::fmod(boids.position[i].z + velocity.z * deltaTime, 1.f),
        };
      }

//      PERF_TIME_END(PerfMarker::TransformBoidsTask);
    };


    const auto renderingTask =
    [&frontend, &vertexBuffer, &vertexBufferMemory, &boidSwapChain, &frameData] ()
    {
      std::atomic_thread_fence(std::memory_order_acquire);


      VkResult result;


      std::size_t currentFrameIndex {};

      auto& renderThreadData = *frontend.renderThreadData;

      while ( renderThreadData.shutdownRequested.load() == false )
      {
        PERF_TIME_BEGIN(PerfMarker::Rendering);


        const auto cpuCmdExecutedFence =
          frontend.cpuCmdExecutedSignals[currentFrameIndex];

        const auto imageReadySignal =
          frontend.swapchain.imageReadySignals[currentFrameIndex];

        const auto gpuCmdExecutedSignal  =
          frontend.gpuCmdExecutedSignals[currentFrameIndex];

        const auto cmdBuffer =
          frontend.commandBuffers[currentFrameIndex];

        const auto framebuffer =
          frontend.swapchain.framebuffers[currentFrameIndex];


        result = vkWaitForFences(
          frontend.device, 1,
          &cpuCmdExecutedFence, VK_TRUE,
          std::numeric_limits <std::uint64_t>::max() );

        if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to wait for cpuCmdExecuted fence", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }


        std::uint32_t acquiredImageIndex {};

        result = vkAcquireNextImageKHR(
          frontend.device,
          frontend.swapchain.handle,
          std::numeric_limits <std::uint64_t>::max(),
          imageReadySignal,
          VK_NULL_HANDLE,
          &acquiredImageIndex );

        if ( result == VK_ERROR_OUT_OF_DATE_KHR )
        {
          currentFrameIndex = 0;
          renderThreadData.swapchainRecreationRequested.store(true);
          return;
        }

        if ( result != VK_SUCCESS &&
             result != VK_SUBOPTIMAL_KHR )
        {
          renderThreadData.result =
            {"[Vk] Failed to acquire next image", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }


        result = vkResetFences(
          frontend.device, 1,
          &cpuCmdExecutedFence );

        if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to reset cpuCmdExecuted fence", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }


        result = vkResetCommandBuffer(
          cmdBuffer, 0 );

        if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to reset command buffer", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }


        const auto bufferIndex = boidSwapChain.front();
        auto& framePositions = frameData[bufferIndex].position;

        std::atomic_thread_fence(std::memory_order_acquire);

        writeVertexBuffer(
          frontend, framePositions.data(),
          0, sizeof(Vector3) * framePositions.length(),
          vertexBufferMemory[acquiredImageIndex] );

        boidSwapChain.retire();


        const VkCommandBufferBeginInfo cmdBufferBeginInfo
        {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = 0,
          .pInheritanceInfo = nullptr,
        };

        result = vkBeginCommandBuffer(
          cmdBuffer, &cmdBufferBeginInfo );

        if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to begin command buffer", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }


        const VkClearValue clearColor
        {
          .color = {0.f, 0.f, 0.f, 1.f},
        };

        const VkRenderPassBeginInfo renderPassBeginInfo
        {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .renderPass = frontend.renderPass,
          .framebuffer = framebuffer,
          .renderArea =
          {
            .offset = {},
            .extent = frontend.swapchain.extent,
          },
          .clearValueCount = 1,
          .pClearValues = &clearColor,
        };

        vkCmdBeginRenderPass(
          cmdBuffer,
          &renderPassBeginInfo,
          VK_SUBPASS_CONTENTS_INLINE );

        vkCmdBindPipeline(
          cmdBuffer,
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          frontend.graphicsPipeline );


        const VkBuffer vertexBuffers[]
        {
          vertexBuffer[acquiredImageIndex],
          vertexBuffer[acquiredImageIndex],
        };

        const VkDeviceSize offsets[]
        {
          0, 0,
        };

        vkCmdBindVertexBuffers(
          cmdBuffer, 0, 2,
          vertexBuffers, offsets );


        const VkViewport viewport
        {
          .x = 0.f,
          .y = 0.f,
          .width = static_cast <float> (frontend.swapchain.extent.width),
          .height = static_cast <float> (frontend.swapchain.extent.height),
          .minDepth = 0.f,
          .maxDepth = 1.f,
        };

        vkCmdSetViewport(
          cmdBuffer, 0,
          1, &viewport );


        const VkRect2D scissor
        {
          .offset = {},
          .extent = frontend.swapchain.extent,
        };

        vkCmdSetScissor(
          cmdBuffer, 0,
          1, &scissor );


        vkCmdDraw(
          cmdBuffer,
          boidCount, 1,
          0, 0 );


        vkCmdEndRenderPass(cmdBuffer);


        result = vkEndCommandBuffer(cmdBuffer);

        if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to end command buffer", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }


        const VkPipelineStageFlags waitStages[]
        {
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        };

        const VkSemaphore waitSemaphores[]
        {
          imageReadySignal,
        };

        const VkSemaphore signalSemaphores[]
        {
          gpuCmdExecutedSignal,
        };

        const VkSubmitInfo submitInfo
        {
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = waitSemaphores,
          .pWaitDstStageMask = waitStages,
          .commandBufferCount = 1,
          .pCommandBuffers = &cmdBuffer,
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = signalSemaphores,
        };

        result = vkQueueSubmit(
          frontend.queues.graphics.handle,
          1, &submitInfo,
          cpuCmdExecutedFence );

        if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to submit command buffer to a queue", result};

          renderThreadData.errorCaught.store(true);

          return;
        }


        const VkSwapchainKHR swapchains[]
        {
          frontend.swapchain.handle,
        };

        const VkPresentInfoKHR presentInfo
        {
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = signalSemaphores,
          .swapchainCount = 1,
          .pSwapchains = swapchains,
          .pImageIndices = &acquiredImageIndex,
          .pResults = nullptr,
        };

        result = vkQueuePresentKHR(
          frontend.queues.presentation.handle,
          &presentInfo );

        if ( result == VK_ERROR_OUT_OF_DATE_KHR ||
             result == VK_SUBOPTIMAL_KHR )
        {
          currentFrameIndex = 0;
          renderThreadData.swapchainRecreationRequested.store(true);
          return;
        }

        else if ( result != VK_SUCCESS )
        {
          renderThreadData.result =
            {"[Vk] Failed to queue image for presentation", result};

          std::atomic_thread_fence(std::memory_order_release);
          renderThreadData.errorCaught.store(true);

          return;
        }

        ++currentFrameIndex %= frontend.swapchain.maxConcurrentFrames;


        PERF_TIME_END(PerfMarker::Rendering);

        timeCounter[PerfMarker::Rendering].update(600);
      }
    };


    threadPool.parallel_for(posInitTask, boidCount, 1);
    threadPool.waitForTasks();


    std::thread renderThread {renderingTask};

    std::cout << "start\n";

    const std::size_t frameCount {600};
    const float targetFrameTime {1.f / 120.f};

    for ( std::size_t frame {}; frame < frameCount; ++frame )
    {
      if ( glfwWindowShouldClose(frontend.window) == true ||
           renderThreadData.errorCaught.load() == true )
        break;

      glfwPollEvents();

      if ( renderThreadData.swapchainRecreationRequested.load() == true )
      {
        renderThreadData.shutdownRequested.store(true);
        renderThread.join();

        renderThreadData.shutdownRequested.store(false);
        renderThreadData.swapchainRecreationRequested.store(false);

        vkDeviceWaitIdle(frontend.device);

        recreateSwapchain(frontend);

        boidSwapChain.reset();

        std::atomic_thread_fence(std::memory_order_release);

        renderThread = std::thread{renderingTask};
      }

      deltaTime = std::fmod(dist(engine), targetFrameTime );


      PERF_TIME_BEGIN(PerfMarker::Total);
      PERF_TIME_BEGIN_COPY(PerfMarker::ResetTask, PerfMarker::Total);


      threadPool.parallel_for(resetAveragePositionTask, boidCount, 1);
      threadPool.parallel_for(resetAverageVelocityTask, boidCount, 1);
      threadPool.parallel_for(resetBoidCountTask, boidCount, 1);
//      threadPool.parallel_for(resetCellsTask, cellCount, 1);
      resetCellsTask(0, cellCount);

      threadPool.waitForTasks();


      PERF_TIME_END(PerfMarker::ResetTask);
      PERF_TIME_BEGIN(PerfMarker::HashPosTask);


      hashPosTask();


      PERF_TIME_END(PerfMarker::HashPosTask);
      PERF_TIME_BEGIN(PerfMarker::Summing);


      threadPool.parallel_for(copyPositionsTask, boidCount, 1);
      threadPool.parallel_for(copyVelocitiesTask, boidCount, 1);
      threadPool.push(averagePositionSumTask);
      threadPool.push(averageVelocitySumTask);
//      threadPool.push(boidCountSumTask);
      boidCountSumTask();

      threadPool.waitForTasks();

      boidSwapChain.swap();


      PERF_TIME_END(PerfMarker::Summing);
      PERF_TIME_BEGIN(PerfMarker::RulesCalc);


      threadPool.parallel_for(calcAlignmentTask, boidCount);
      threadPool.parallel_for(calcCoherenceTask, boidCount);
      threadPool.parallel_for(calcSeparationTask, boidCount);
//      threadPool.parallel_for(calcObstacleAvoidanceTask, boidCount, 1);
      calcObstacleAvoidanceTask(0, boidCount);

      threadPool.waitForTasks();


      PERF_TIME_END(PerfMarker::RulesCalc);
      PERF_TIME_BEGIN(PerfMarker::Transform);


      threadPool.parallel_for(transformBoidsTask, boidCount, threadCount + 1);
      threadPool.waitForTasks();


      PERF_TIME_END(PerfMarker::Transform);
      PERF_TIME_END(PerfMarker::Total);


      for ( size_t i {}; i < PerfMarker::Count; ++i )
        if ( i != PerfMarker::Rendering )
          timeCounter[i].update(frameCount);
    }

    renderThreadData.shutdownRequested.store(true);
    renderThread.join();

    if ( frontend.device != VK_NULL_HANDLE )
      vkDeviceWaitIdle(frontend.device);

    std::atomic_thread_fence(std::memory_order_acquire);

    if ( renderThreadData.errorCaught.load() == true )
      LOG_ERROR( "Render thread error {}: {}",
        (int) renderThreadData.result.code,
        renderThreadData.result.message );


    Vector3 pos {};
    Vector3 vel {};

    for ( std::size_t i {}; i < boidCount; ++i )
    {
      pos += boids.position[i];
      vel += boids.velocity[i];
    }

    pos /= boidCount;
    vel /= boidCount;

    std::cout << "avg pos " << pos.x << ", " << pos.y << ", " << pos.z << "\n";
    std::cout << "avg vel " << vel.x << ", " << vel.y << ", " << vel.z << "\n";
    std::cout << "\n";

    std::cout << "Threads: " << threadCount + 1 << "\n";

    printElapsedTime(PerfMarker::ResetTask, "ResetTask");
    printElapsedTime(PerfMarker::HashPosTask, "HashPosTask");
    printElapsedTime(PerfMarker::Summing, "Summing");
    printElapsedTime(PerfMarker::RulesCalc, "RulesCalc");
    printElapsedTime(PerfMarker::Transform, "Transform");
    printElapsedTime(PerfMarker::Rendering, "Rendering");
    printElapsedTime(PerfMarker::Total, "Total");
    std::cout << "\n";

    printElapsedTime(PerfMarker::PositionSumTask, "PositionSumTask");
    printElapsedTime(PerfMarker::VelocitySumTask, "VelocitySumTask");
    printElapsedTime(PerfMarker::BoidCountSumTask, "BoidCountSumTask");

//    printElapsedTime(PerfMarker::AlignmentTask, "AlignmentTask");
//    printElapsedTime(PerfMarker::CoherenceTask, "CoherenceTask");
//    printElapsedTime(PerfMarker::SeparationTask, "SeparationTask");
    printElapsedTime(PerfMarker::ObstacleAvoidanceTask, "ObstacleAvoidanceTask");
    std::cout << "\n";

    std::cout << "Memory usage: " << allocator.bytesReserved() << " bytes\n";
    std::cout << "Memory usage (Vulkan):\n";
    vkAllocator.printMemoryUsage();

    threadPool.deinit();
  }


  allocator.free();

  for ( std::size_t i {}; i < frontend.swapchain.maxConcurrentFrames; ++i )
  {
    destroyVertexBuffer(
      frontend, vertexBuffer[i],
      vertexBufferMemory[i] );
  }

  deinitializeFrontend(frontend);

  destroyLogger();

  return 0;
}

