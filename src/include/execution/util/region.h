#pragma once

#include <cstdint>
#include <limits>
#include <memory> // TODO(jordig) fix?
#include <string>
#include <type_traits>
#include <variant>

#include "common/macros.h"
#include "common/managed_pointer.h"  // TODO(jordig) is this needed?
#include "common/math_util.h"
#include "execution/util/execution_common.h"

namespace terrier::execution::util {

/**
 * A region-based allocator supports fast O(1) time allocations of small chunks
 * of memory. Individual de-allocations are not supported, but the entire
 * region can be de-allocated in one fast operation upon destruction. Regions
 * are used to hold ephemeral objects that are allocated once and freed all at
 * once. This is the pattern used during parsing when generating AST nodes
 * which are thrown away after compilation to bytecode.
 */
class Region {
 public:
  /**
   * Construct a region with the given name @em name. No allocations are
   * performed upon construction, only at the first call to @em Allocate().
   */
  explicit Region(std::string name) noexcept;

  /**
   * Regions cannot be copied or moved
   */
  DISALLOW_COPY_AND_MOVE(Region);

  /**
   * Destructor. All allocated memory is freed here.
   */
  ~Region();

  /**
   * Allocate memory from this region
   * @param size The number of bytes to allocate
   * @param alignment The desired alignment
   * @return A pointer to the start of the allocated space
   */
  void *Allocate(std::size_t size, std::size_t alignment = K_DEFAULT_BYTE_ALIGNMENT);

  /**
   * Allocate a (contiguous) array of elements of the given type
   * @tparam T The type of each element in the array
   * @param num_elems The number of requested elements in the array
   * @return A pointer to the allocated array
   */
  template <typename T>
  T *AllocateArray(std::size_t num_elems) {
    return static_cast<T *>(Allocate(num_elems * sizeof(T), alignof(T)));
  }

  /* TODO(jordig) Should this be "Managed"? DelegateToRegion? TakeOwnership?
   * TODO(jordig) Maybe something as short and non-descript as "Move"? No... Hm.
   */
  /**
   * Delegate the freeing of a unique_ptr to the region
   * @tparam T The base-type of the pointer
   * @param ptr The pointer that the region is to manage
   * @return A managed pointer representing the pointer being managed
   */
  template <typename T>
  common::ManagedPointer<T> Manage(std::unique_ptr<T> ptr) {
    // TODO(jordig) Make sure it's safe to allocate this from the region & do it
    // Should just be making the if into an else-if and then dragging that out.
    // maybe UAF but order on our side i think? otws reverse or peek. test it
    auto chunk = new (::operator new (sizeof(Chunk))) Chunk;
    auto underlying = ptr.release();
    auto wrapper = DeleterWrapper<T>(std::ref(ptr.get_deleter()), underlying);

    // Initialize chunk
    chunk->Init(head_, sizeof(Chunk)); // TODO: Chunk can have a real constructor
    chunk->buffer_ = *reinterpret_cast<Deleter *>(&wrapper);

    // Update linked list
    head_ = chunk;
    position_ = chunk->Start();
    end_ = chunk->End();
    chunk_bytes_allocated_ += chunk->size_;

    return common::ManagedPointer(underlying);
  }

  // TODO(jordig) probably unneeded. definitely hacky and gross. remove later?
  /**
   * Returns a unique_ptr to a leaky caller
   * @tparam T The base-type of the pointer
   * @param ptr The pointer that the region is to manage
   * @return A new unique pointer to *ptr that the caller is expected to leak
   */
  template <typename T>
  std::unique_ptr<T> ManageLeakedUniquePtr(std::unique_ptr<T> &ptr) {
    return std::unique_ptr<T>(Manage(ptr).Get());
  }

  /**
   * Individual de-allocations in a region-allocator are a no-op. All memory is
   * freed when the region is destroyed, or manually through a call to
   * @em FreeAll().
   * @param ptr The pointer to the memory we're de-allocating
   * @param size The number of bytes the pointer points to
   */
  void Deallocate(const void *ptr, std::size_t size) const {
    // No-op
  }

  /**
   * Free all allocated objects in one fell swoop
   */
  void FreeAll();

