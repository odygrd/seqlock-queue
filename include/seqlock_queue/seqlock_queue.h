#pragma once

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#if defined(_WIN32)
  #include <malloc.h>
#elif defined(__APPLE__)
  #include <sys/mman.h>
#elif defined(__CYGWIN__)
  #include <sys/mman.h>
#elif defined(__linux__)
  #include <sys/mman.h>
#endif

namespace sq::detail
{
constexpr uint32_t CACHE_ALIGNED{64u};
/***/
constexpr bool is_pow_of_two(uint64_t number) noexcept
{
  return (number != 0u) && ((number & (number - 1u)) == 0u);
}

/***/
uint64_t next_power_of_2(uint64_t n)
{
  constexpr uint64_t max_power_of_2 = (std::numeric_limits<uint64_t>::max() >> 1u) + 1u;

  if (n >= max_power_of_2)
  {
    return max_power_of_2;
  }

  return is_pow_of_two(static_cast<uint64_t>(n)) ? n : static_cast<uint64_t>(std::pow(2u, log2(n) + 1u));
}

void* align_pointer(void* pointer, size_t alignment) noexcept
{
  if (alignment == 0)
  {
    return pointer;
  }

  assert(is_pow_of_two(alignment) && "alignment must be a power of two");
  return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(pointer) + (alignment - 1ul)) & ~(alignment - 1ul));
}

/***/
void* alloc_aligned(size_t size, size_t alignment, bool huge_pages /* = false */)
{
#if defined(_WIN32)
  void* p = _aligned_malloc(size, alignment);

  if (!p)
  {
    throw std::runtime_error{std::string{"alloc_aligned failed with errno "} + std::to_string(errno)};
  }

  return p;
#else
  // Calculate the total size including the metadata and alignment
  constexpr size_t metadata_size{2u * sizeof(size_t)};
  size_t const total_size{size + metadata_size + alignment};

  // Allocate the memory
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;

  #if defined(__linux__)
  if (huge_pages)
  {
    flags |= MAP_HUGETLB;
  }
  #endif

  void* mem = ::mmap(nullptr, total_size, PROT_READ | PROT_WRITE, flags, -1, 0);

  if (mem == MAP_FAILED)
  {
    throw std::runtime_error{std::string{"mmap failed with errno "} + std::to_string(errno)};
  }

  // Calculate the aligned address after the metadata
  auto const aligned_address =
    static_cast<std::byte*>(detail::align_pointer(static_cast<std::byte*>(mem) + metadata_size, alignment));

  // Calculate the offset from the original memory location
  auto const offset = static_cast<size_t>(aligned_address - static_cast<std::byte*>(mem));

  // Store the size and offset information in the metadata
  std::memcpy(aligned_address - sizeof(size_t), &total_size, sizeof(total_size));
  std::memcpy(aligned_address - (2u * sizeof(size_t)), &offset, sizeof(offset));

  return aligned_address;
#endif
}

/***/
void free_aligned(void* ptr) noexcept
{
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  // Retrieve the size and offset information from the metadata
  size_t offset;
  std::memcpy(&offset, static_cast<std::byte*>(ptr) - (2u * sizeof(size_t)), sizeof(offset));

  size_t total_size;
  std::memcpy(&total_size, static_cast<std::byte*>(ptr) - sizeof(size_t), sizeof(total_size));

  // Calculate the original memory block address
  void* mem = static_cast<std::byte*>(ptr) - offset;

  ::munmap(mem, total_size);
#endif
}
} // namespace sq::detail

namespace sq
{
template <typename T, size_t Alignment>
struct alignas(Alignment) Slot
{
  static_assert(std::is_trivially_copyable<T>::value, "T needs to be trivially copyable");

  T value;
  std::atomic<uint8_t> version{std::numeric_limits<uint8_t>::max() - 1u};
};

/***/
template <typename T, size_t SlotAlignment = detail::CACHE_ALIGNED, size_t CacheAligned = detail::CACHE_ALIGNED>
class BoundedSeqlockQueue
{
public:
  using value_t = T;
  using slot_t = Slot<value_t, SlotAlignment>;

