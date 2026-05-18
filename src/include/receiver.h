/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_receiver_h_
#define __camel_receiver_h_

#include <stdint.h>
#include <stddef.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*camel_send_feedback_func)(void* handler, const uint8_t* payload, int payload_size);

typedef struct
{
	void* handler;
	camel_send_feedback_func send_cb;

	camel_bin_stream_t strm;

	uint32_t cur_frame_id;
	uint32_t cur_frame_size;
	uint32_t cur_frame_packet_count;
	uint32_t cur_frame_first_packet_size;
	uint32_t cur_frame_interval_count;
	uint32_t cur_frame_interval_received_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
	uint64_t first_ts;
	uint64_t last_ts;
} camel_receiver_t;

camel_receiver_t* camel_receiver_create(void* handler, camel_send_feedback_func cb);
void camel_receiver_destroy(camel_receiver_t* r);

void camel_receiver_on_received_frame_info(camel_receiver_t* r, uint32_t frame_id, size_t size);

#ifdef __cplusplus
}
#endif

#endif
