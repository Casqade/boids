#include "Allocators.hpp"
#include "Containers.hpp"
#include "Vector.hpp"
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
  Total,

  PositionSumTask,
  VelocitySumTask,
  BoidCountSumTask,

  ObstacleAvoidanceTask,
  AlignmentTask,
  CoherenceTask,
  SeparationTask,

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

int
main(
  int argc,
  char* argv[] )
{
  const std::size_t threadCount {3};
  const std::size_t taskBufferSize = threadCount * 2;
  const std::size_t boidCount {400'000};
  const std::size_t cellPerAxisCount {100};
  const std::size_t cellCount =
    std::pow(cellPerAxisCount, std::size_t{3});

  const std::size_t maxOccupiedCellCount =
    std::min(boidCount, cellCount);

  const auto boidMemory =
    sizeof(std::size_t) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3);

  const auto cellMemory =
    sizeof(std::size_t) +
    sizeof(Vector3) +
    sizeof(Vector3);

  AllocatorArena allocator {};
  allocator.reserve(
    sizeof(ThreadPool::TaskStorage) * taskBufferSize +
    sizeof(ThreadPool::ThreadEntry) * threadCount +
    boidMemory * boidCount +
    cellMemory * maxOccupiedCellCount +
    sizeof(std::size_t) * cellCount +
    sizeof(std::size_t) * 13 );

  static float deltaTime;

  {
    auto mask = initAffinityMask();
    addCpuToAffinityMask(mask, 0);
    setThreadAffinity(mask);


    ThreadPool threadPool {};
    threadPool.init(
      allocator, taskBufferSize, threadCount, 2 );


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


    const auto resetCellsTask =
    [&cells, tombstone = maxOccupiedCellCount] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      std::fill_n(
        cells.data() + rangeStart,
        rangeEnd - rangeStart,
        tombstone );
    };

    const auto resetAveragePositionTask =
    [&occupiedCells] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        occupiedCells.averagePosition[i] = {};
    };

    const auto resetAverageVelocityTask =
    [&occupiedCells] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        occupiedCells.averageVelocity[i] = {};
    };

    const auto resetBoidCountTask =
    [&occupiedCells] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( size_t i = rangeStart; i < rangeEnd; ++i )
        occupiedCells.boidCount[i] = {};
    };


    const auto hashPosTask =
    [&boids, &cells, tombstone = maxOccupiedCellCount] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      size_t occupiedCellCount {};

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
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
    [&boids, &occupiedCells]
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
    [&boids, &occupiedCells]
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
    [&boids, &rules] ()
    {
      PERF_TIME_BEGIN(PerfMarker::ObstacleAvoidanceTask);

      for ( std::size_t i {}; i < boidCount; ++i )
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
    [&boids, &occupiedCells, &weights = rules.weights] ()
    {
      PERF_TIME_BEGIN(PerfMarker::AlignmentTask);

      for ( std::size_t i {}; i < boidCount; ++i )
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

      PERF_TIME_END(PerfMarker::AlignmentTask);
    };

    const auto calcCoherenceTask =
    [&boids, &occupiedCells, &weights = rules.weights] ()
    {
      PERF_TIME_BEGIN(PerfMarker::CoherenceTask);

      for ( std::size_t i {}; i < boidCount; ++i )
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

      PERF_TIME_END(PerfMarker::CoherenceTask);
    };

    const auto calcSeparationTask =
    [&boids, &occupiedCells, &weights = rules.weights] ()
    {
      PERF_TIME_BEGIN(PerfMarker::SeparationTask);

      for ( std::size_t i {}; i < boidCount; ++i )
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

      PERF_TIME_END(PerfMarker::SeparationTask);
    };


    const auto transformBoidsTask =
    [&boids, &rules] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
//      PERF_TIME_BEGIN(PerfMarker::TransformBoidsTask);

      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        auto& velocity = boids.velocity[i];
        auto& position = boids.position[i];

        const auto& obstacleAvoidance = boids.obstacleAvoidance[i];
        const auto& alignment = boids.alignment[i];
        const auto& coherence = boids.coherence[i];
        const auto& separation = boids.separation[i];

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

    threadPool.parallel_for(posInitTask, boidCount);
    threadPool.waitForTasks();


    std::cout << "start\n";

    const std::size_t frameCount {600};
    const float targetFrameTime {1.f / 120.f};

    for ( std::size_t frame {}; frame < frameCount; ++frame )
    {
      deltaTime = std::fmod(dist(engine), targetFrameTime );


      PERF_TIME_BEGIN(PerfMarker::Total);
      PERF_TIME_BEGIN_COPY(PerfMarker::ResetTask, PerfMarker::Total);

      threadPool.push(
      [resetAveragePositionTask, boidCount] ()
      {
        resetAveragePositionTask(0, boidCount);
      });

      threadPool.push(
      [resetAverageVelocityTask, boidCount] ()
      {
        resetAverageVelocityTask(0, boidCount);
      });

      threadPool.push(
      [resetBoidCountTask, boidCount] ()
      {
        resetBoidCountTask(0, boidCount);
      });

//      threadPool.parallel_for(resetCellsTask, cellCount, threadCount - 3);
      resetCellsTask(0, cellCount);

      threadPool.waitForTasks();


      PERF_TIME_END(PerfMarker::ResetTask);
      PERF_TIME_BEGIN(PerfMarker::HashPosTask);

      hashPosTask(0, boidCount);
//      threadPool.parallel_for(hashPosTask, boidCount);
//      threadPool.waitForTasks();

      PERF_TIME_END(PerfMarker::HashPosTask);
      PERF_TIME_BEGIN(PerfMarker::Summing);

      threadPool.push(averagePositionSumTask);
      threadPool.push(averageVelocitySumTask);
      boidCountSumTask();

      threadPool.waitForTasks();

      PERF_TIME_END(PerfMarker::Summing);
      PERF_TIME_BEGIN(PerfMarker::RulesCalc);

      threadPool.push(calcAlignmentTask);
      threadPool.push(calcCoherenceTask);
      threadPool.push(calcSeparationTask);
      calcObstacleAvoidanceTask();

      threadPool.waitForTasks();

      PERF_TIME_END(PerfMarker::RulesCalc);
      PERF_TIME_BEGIN(PerfMarker::Transform);

//      transformBoidsTask(0, boidCount);
      threadPool.parallel_for(transformBoidsTask, boidCount);
      threadPool.waitForTasks();

      PERF_TIME_END(PerfMarker::Transform);
      PERF_TIME_END(PerfMarker::Total);

      for ( size_t i {}; i < PerfMarker::Count; ++i )
        timeCounter[i].update(frameCount);
    }

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

    printElapsedTime(PerfMarker::ResetTask, "reinit");
    printElapsedTime(PerfMarker::HashPosTask, "HashPosTask");
    printElapsedTime(PerfMarker::Summing, "Summing");
    printElapsedTime(PerfMarker::RulesCalc, "RulesCalc");
    printElapsedTime(PerfMarker::Transform, "Transform");
    printElapsedTime(PerfMarker::Total, "Total");
    std::cout << "\n";
    printElapsedTime(PerfMarker::PositionSumTask, "PositionSumTask");
    printElapsedTime(PerfMarker::VelocitySumTask, "VelocitySumTask");
    printElapsedTime(PerfMarker::BoidCountSumTask, "BoidCountSumTask");

    printElapsedTime(PerfMarker::ObstacleAvoidanceTask, "ObstacleAvoidanceTask");
    printElapsedTime(PerfMarker::AlignmentTask, "AlignmentTask");
    printElapsedTime(PerfMarker::CoherenceTask, "CoherenceTask");
    printElapsedTime(PerfMarker::SeparationTask, "SeparationTask");
    std::cout << "\n";

    std::cout << "Memory usage: " << allocator.bytesReserved() << " bytes\n";

    threadPool.deinit();
  }


  allocator.free();

  return 0;
}

