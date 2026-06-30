// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_BUFFER_H
#define TTG_STARPU_BUFFER_H

#include <array>
#include <vector>
#include <cassert>
#include <memory>
#include <starpu.h>

#include "ttg/starpu/ttg_data_copy.h"
#include "ttg/starpu/starpu-ext.h"
#include "ttg/util/iovec.h"
#include "ttg/device/device.h"
#include "ttg/starpu/device.h"
#include "ttg/devicescope.h"

namespace ttg_starpu {

namespace detail {
  // fwd decl
  template<typename T, typename A>
  void* get_starpu_data(const ttg_starpu::Buffer<T, A>& db);

  template<typename T>
  struct empty_allocator {
    using value_type = std::decay_t<T>;

    value_type* allocate(std::size_t size) {
      throw std::runtime_error("Allocate on empty allocator!");
    }

    void deallocate(value_type* ptr, std::size_t size) {
      /* nothing to be done */
    }
  };

  /* overloads for pointers and smart pointers */
  template<typename T>
  inline T* to_address(T* ptr) {
    return ptr;
  }

  template<typename T>
  inline auto to_address(T&& ptr) {
    return ptr.get();
  }

  /**
   * StarPU buffer data wrapper.
   * TODO: Implement full StarPU data handle integration if needed.
   */
  template<typename PtrT, typename Allocator>
  struct ttg_starpu_data_types {
    using allocator_traits = std::allocator_traits<Allocator>;
    using allocator_type = typename allocator_traits::allocator_type;
    using value_type = typename allocator_traits::value_type;

    // TODO: Look if we need to use ttg::device::available_space
    static constexpr bool always_allocate_on_host = true;

    struct data_copy_type {
      PtrT ptr;
      std::size_t size;
      void* host_ptr = nullptr;
      starpu_data_handle_t handle = nullptr;
      
      data_copy_type(PtrT p, std::size_t s) : ptr(p), size(s) {
        host_ptr = to_address(ptr);
        if (host_ptr != nullptr) {
          starpu_vector_data_register(&handle, STARPU_MAIN_RAM,
                                      reinterpret_cast<uintptr_t>(host_ptr),
                                      static_cast<uint32_t>(s), sizeof(value_type));
        }
      }
    };

    static void* create_data(std::size_t size, ttg::scope scope) {
      try {
        auto ptr = std::make_shared<value_type[]>(size);
        auto* data = new data_copy_type(ptr, size);
        return data;
      } catch (...) {
        throw std::bad_alloc();
      }
    }

    template<typename PtrType>
    static void* create_data(PtrType& ptr, std::size_t size, ttg::scope scope) {
      auto* data = new data_copy_type(ptr, size);
      return data;
    }

    static void release_data(void* data_ptr) {
      if (data_ptr) {
        auto* copy = static_cast<data_copy_type*>(data_ptr);
        if (copy->handle) {
          starpu_data_unregister(copy->handle);
        }
        delete copy;
      }
    }
  };

} // namespace detail

/**
 * A buffer that is mirrored between host memory
 * and different devices. The runtime is free to
 * move data between device and host memory based
 * on where the tasks are executing.
 *
 * Note that a buffer is movable and should not
 * be shared between two objects (e.g., through a pointer)
 * in order for TTG to properly facilitate ownership
 * tracking of the containing object. * A buffer that manages memory for TTG on StarPU.
 * 
 * TODO: Integrate with StarPU data handles if needed.
 * Currently all data is stored on host memory.
 */
template<typename T, typename Allocator>
struct Buffer {

  using value_type = std::remove_all_extents_t<T>;
  using pointer_type = std::add_pointer_t<value_type>;
  using const_pointer_type = const std::remove_const_t<value_type>*;
  using element_type = std::decay_t<T>;

  static_assert(std::is_trivially_copyable_v<element_type>,
                "Only trivially copyable types are supported.");

private:
  void* m_data = nullptr;
  std::size_t m_count = 0;
  starpu_data_handle_t m_handle = nullptr;
  
  friend void* detail::get_starpu_data<T>(const ttg_starpu::Buffer<T, Allocator>&);

  void release_data() {
    if (nullptr == m_data) return;
    detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::release_data(m_data);
    m_data = nullptr;
  }

public:

  Buffer() = default;

  Buffer(std::size_t n, ttg::scope scope = ttg::scope::SyncIn)
    : m_data(detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::create_data(n, scope))
    , m_count(n)
  { }

  Buffer(std::shared_ptr<value_type[]> ptr, std::size_t n,
         ttg::scope scope = ttg::scope::SyncIn)
    : m_data(detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>,
                                           detail::empty_allocator<element_type>>::create_data(ptr, n, scope))
    , m_count(n)
  { }

  template<typename Deleter>
  Buffer(std::unique_ptr<value_type[], Deleter> ptr, std::size_t n,
         ttg::scope scope = ttg::scope::SyncIn)
    : m_data(detail::ttg_starpu_data_types<std::unique_ptr<value_type[], Deleter>,
                                           detail::empty_allocator<element_type>>::create_data(ptr, n, scope))
    , m_count(n)
  { }

  virtual ~Buffer() {
    unpin();  // make sure the copies are not pinned
    release_data();
  }

  /* Move constructor */
  Buffer(Buffer&& db)
    : m_data(db.m_data), m_count(db.m_count)
  {
    db.m_data = nullptr;
    db.m_count = 0;
  }

  Buffer(const Buffer& db) = delete;

  /* Move assignment */
  Buffer& operator=(Buffer&& db) {
    std::swap(m_data, db.m_data);
    std::swap(m_count, db.m_count);
    return *this;
  }

  Buffer& operator=(const Buffer& db) = delete;

