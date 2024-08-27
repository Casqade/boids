#pragma once

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <memory>


#define IsPowerOfTwo(integer) \
  !( integer != 1 && integer & (integer - 1) )


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

AllocatorArena::~AllocatorArena()
{
  assert(mStart == nullptr);
}

bool
AllocatorArena::reserve(
  const std::size_t bytes,
  AllocatorArena* parent ) noexcept
{
  assert(mStart == nullptr);

  if ( parent == nullptr )
    mStart = std::malloc(bytes);
  else
    mStart = parent->allocate <std::byte> (bytes);

  if ( mStart == nullptr )
    return false;

  mCurrent = mStart;
  mEnd = static_cast <std::byte*> (mStart) + bytes;

  return true;
}

void
AllocatorArena::free(
  AllocatorArena* parent ) noexcept
{
  assert(mStart != nullptr);

  if ( parent == nullptr )
    std::free(mStart);
  else
    parent->deallocate(
      static_cast <std::byte*> (mStart),
      bytesReserved() );

  mStart = {};
  mEnd = {};
  mCurrent = {};
}

template <typename T>
T*
AllocatorArena::allocate(
  const std::size_t elements,
  const std::size_t alignment ) noexcept
{
  assert(mStart != nullptr);
  assert(IsPowerOfTwo(alignment));

  if ( mCurrent == mEnd )
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

std::size_t
AllocatorArena::bytesLeft() const noexcept
{
  return
    static_cast <std::byte*> (mEnd) -
    static_cast <std::byte*> (mCurrent);
}

std::size_t
AllocatorArena::bytesReserved() const noexcept
{
  return
    static_cast <std::byte*> (mEnd) -
    static_cast <std::byte*> (mStart);
}