  // -------------------------------------------------------
  // Accessors
  // -------------------------------------------------------

  /**
   * @return The name of the region
   */
  const std::string &Name() const { return name_; }

  /**
   * @return The number of bytes this region has given out
   */
  uint64_t Allocated() const { return allocated_; }

  /**
   * @return The number of bytes wasted due to alignment requirements
   */
  uint64_t AlignmentWaste() const { return alignment_waste_; }

  /**
   * @return The total number of bytes acquired from the OS
   */
  uint64_t TotalMemory() const { return chunk_bytes_allocated_; }

 private:
  // Expand the region
  uintptr_t Expand(std::size_t requested);

 private:
  /**
   * A functor that wraps around a raw pointer and its deleter.
   * @tparam T the base type of the pointer
   */
  template <typename T>
  struct DeleterWrapper {
    //
    std::function<void(T *)> deleter_;
    //
    T *object_;
    /**
     * DeleterWrapper's constructor
     * @param deleter The function to call with the wrapped object in order to delete it
     * @param object The object being wrapped. Gets deleted by deleter.
     * @return A DeleterWrapper specialized for the type T
     */
    DeleterWrapper(std::function<void(T *)> deleter, T *object) : deleter_(deleter), object_(object){};
    /**
     * The parameterless overload that deletes the wrapped object.
     */
    void operator()() { deleter_(object_); }
  };

  // An alias used to erase the DeleterWrapper template argument
  using Deleter = DeleterWrapper<void>;

  // A chunk represents a physically contiguous "chunk" of memory. It is the
  // smallest unit of allocation a region acquires from the operating system.
  // Each individual region allocation is sourced from a chunk.
  struct Chunk {
    Chunk *next_;
    uint64_t size_;  // Includes the size of the chunk
    // buffer_ is either a quasi-empty buffer (e.g. char[0]) or a delete wrapper
    std::variant<std::monostate, Deleter> buffer_;

    void Init(Chunk *next, uint64_t size) {
      this->next_ = next;
      this->size_ = size;
    }

    uintptr_t Start() const { return reinterpret_cast<uintptr_t>(&this->buffer_); }

    uintptr_t End() const { return reinterpret_cast<uintptr_t>(this) + size_; }
  };

 private:
  // The alignment of all pointers
  static const uint32_t K_DEFAULT_BYTE_ALIGNMENT = 8;

  // Min chunk allocation is 8common::Constants::KB
  static const std::size_t K_MIN_CHUNK_ALLOCATION = 8 * 1024;

  // Max chunk allocation is 1common::Constants::MB
  static const std::size_t K_MAX_CHUNK_ALLOCATION = 1 * 1024 * 1024;

  // The name of the region
  const std::string name_;

  // The number of bytes allocated by this region
  std::size_t allocated_;

  // Bytes wasted due to alignment
  std::size_t alignment_waste_;

  // The total number of chunk bytes. This may include bytes not yet given out
  // by the region
  std::size_t chunk_bytes_allocated_;

  // The head of the chunk list
  Chunk *head_;

  // The position in the current free chunk where the next allocation can happen
  // These two fields make up the contiguous space [position, limit) where
  // allocations can happen from
  uintptr_t position_;
  uintptr_t end_;
};

/**
 * Base class for objects allocated from a region
 */
class RegionObject {
 public:
  /**
   * Should not be called
   */
  void *operator new(std::size_t size) = delete;

  /**
   * Should not be called
   */
  void operator delete(void *ptr) = delete;

  /**
   * Allocation
   * @param size size of buffer to allocate
   * @param region region to use for allocation
   * @return pointer to allocated buffer
   */
  void *operator new(std::size_t size, Region *region) { return region->Allocate(size); }

  // Objects from a Region shouldn't be deleted individually. They'll be deleted
  // when the region is destroyed. You can invoke this behavior manually by
  // calling Region::FreeAll().
  /**
   * Should not be called.
   */
  void operator delete(UNUSED_ATTRIBUTE void *ptr, UNUSED_ATTRIBUTE Region *region) {
    UNREACHABLE("Calling \"delete\" on region object is forbidden!");
  }
};

}  // namespace terrier::execution::util
