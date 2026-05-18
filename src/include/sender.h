/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_sender_h_
#define __camel_sender_h_

#include <stdint.h>
#include <stddef.h>

#include "bandwidth_estimator.h"
#include "congestion_detector.h"
#include "burst_controller.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*camel_bitrate_changed_func)(void* trigger, uint32_t bitrate, uint8_t fraction_loss, uint32_t rtt);
typedef void (*camel_pace_send_func)(void* handler, uint32_t packet_id, int retrans, size_t size, int padding);
typedef void (*camel_app_layer_predict_func)(void* trigger, int32_t target_rate, double* ssim, double* pssim,
	double* size_u, double* size_sigma2);

#define CAMEL_MAX_FRAME_NUM 300

typedef struct
{
	uint32_t frame_id[CAMEL_MAX_FRAME_NUM];
	uint64_t frame_send_ts[CAMEL_MAX_FRAME_NUM];
} camel_frame_info_t;

typedef struct
{
	void* trigger;
	camel_bitrate_changed_func trigger_cb;
	void* handler;
	camel_pace_send_func send_cb;
	camel_app_layer_predict_func app_func;

	uint32_t last_bitrate_bps;
	int64_t notify_ts_ms;

	uint32_t max_bitrate;
	uint32_t min_bitrate;

	camel_bin_stream_t strm;

	camel_frame_info_t fit;
	camel_estimator_t estimator;
	camel_congestion_detector_t detector;
	camel_burst_controller_t burst_ctrl;

	size_t congestion_window_bytes;
	uint32_t camel_target_bitrate_bps;
	double camel_gamma;
	double camel_last_slope_us_per_byte;
} camel_sender_t;

camel_sender_t* camel_sender_create(void* trigger,
	camel_bitrate_changed_func bitrate_cb,
	void* handler,
	camel_pace_send_func send_cb,
	int queue_ms,
	int padding,
	camel_app_layer_predict_func app_func);

void camel_sender_destroy(camel_sender_t* s);

void camel_sender_send_frame(camel_sender_t* s, uint32_t frame_id, size_t size);
void camel_sender_on_feedback(camel_sender_t* s, const uint8_t* feedback, int feedback_size);
void camel_sender_heartbeat(camel_sender_t* s, int64_t now_ts_ms);

#ifdef __cplusplus
}
#endif

#endif