  BoundedSeqlockQueue(size_t capacity, bool huge_pages = false)
    : _capacity(detail::next_power_of_2(capacity)), _mask(_capacity - 1)
  {
    // Construct in place the objects
    _slots = static_cast<slot_t*>(detail::alloc_aligned(sizeof(slot_t) * _capacity, CacheAligned, huge_pages));

    for (uint64_t i = 0; i < capacity; ++i)
    {
      new (_slots + i) slot_t{};
    }
  };

  template <typename>
  friend class SeqlockQueueProducer;

  template <typename>
  friend class SeqlockQueueConsumer;

private:
  slot_t* _slots{nullptr};
  size_t _capacity{0};
  size_t _mask{0};
};

/***/
template <typename TBoundedSeqlockQueue>
class SeqlockQueueProducer
{
public:
  using value_t = typename TBoundedSeqlockQueue::value_t;
  using slot_t = typename TBoundedSeqlockQueue::slot_t;

  explicit SeqlockQueueProducer(TBoundedSeqlockQueue const& bounded_seqlock_queue)
    : _slots(bounded_seqlock_queue._slots),
      _capacity(bounded_seqlock_queue._capacity),
      _mask(bounded_seqlock_queue._mask)
  {
  }

  value_t& prepare_write() noexcept
  {
    slot_t& slot = _slots[_write_pos & _mask];
    slot.version.fetch_add(1, std::memory_order_release);
    assert((slot.version.load() & 1) && "Version must always be odd");
    std::atomic_signal_fence(std::memory_order_acq_rel);
    return slot.value;
  }

  void commit_write() noexcept
  {
    slot_t& slot = _slots[_write_pos & _mask];
    std::atomic_signal_fence(std::memory_order_acq_rel);
    slot.version.fetch_add(1, std::memory_order_release);
    assert(!(slot.version.load() & 1) && "Version must always be even");
    _write_pos += 1;
  }

private:
  slot_t* _slots{nullptr};
  size_t _capacity{0};
  size_t _mask{0};
  size_t _write_pos{0};
};

/***/
template <typename TBoundedSeqlockQueue>
class SeqlockQueueConsumer
{
public:
  using value_t = typename TBoundedSeqlockQueue::value_t;
  using slot_t = typename TBoundedSeqlockQueue::slot_t;

  explicit SeqlockQueueConsumer(TBoundedSeqlockQueue& bounded_seqlock_queue)
    : _slots(bounded_seqlock_queue._slots),
      _capacity(bounded_seqlock_queue._capacity),
      _mask(bounded_seqlock_queue._mask)
  {
  }

  bool try_read(value_t& result) noexcept
  {
    size_t const read_index = _read_pos & _mask;
    slot_t const& slot = _slots[read_index];

    uint8_t const version_1 = slot.version.load(std::memory_order_acquire);
    std::atomic_signal_fence(std::memory_order_acq_rel);

    result = slot.value;

    std::atomic_signal_fence(std::memory_order_acq_rel);
    uint8_t const version_2 = slot.version.load(std::memory_order_acquire);

    if ((version_1 != version_2) || (version_1 & 1)) [[unlikely]]
    {
      // This can only happen when the producer catches up with the consumer and tries to
      // overwrite the slot
      return false;
    }

    constexpr uint8_t limit = std::numeric_limits<typename decltype(slot.version)::value_type>::max() - 1;
    uint8_t const version_diff = version_1 - _read_version;

    if (version_diff >= limit)
    {
      // when the version wraps we will have e.g. for capacity 4
      // 0 0 254 254
      // version_2 will be 254 for the slot at index 2 when reading here
      // _read_version will be 0
      // we do not want to read 254 as we already read it earlier before version wrapped around

      // This also ensures that we won't be reading the values twice, eg :
      // version_1 is 10;
      // _read_version is 12 after reading the whole queue and wrapping
      // publisher never wrote something new to the start of the queue
      // version_1 - _read_version will give 254
      return false;
    }

    // When we have read all items for the queue we will wrap around, we need to increment
    // the reader version
    if (read_index == 0)
    {
      _read_version = version_2;
    }
    else if (read_index == (_capacity - 1))
    {
      _read_version = version_2 + 2;
    }

    _read_pos += 1;

    return true;
  }

private:
  slot_t const* _slots{nullptr};
  size_t _capacity{0};
  size_t _mask{0};
  size_t _read_pos{0};
  uint8_t _read_version{0};
};
} // namespace sq
