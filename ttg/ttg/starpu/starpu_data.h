// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_DATA_H
#define TTG_STARPU_DATA_H

#include "ttg/buffer.h"
#include <starpu.h>

namespace ttg_starpu::detail {
  
  /* Iterate over all buffers in a value and apply a function to each.
   * Stub implementation for StarPU.
   * 
   * TODO: This function may need active implementation if device data tracking
   * becomes necessary for StarPU. Currently, all calls are commented out in
   * the main TTG code. */
  template<typename Value, typename Fn>
  void foreach_starpu_data(Value&& value, Fn&& fn) {
    /* For StarPU, this function is currently a no-op.
     * Device data tracking is handled differently in StarPU
     * compared to PaRSEC. This function exists for API compatibility. */
    
    if constexpr (ttg::detail::has_buffer_apply_v<Value>) {
      ttg::detail::buffer_apply(value, [&]<typename B>(B&& b){
        // TODO: Implement StarPU-specific data tracking if needed
        // For now, do nothing - StarPU handles data differently
      });
    }
  }

  /* Find the latest data copy on a device for StarPU.
   * 
   * TODO: This is a stub for PaRSEC compatibility.
   * StarPU uses a different data model (handles instead of explicit copies).
   * If device-specific data location tracking becomes necessary,
   * this should be reimplemented using StarPU's data handles.
   * 
   * Currently returns {0, nullptr} (host device, no explicit copy).
   */
  inline std::tuple<int, void*> find_device_copy(void* data_handle) {
    // TODO: Implement StarPU data copy location tracking when needed
    // For now, always return host device (index 0)
    return {0, nullptr};
  }

} // namespace ttg_starpu::detail

#endif // TTG_STARPU_DATA_H
