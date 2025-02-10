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

  mMiddleBufferSwaps.fetch_add(
    1, std::memory_order_relaxed );
}

void
Swapchain::retire()
{
  const auto currentSwaps =
    mMiddleBufferSwaps.load(std::memory_order_relaxed);

  if ( currentSwaps > mMiddleBufferSwapsPrev )
    swap(mFront);

  mMiddleBufferSwapsPrev = currentSwaps;
}

void
Swapchain::reset()
{
  mFront = 2;
  mMiddle = 1;
  mBack = 0;

  mMiddleBufferSwaps = 0;
  mMiddleBufferSwapsPrev = 0;
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