  /* Set the current device owner
   * TODO: Implement StarPU device tracking */
  void set_owner_device(const ttg::device::Device& device) {
  }

  /* Check if data is current on device
   * StarPU handles this automatically via the data handle.
   */
  bool is_current_on(ttg::device::Device dev) const {
    if (empty()) return true;
    return true; // StarPU ensures coherency on demand
  }

  /* Get owner device
   * StarPU handles this internally.
   */
  ttg::device::Device get_owner_device() const {
    return ttg::device::current_device();
  }

  /* Get the pointer on the currently active device. */
  pointer_type current_device_ptr() {
    if (empty()) return nullptr;
    auto* data = static_cast<detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::data_copy_type*>(m_data);
    void* ptr = nullptr;
    starpu_data_get_local_ptr(data->handle, &ptr);
    return static_cast<pointer_type>(ptr);
  }

  const_pointer_type current_device_ptr() const {
    if (empty()) return nullptr;
    auto* data = static_cast<const detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::data_copy_type*>(m_data);
    void* ptr = nullptr;
    starpu_data_get_local_ptr(data->handle, &ptr);
    return static_cast<const_pointer_type>(ptr);
  }

  pointer_type owner_device_ptr() {
    return current_device_ptr();
  }

  const_pointer_type owner_device_ptr() const {
    return current_device_ptr();
  }

  pointer_type device_ptr_on(const ttg::device::Device& device) {
    return current_device_ptr();
  }

  const_pointer_type device_ptr_on(const ttg::device::Device& device) const {
    return current_device_ptr();
  }

  pointer_type host_ptr() {
    if (empty()) return nullptr;
    auto* data = static_cast<detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::data_copy_type*>(m_data);
    return static_cast<pointer_type>(data->host_ptr);
  }

  const_pointer_type host_ptr() const {
    if (empty()) return nullptr;
    auto* data = static_cast<const detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::data_copy_type*>(m_data);
    return static_cast<const_pointer_type>(data->host_ptr);
  }

  /* Check if valid on device
   * StarPU handles validity automatically.
   */
  bool is_valid_on(const ttg::device::Device& device) const {
    return is_valid();
  }

  /* Allocate on device
   * StarPU handles allocation on-demand.
   */
  void allocate_on(const ttg::device::Device& device) {
    if (!m_data && m_count > 0) {
      throw std::runtime_error("Cannot allocate on an empty buffer!");
    }
  }

  /* Pin memory on all devices - TODO */
  void pin() {
    // TODO: Implement StarPU memory pinning if needed
  }

  /* Unpin memory from all devices - TODO */
  void unpin() {
    // TODO: Implement StarPU memory unpinning if needed
  }

  void pin_on(int device_id) {
    // TODO: Device-specific pinning
  }

  void unpin_on(int device_id) {
    // TODO: Device-specific unpinning
  }

  bool is_valid() const {
    return (m_count == 0 || m_data != nullptr);
  }

  operator bool() const {
    return !empty();
  }

  std::size_t size() const {
    return m_count;
  }

  bool empty() const {
    return m_count == 0;
  }

  /* Reallocate the buffer with count elements */
  void reset(std::size_t n, ttg::scope scope = ttg::scope::SyncIn) {
    release_data();
    m_data = detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>, Allocator>::create_data(n, scope);
    m_count = n;
  }

  void reset(std::shared_ptr<value_type[]> ptr, std::size_t n, ttg::scope scope = ttg::scope::SyncIn) {
    release_data();
    m_data = detail::ttg_starpu_data_types<std::shared_ptr<value_type[]>,
                                           detail::empty_allocator<element_type>>::create_data(ptr, n, scope);
    m_count = n;
  }

  /**
   * Clears the buffer. After this operation, the buffer is empty.
   */
  void clear() {
    release_data();
    m_count = 0;
  }

  /* Reset scope
   * TODO: Implement StarPU scope semantics */
  void reset_scope(ttg::scope scope) {
    // TODO: Update StarPU data state based on scope
  }

  ttg::scope scope() const {
    return ttg::scope::SyncIn;
  }

  void prefer_device(ttg::device::Device dev) {
    // TODO: Hint to StarPU about preferred device
  }

  void add_device(ttg::device::Device dev, pointer_type ptr, bool is_current = false) {
    // TODO: Add device copy with StarPU
    throw std::runtime_error("add_device not yet implemented for StarPU");
  }

#ifdef TTG_SERIALIZATION_SUPPORTS_BOOST
  template <typename Archive>
  void serialize(Archive& ar, const unsigned int version) {
    if constexpr (ttg::detail::is_output_archive_v<Archive>) {
      std::size_t s = size();
      ar& s;
    } else {
      std::size_t s;
      ar & s;
      reset(s);
    }
  }
#endif

#ifdef TTG_SERIALIZATION_SUPPORTS_MADNESS
  template <typename Archive>
  std::enable_if_t<std::is_base_of_v<madness::archive::BufferInputArchive, Archive> ||
                   std::is_base_of_v<madness::archive::BufferOutputArchive, Archive>>
  serialize(Archive& ar) {
    if constexpr (ttg::detail::is_output_archive_v<Archive>) {
      std::size_t s = size();
      ar& s;
    } else {
      std::size_t s;
      ar & s;
      if (m_data != nullptr) {
        if (s != size()) {
          throw std::runtime_error("Buffer size mismatch in deserialization!");
        }
      } else {
        reset(s, ttg::scope::Allocate);
      }
    }
  }
#endif

};

namespace detail {
  template<typename T, typename A>
  void* get_starpu_data(const ttg_starpu::Buffer<T, A>& db) {
    return const_cast<void*>(reinterpret_cast<const void*>(&db.m_handle));
  }
} // namespace detail

} // namespace ttg_starpu

#endif // TTG_STARPU_BUFFER_H
