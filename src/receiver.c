/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "receiver.h"
#include "burst_controller.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

static uint64_t camel_get_sys_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static uint64_t camel_min_u64(uint64_t a, uint64_t b)
{
	return a < b ? a : b;
}

static uint64_t camel_max_u64(uint64_t a, uint64_t b)
{
	return a > b ? a : b;
}

static size_t camel_min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static uint32_t camel_max_u32(uint32_t a, uint32_t b)
{
	return a > b ? a : b;
}

static void camel_receiver_record_interval_bytes(camel_receiver_t* r, size_t frame_offset, size_t size)
{
	while (size > 0) {
		uint32_t interval = (uint32_t)(frame_offset / CAMEL_BURST_INTERVAL_BYTES);
		size_t interval_end = ((size_t)interval + 1) * CAMEL_BURST_INTERVAL_BYTES;
		size_t bytes = camel_min_size(size, interval_end - frame_offset);

		if (interval >= CAMEL_FEEDBACK_MAX_INTERVALS)
			break;

		r->cur_frame_interval_received_bytes[interval] += (uint32_t)bytes;
		r->cur_frame_interval_count = camel_max_u32(r->cur_frame_interval_count, interval + 1);
		frame_offset += bytes;
		size -= bytes;
	}
}

static void camel_receiver_fill_feedback(camel_receiver_t* r, camel_feedback_msg_t* msg)
{
	memset(msg, 0, sizeof(*msg));
	msg->frame_id = r->cur_frame_id;
	msg->frame_size = r->cur_frame_size;
	msg->packet_count = r->cur_frame_packet_count;
	msg->first_packet_size = r->cur_frame_first_packet_size;
	msg->first_ts = r->first_ts;
	msg->last_ts = r->last_ts;
	msg->feedback_send_ts_us = camel_get_sys_us();
	msg->interval_count = r->cur_frame_interval_count;
	memcpy(msg->interval_received_bytes, r->cur_frame_interval_received_bytes,
		sizeof(uint32_t) * msg->interval_count);
}

static void camel_receiver_reset_frame_stats(camel_receiver_t* r, uint32_t frame_id, size_t size, uint64_t cur_ts)
{
	memset(r->cur_frame_interval_received_bytes, 0, sizeof(r->cur_frame_interval_received_bytes));
	r->cur_frame_id = frame_id;
	r->cur_frame_size = 0;
	r->cur_frame_interval_count = 0;
	camel_receiver_record_interval_bytes(r, r->cur_frame_size, size);
	r->cur_frame_size = (uint32_t)size;
	r->cur_frame_packet_count = 1;
	r->cur_frame_first_packet_size = (uint32_t)size;
	r->first_ts = cur_ts;
	r->last_ts = cur_ts;
}

camel_receiver_t* camel_receiver_create(void* handler, camel_send_feedback_func cb)
{
	camel_receiver_t* r = (camel_receiver_t*)calloc(1, sizeof(camel_receiver_t));
	if (r == NULL)
		return NULL;

	r->handler = handler;
	r->send_cb = cb;
	camel_bin_stream_init(&r->strm);
	return r;
}

void camel_receiver_destroy(camel_receiver_t* r)
{
	if (r == NULL)
		return;
	camel_bin_stream_destroy(&r->strm);
	free(r);
}

void camel_receiver_on_received_frame_info(camel_receiver_t* r, uint32_t frame_id, size_t size)
{
	uint64_t cur_ts = camel_get_sys_us();

	if (r == NULL)
		return;

	if (r->cur_frame_id == 0) {
		r->cur_frame_id = frame_id;
		r->cur_frame_size = 0;
		camel_receiver_record_interval_bytes(r, r->cur_frame_size, size);
		r->cur_frame_size += (uint32_t)size;
		r->cur_frame_packet_count = 1;
		r->cur_frame_first_packet_size = (uint32_t)size;
		r->first_ts = cur_ts;
		r->last_ts = camel_max_u64(r->last_ts, cur_ts);
	}
	else if (r->cur_frame_id < frame_id) {
		camel_feedback_msg_t msg;
		camel_receiver_fill_feedback(r, &msg);
		if (r->send_cb != NULL) {
			camel_feedback_msg_encode(&r->strm, &msg);
			r->send_cb(r->handler, r->strm.data, (int)r->strm.used);
		}
		camel_receiver_reset_frame_stats(r, frame_id, size, cur_ts);
	}
	else if (r->cur_frame_id == frame_id) {
		camel_receiver_record_interval_bytes(r, r->cur_frame_size, size);
		r->cur_frame_size = r->cur_frame_size + (uint32_t)size;
		r->cur_frame_packet_count++;
		r->first_ts = camel_min_u64(r->first_ts, cur_ts);
		r->last_ts = camel_max_u64(r->last_ts, cur_ts);
	}
}
