/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_pacer_h_
#define __camel_pacer_h_

#include <stdint.h>
#include <stddef.h>

#include "callbacks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct camel_pacer_packet_t
{
	uint32_t packet_id;
	int retrans;
	size_t size;
	int padding;
	int64_t enqueue_ts_ms;
} camel_pacer_packet_t;

typedef struct camel_pacer_t
{
	void* handler;
	camel_pace_send_func send_cb;

	camel_pacer_packet_t* queue;
	uint32_t capacity;
	uint32_t size;
	uint32_t head;
	uint32_t tail;

	uint32_t pacing_bitrate_bps;
	uint32_t min_pacing_bitrate_bps;
	size_t congestion_window_bytes;
	size_t outstanding_bytes;
	size_t max_burst_bytes;

	int64_t last_update_ts_ms;
	int64_t budget_bytes;
} camel_pacer_t;

int camel_pacer_init(camel_pacer_t* p, void* handler, camel_pace_send_func send_cb, uint32_t capacity);
void camel_pacer_destroy(camel_pacer_t* p);

void camel_pacer_set_estimate_bitrate(camel_pacer_t* p, uint32_t bitrate_bps);
void camel_pacer_set_bitrate_limits(camel_pacer_t* p, uint32_t min_bitrate_bps);
void camel_pacer_set_congestion_window(camel_pacer_t* p, size_t cwnd_bytes);
void camel_pacer_set_outstanding(camel_pacer_t* p, size_t outstanding_bytes);
void camel_pacer_set_max_burst_bytes(camel_pacer_t* p, size_t max_burst_bytes);

int camel_pacer_insert_packet(camel_pacer_t* p, uint32_t packet_id, int retrans, size_t size, int padding, int64_t now_ts_ms);
void camel_pacer_try_transmit(camel_pacer_t* p, int64_t now_ts_ms);

#ifdef __cplusplus
}
#endif

#endif
