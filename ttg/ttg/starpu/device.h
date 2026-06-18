// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_DEVICE_H
#define TTG_STARPU_DEVICE_H

#include "ttg/device/device.h"
#include <starpu.h>

namespace ttg_starpu {

  namespace detail {

    // the first ID of an accelerator device in the StarPU ID-space (host is 0)
    inline int first_device_id = 1;
    
    //Get the number of available StarPU devices (excluding host)
    inline int get_starpu_device_count() {
      return starpu_worker_get_count() - 1;
    }

    /**
     * map from TTG ID-space to StarPU device ID-space
     */
    inline
    int ttg_device_to_starpu_device(const ttg::device::Device& device) {
      if (device.is_host()) {
        return 0; 
      } else {
        return device.id() + first_device_id;  
      }
    }

    /**
     * map from StarPU device ID-space to TTG ID-space
     */
    inline
    ttg::device::Device starpu_device_to_ttg_device(int starpu_id) {
      if (starpu_id == 0) {
        return ttg::device::Device(starpu_id, ttg::ExecutionSpace::Host);
      }
      return ttg::device::Device(starpu_id - first_device_id,
                                ttg::device::available_execution_space);
    }
  } // namespace detail


  inline
  int num_devices() {
    return detail::get_starpu_device_count();
  }

} // namespace ttg_starpu

#endif // TTG_STARPU_DEVICE_H