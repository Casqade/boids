#pragma once

#include <vulkan/vulkan_core.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <map>
#include <unordered_map>


#define IsPowerOfTwo(integer) \
  integer != 0 && !( integer & (integer - 1) )


class AllocatorArena
{
protected:
  void* mStart {};
  void* mEnd {};
  void* mCurrent {};


public:
  AllocatorArena() noexcept = default;
  AllocatorArena( const AllocatorArena& ) noexcept = delete;
  ~AllocatorArena() noexcept;

  bool reserve(
    const std::size_t bytes,
    AllocatorArena* scratch = {} ) noexcept;

  void free( AllocatorArena* scratch = {} ) noexcept;

  template <typename T>
  T* allocate(
    const std::size_t elements,
    const std::size_t alignment = {} ) noexcept;

  template <typename T>
  void deallocate(
    T* chunk,
    const std::size_t elements ) noexcept;

  std::size_t bytesLeft() const noexcept;
  std::size_t bytesReserved() const noexcept;
};


template <typename T>
T*
AllocatorArena::allocate(
  const std::size_t elements,
  const std::size_t alignment ) noexcept
{
  assert(mStart != nullptr);
  assert(IsPowerOfTwo(alignment));

  if ( mCurrent == mEnd ||
       IsPowerOfTwo(alignment) == false )
    return {};


  auto chunkSizeAddress =
    static_cast <std::size_t*> (mCurrent);

  void* allocationAddress = chunkSizeAddress + 1;

  const auto allocationSize =
    sizeof(T) * elements;

  auto remainingSpace = bytesLeft();

  if ( allocationSize + sizeof(std::size_t) > remainingSpace )
    return {};

  auto padding = remainingSpace;

  if ( alignment != 0 )
    allocationAddress = std::align(
      alignment,
      allocationSize,
      allocationAddress,
      remainingSpace );

  if ( allocationAddress == nullptr )
    return {};

  padding -= remainingSpace;


  const auto chunkSizeAddressShifted =
    reinterpret_cast <std::byte*> (chunkSizeAddress) + padding;

  chunkSizeAddress =
    reinterpret_cast <std::size_t*> (chunkSizeAddressShifted);

  *chunkSizeAddress = padding + allocationSize;

  mCurrent = reinterpret_cast <std::byte*> (
    chunkSizeAddress + 1) + allocationSize;

  return static_cast <T*> (allocationAddress);
}

template <typename T>
void
AllocatorArena::deallocate(
  T* chunk,
  const std::size_t elements ) noexcept
{
  assert(mStart != nullptr);

  const auto chunkSizeAddress =
    reinterpret_cast <std::size_t*> (chunk) - 1;

  const std::size_t chunkSize = *chunkSizeAddress;

  const auto chunkEnd =
    reinterpret_cast <std::byte*> (chunk) + elements * sizeof(T);

  assert(mCurrent == chunkEnd);

  mCurrent = chunkEnd - chunkSize - sizeof(std::size_t);
}


namespace cqdeVk
{

class Allocator
{
  struct AllocatedBlock
  {
    size_t size {};
    size_t alignment {};
    VkSystemAllocationScope scope {};

    AllocatedBlock() = default;
  };

  struct ScopeAllocation
  {
    size_t size {};
    size_t count {};

    ScopeAllocation() = default;
  };

  using AllocationScopeMap =
    std::map <VkSystemAllocationScope, ScopeAllocation>;

  using AllocatedBlocks =
    std::unordered_map <void*, AllocatedBlock>;

  AllocatedBlocks mAllocatedBlocks {};
  AllocationScopeMap mOccupiedMemory {};


public:
  Allocator() = default;


  void printMemoryUsage() const;

  void* allocate(
    const size_t size,
    const size_t alignment,
    const VkSystemAllocationScope );

  void* reallocate(
    void* data,
    const size_t size,
    const size_t alignment,
    const VkSystemAllocationScope );

  void deallocate( void* data );


  void allocate_internal(
    const size_t size,
    const VkInternalAllocationType,
    const VkSystemAllocationScope );

  void deallocate_internal(
    const size_t size,
    const VkInternalAllocationType,
    const VkSystemAllocationScope );
};


VKAPI_ATTR
void*
VKAPI_CALL
allocate(
  void* pAllocator,
  size_t size,
  size_t alignment,
  VkSystemAllocationScope );

VKAPI_ATTR
void*
VKAPI_CALL
reallocate(
  void* pAllocator,
  void* data,
  size_t size,
  size_t alignment,
  VkSystemAllocationScope );

VKAPI_ATTR
void
VKAPI_CALL
free(
  void* pAllocator,
  void* data );

VKAPI_ATTR
void
VKAPI_CALL
internalAllocate(
  void* pAllocator,
  size_t size,
  VkInternalAllocationType,
  VkSystemAllocationScope );

VKAPI_ATTR
void
VKAPI_CALL
internalFree(
  void* pAllocator,
  size_t size,
  VkInternalAllocationType,
  VkSystemAllocationScope );

} // namespace cqdeVk
