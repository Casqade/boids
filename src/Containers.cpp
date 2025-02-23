#include "Containers.hpp"


void
Swapchain::swap(
  std::size_t& bufferIndex )
{
  auto oldMiddle = mMiddle.load(std::memory_order_acquire);

  while ( mMiddle.compare_exchange_strong(oldMiddle, bufferIndex) == false )
    ;

  bufferIndex = oldMiddle;
}

void
Swapchain::swap()
{
  swap(mBack);

  mMiddleBufferWasRead.clear(std::memory_order_relaxed);
}

void
Swapchain::retire()
{
  const auto wasRead = mMiddleBufferWasRead.test_and_set(
    std::memory_order_relaxed );

  if ( wasRead == false )
    swap(mFront);
}

void
Swapchain::reset()
{
  mFront = 2;
  mMiddle.store(1, std::memory_order_relaxed);
  mBack = 0;

  mMiddleBufferWasRead.clear(std::memory_order_relaxed);
}

std::size_t
Swapchain::back() const
{
  return mBack;
}

std::size_t
Swapchain::front() const
{
  return mFront;
}
