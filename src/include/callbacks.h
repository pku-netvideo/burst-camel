/*-
* Copyright (c) 2026 burst-camel contributors.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_callbacks_h_
#define __camel_callbacks_h_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*camel_bitrate_changed_func)(void* trigger, uint32_t bitrate, uint8_t fraction_loss, uint32_t rtt);
typedef void (*camel_pace_send_func)(void* handler, uint32_t packet_id, int retrans, size_t size, int padding);
typedef void (*camel_app_layer_predict_func)(void* trigger, int32_t target_rate, double* ssim, double* pssim,
	double* size_u, double* size_sigma2);

#ifdef __cplusplus
}
#endif

#endif
