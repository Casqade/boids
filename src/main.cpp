#include "Allocators.hpp"
#include "Containers.hpp"
#include "Vector.hpp"
#include "ThreadPool.hpp"
#include "PerformanceCounter.hpp"

#include <cassert>
#include <chrono>

#include <bitset>
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
  using FloatType = Vector3::value_type;


  Array <std::size_t> cellId {};
  Array <std::size_t> boidCount {};

  Array <Vector3> averagePosition {};
  Array <Vector3> averageVelocity {};

  Array <Vector3> obstacleAvoidance {};
  Array <Vector3> alignment {};
  Array <Vector3> coherence {};
  Array <Vector3> separation {};
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
  const std::size_t boidCount {400'000};
  const std::size_t cellPerAxisCount {100};
  const std::size_t cellCount =
    std::pow(cellPerAxisCount, std::size_t{3});

  const auto boidMemory =
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(std::size_t) +
    sizeof(std::size_t) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3) +
    sizeof(Vector3);

  const auto cellMemory =
    sizeof(std::size_t);
//    sizeof(std::atomic_size_t);


  AllocatorArena allocator {};
  allocator.reserve(
    sizeof(ThreadPool::ThreadEntry) * threadCount +
    boidMemory * boidCount +
    cellMemory * cellCount +
    sizeof(std::size_t) * 12 );


  {
    auto mask = initAffinityMask();
    addCpuToAffinityMask(mask, 0);
    setThreadAffinity(mask);


    ThreadPool threadPool {};
    threadPool.init(
      allocator, threadCount, 2 );


    BoidData boids
    {
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
      {allocator, boidCount},
    };

    Array <std::size_t> cells {allocator, cellCount};

    BoidRuleset rules {};

    std::random_device rd {};
    std::uniform_real_distribution dist(0.f, 1.f);
    std::minstd_rand0 engine {rd()};

    const auto posInitTask =
    [&boids, &dist, &engine] ( const std::size_t rangeStart, const std::size_t rangeEnd )
    {
      for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
      {
        boids.position[i] = { dist(engine), dist(engine), dist(engine) };
//        boids.velocity[i] = { dist(engine), dist(engine), dist(engine) };
      }
    };

    threadPool.parallel_for(posInitTask, boidCount);
    threadPool.waitForTasks();


    std::cout << "start\n";

    const std::size_t frameCount {600};

    for ( std::size_t frame {}; frame < frameCount; ++frame )
    {
      const float delta = std::fmod(dist(rd), 5.f / frameCount);

      PERF_TIME_BEGIN(PerfMarker::Total);
      PERF_TIME_BEGIN_COPY(PerfMarker::ResetTask, PerfMarker::Total);

      const auto resetCellsTask =
      [&cells, boidCount] ( const std::size_t rangeStart, const std::size_t rangeEnd )
      {
        std::fill_n(
          cells.data() + rangeStart,
          rangeEnd - rangeStart,
          boidCount );
      };

      const auto resetAveragePositionTask =
      [&boids] ( const std::size_t rangeStart, const std::size_t rangeEnd )
      {
        for ( size_t i = rangeStart; i < rangeEnd; ++i )
          boids.averagePosition[i] = {};
      };

      const auto resetAverageVelocityTask =
      [&boids] ( const std::size_t rangeStart, const std::size_t rangeEnd )
      {
        for ( size_t i = rangeStart; i < rangeEnd; ++i )
          boids.averageVelocity[i] = {};
      };

      const auto resetBoidCountTask =
      [&boids] ( const std::size_t rangeStart, const std::size_t rangeEnd )
      {
        for ( size_t i = rangeStart; i < rangeEnd; ++i )
          boids.boidCount[i] = {};
      };


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


      const auto hashPosTask =
      [&boids, &cells] ( const std::size_t rangeStart, const std::size_t rangeEnd )
      {
        for ( std::size_t i = rangeStart; i < rangeEnd; ++i )
        {
          const auto& boidPosition = boids.position[i];

          const auto cellId = hashPos(
            boidPosition, cellPerAxisCount );

          boids.cellId[i] = cellId;

          cells[cellId] = std::min(i, cells[cellId]);
        }
      };

      hashPosTask(0, boidCount);
      //    threadPool.parallel_for(hashPosTask, boidCount);
      //    threadPool.waitForTasks();

      PERF_TIME_END(PerfMarker::HashPosTask);
      PERF_TIME_BEGIN(PerfMarker::Summing);

      const auto averagePositionSumTask =
      [&boids, &cells]
      {
        PERF_TIME_BEGIN(PerfMarker::PositionSumTask);

        for ( std::size_t i {}; i < boidCount; ++i )
        {
          const auto cellId = cells[boids.cellId[i]];

          const auto& boidPosition = boids.position[i];

          boids.averagePosition[cellId] += boidPosition;
        }

        PERF_TIME_END(PerfMarker::PositionSumTask);
      };

      const auto averageVelocitySumTask =
      [&boids, &cells]
      {
        PERF_TIME_BEGIN(PerfMarker::VelocitySumTask);

        for ( std::size_t i {}; i < boidCount; ++i )
        {
          const auto cellId = cells[boids.cellId[i]];

          const auto& boidVelocity = boids.velocity[i];

          boids.averageVelocity[cellId] += boidVelocity;
        }

        PERF_TIME_END(PerfMarker::VelocitySumTask);
      };

      const auto boidCountSumTask =
      [&boids, &cells] ()
      {
        PERF_TIME_BEGIN(PerfMarker::BoidCountSumTask);

        for ( std::size_t i {}; i < boidCount; ++i )
        {
          const auto cellId = cells[boids.cellId[i]];

          boids.boidCount[cellId] += 1;
        }

        PERF_TIME_END(PerfMarker::BoidCountSumTask);
      };

      threadPool.push(averagePositionSumTask);
      threadPool.push(averageVelocitySumTask);
      boidCountSumTask();

      threadPool.waitForTasks();


      PERF_TIME_END(PerfMarker::Summing);
      PERF_TIME_BEGIN(PerfMarker::RulesCalc);

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
      [&boids, &cells, &weights = rules.weights] ()
      {
        PERF_TIME_BEGIN(PerfMarker::AlignmentTask);

        for ( std::size_t i {}; i < boidCount; ++i )
        {
          const auto cellId = cells[boids.cellId[i]];

          const auto neighborCount = boids.boidCount[cellId];

//          assert(neighborCount > 0);

          const auto& velocity = boids.velocity[i];

          const auto& averageVelocity =
            boids.averageVelocity[cellId];

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
      [&boids, &cells, &weights = rules.weights] ()
      {
        PERF_TIME_BEGIN(PerfMarker::CoherenceTask);

        for ( std::size_t i {}; i < boidCount; ++i )
        {
          const auto cellId = cells[boids.cellId[i]];
          const auto neighborCount = boids.boidCount[cellId];

//          assert(neighborCount > 0);

          const auto& position = boids.position[i];

          const auto& averagePosition =
            boids.averagePosition[cellId];

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
      [&boids, &cells, &weights = rules.weights] ()
      {
        PERF_TIME_BEGIN(PerfMarker::SeparationTask);

        for ( std::size_t i {}; i < boidCount; ++i )
        {
          const auto cellId = cells[boids.cellId[i]];
          const auto neighborCount = boids.boidCount[cellId];

//          assert(neighborCount > 0);

          const auto& position = boids.position[i];

          const auto& averagePosition =
            boids.averagePosition[cellId];

          const auto separation =
            position - averagePosition / neighborCount;

          boids.separation[i] =
//            averagePosition;
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
      [&boids, &rules, delta] ( const std::size_t rangeStart, const std::size_t rangeEnd )
      {
//        PERF_TIME_BEGIN(PerfMarker::TransformBoidsTask);

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
            (velocity + (desiredVelocity - velocity) * delta).normalized();

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

          position += velocity * rules.maxSpeed * delta;

          assert(position.x >= 0.f);
          assert(position.y >= 0.f);
          assert(position.z >= 0.f);
          assert(position.x <= 1.f);
          assert(position.y <= 1.f);
          assert(position.z <= 1.f);
          continue;

          boids.position[i] =
          {
            std::fmod(boids.position[i].x + velocity.x * delta, 1.f),
            std::fmod(boids.position[i].y + velocity.y * delta, 1.f),
            std::fmod(boids.position[i].z + velocity.z * delta, 1.f),
          };
        }

//        PERF_TIME_END(PerfMarker::TransformBoidsTask);
      };

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

    std::cout << "boid pos " << pos.x << ", " << pos.y << ", " << pos.z << "\n";
    std::cout << "boid vel " << vel.x << ", " << vel.y << ", " << vel.z << "\n";

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

    threadPool.deinit();
  }


  allocator.free();

  return 0;
}

