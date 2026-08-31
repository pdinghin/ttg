// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_EXT_H
#define TTG_STARPU_EXT_H

#include <cstdint>
#include <starpu.h>

/* HACK: we need this flag on a data copy to indicate whether it has been registered */
#define TTG_STARPU_DATA_FLAG_REGISTERED        (static_cast<uint32_t>(1 << 2))

/* HACK: mark the flows of device scratch as temporary so that we can easily discard it */
#define TTG_STARPU_FLOW_ACCESS_TMP             (static_cast<uint8_t>(1 << 7))

typedef struct starpu_task starpu_task_t;

typedef enum starpu_hook_return_e {
    STARPU_HOOK_RETURN_DONE    =  0,  /* This execution succeeded */
    STARPU_HOOK_RETURN_AGAIN   = -1,  /* Reschedule later */
    STARPU_HOOK_RETURN_NEXT    = -2,  /* Try next variant [if any] */
    STARPU_HOOK_RETURN_DISABLE = -3,  /* Disable the device, something went wrong */
    STARPU_HOOK_RETURN_ASYNC   = -4,  /* The task is outside our reach, the completion will
                                       * be triggered asynchronously. */
    STARPU_HOOK_RETURN_ERROR   = -5,  /* Some other major error happened */
} starpu_hook_return_t;

typedef struct starpu_codelet starpu_codelet_t;
#define starpu_nb_devices 1 //TODO: change with a function which give the number of devices

#endif // TTG_STARPU_EXT_H