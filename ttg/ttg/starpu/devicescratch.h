// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_DEVICESCRATCH_H
#define TTG_STARPU_DEVICESCRATCH_H

#include <array>
#include <starpu.h>
#include <ttg/devicescope.h>

namespace ttg_starpu {

namespace detail {
  // fwd decl
  template<typename T>
  void* get_starpu_data(const ttg_starpu::devicescratch<T>&);
} // namespace detail

/**
 * TTG will allocate memory on the device
 * and transfer data in and out based on the scope.
 */
template<typename T>
struct devicescratch {

  using element_type = std::decay_t<T>;

  static_assert(std::is_trivially_copyable_v<element_type>,
                "Only trivially copyable types are supported for devices.");
  static_assert(std::is_default_constructible_v<element_type>,
                "Only default constructible types are supported for devices.");

private:

  element_type* m_ptr = nullptr;
  ttg::scope m_scope = ttg::scope::SyncIn;
  std::size_t m_count = 1;
  // TODO: Add StarPU data handle when device support is implemented
  // starpu_data_handle_t m_handle = nullptr;

public:
  /* Constructing a devicescratch using application-managed memory.
   * The memory pointed to by ptr must be accessible during
   * the life-time of the devicescratch. */
  devicescratch(element_type* ptr, ttg::scope scope = ttg::scope::SyncIn, std::size_t count = 1)
  : m_ptr(ptr)
  , m_scope(scope)
  , m_count(count)
  {
    // TODO: Register memory with StarPU when device support is enabled
  }
  /* don't allow moving */
  devicescratch(devicescratch&&) = delete;
  /* don't allow copying */
  devicescratch(const devicescratch& db) = delete;
  /* don't allow moving */
  devicescratch& operator=(devicescratch&&) = delete;
  /* don't allow copying */
  devicescratch& operator=(const devicescratch& db) = delete;

  ~devicescratch() {
    // TODO: Unregister memory from StarPU
    m_ptr = nullptr;
  }


   // TODO: With StarPU device support, this would acquire device memory pointer 
  element_type* device_ptr() {
    return m_ptr;
  }

   // TODO: With StarPU device support, this would acquire device memory pointer 
  const element_type* device_ptr() const {
    return m_ptr;
  }


   // TODO: Implement with StarPU device queries 
  bool is_valid() const {
    return (m_ptr != nullptr);
  }

  ttg::scope scope() const {
    return m_scope;
  }

  std::size_t size() const {
    return m_count;
  }

};

namespace detail {
  template<typename T>
  void* get_starpu_data(const ttg_starpu::devicescratch<T>& scratch) {
    // TODO: Return StarPU data handle when implemented
    return const_cast<typename ttg_starpu::devicescratch<T>::element_type*>(scratch.m_ptr);
  }
} // namespace detail

} // namespace ttg_starpu

#endif // TTG_STARPU_DEVICESCRATCH_H
