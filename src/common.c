/*-
* Copyright (c) 2026 burst-camel contributors.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "common.h"

#include <stdlib.h>
#include <string.h>

#define CAMEL_DEFAULT_STREAM_SIZE 1024

static void camel_put_2(uint8_t* ptr, uint16_t val)
{
	ptr[0] = (uint8_t)(val >> 8);
	ptr[1] = (uint8_t)val;
}

static uint16_t camel_get_2(const uint8_t* ptr)
{
	return ((uint16_t)ptr[0] << 8) + (uint16_t)ptr[1];
}

static void camel_put_4(uint8_t* ptr, uint32_t val)
{
	ptr[0] = (uint8_t)(val >> 24);
	ptr[1] = (uint8_t)(val >> 16);
	ptr[2] = (uint8_t)(val >> 8);
	ptr[3] = (uint8_t)val;
}

static uint32_t camel_get_4(const uint8_t* ptr)
{
	return ((uint32_t)ptr[0] << 24) + ((uint32_t)ptr[1] << 16) + ((uint32_t)ptr[2] << 8) + (uint32_t)ptr[3];
}

static void camel_put_8(uint8_t* ptr, uint64_t val)
{
	ptr[0] = (uint8_t)(val >> 56);
	ptr[1] = (uint8_t)(val >> 48);
	ptr[2] = (uint8_t)(val >> 40);
	ptr[3] = (uint8_t)(val >> 32);
	ptr[4] = (uint8_t)(val >> 24);
	ptr[5] = (uint8_t)(val >> 16);
	ptr[6] = (uint8_t)(val >> 8);
	ptr[7] = (uint8_t)val;
}

static uint64_t camel_get_8(const uint8_t* ptr)
{
	return ((uint64_t)ptr[0] << 56) + ((uint64_t)ptr[1] << 48) + ((uint64_t)ptr[2] << 40) + ((uint64_t)ptr[3] << 32)
		+ ((uint64_t)ptr[4] << 24) + ((uint64_t)ptr[5] << 16) + ((uint64_t)ptr[6] << 8) + (uint64_t)ptr[7];
}

static void camel_stream_resize(camel_bin_stream_t* strm, size_t needed)
{
	size_t alloc_size = strm->size;
	if (alloc_size == 0)
		alloc_size = CAMEL_DEFAULT_STREAM_SIZE;

	while (needed > alloc_size)
		alloc_size *= 2;

	strm->data = (uint8_t*)realloc(strm->data, alloc_size);
	strm->size = alloc_size;
}

int camel_bin_stream_init(camel_bin_stream_t* strm)
{
	if (strm == NULL)
		return -1;

	memset(strm, 0, sizeof(*strm));
	strm->size = CAMEL_DEFAULT_STREAM_SIZE;
	strm->data = (uint8_t*)malloc(strm->size);
	if (strm->data == NULL) {
		strm->size = 0;
		return -1;
	}
	return 0;
}

void camel_bin_stream_destroy(camel_bin_stream_t* strm)
{
	if (strm == NULL)
		return;
	free(strm->data);
	memset(strm, 0, sizeof(*strm));
}

void camel_bin_stream_rewind(camel_bin_stream_t* strm, int reset)
{
	if (strm == NULL)
		return;
	if (reset) {
		strm->used = 0;
	}
	strm->rpos = 0;
}

void camel_mach_uint16_write(camel_bin_stream_t* strm, uint16_t val)
{
	if (strm == NULL)
		return;
	if (strm->used + 2 > strm->size)
		camel_stream_resize(strm, strm->used + 2);
	camel_put_2(strm->data + strm->used, val);
	strm->used += 2;
}

void camel_mach_uint16_read(camel_bin_stream_t* strm, uint16_t* val)
{
	if (val == NULL) 
		return;
	*val = 0;
	if (strm == NULL)
		return;
	if (strm->rpos + 2 > strm->used)
		return;
	*val = camel_get_2(strm->data + strm->rpos);
	strm->rpos += 2;
}

void camel_mach_uint32_write(camel_bin_stream_t* strm, uint32_t val)
{
	if (strm == NULL)
		return;
	if (strm->used + 4 > strm->size)
		camel_stream_resize(strm, strm->used + 4);
	camel_put_4(strm->data + strm->used, val);
	strm->used += 4;
}

void camel_mach_uint32_read(camel_bin_stream_t* strm, uint32_t* val)
{
	if (val == NULL)
		return;
	*val = 0;
	if (strm == NULL)
		return;
	if (strm->rpos + 4 > strm->used)
		return;
	*val = camel_get_4(strm->data + strm->rpos);
	strm->rpos += 4;
}

void camel_mach_uint64_write(camel_bin_stream_t* strm, uint64_t val)
{
	if (strm == NULL)
		return;
	if (strm->used + 8 > strm->size)
		camel_stream_resize(strm, strm->used + 8);
	camel_put_8(strm->data + strm->used, val);
	strm->used += 8;
}

void camel_mach_uint64_read(camel_bin_stream_t* strm, uint64_t* val)
{
	if (val == NULL)
		return;
	*val = 0;
	if (strm == NULL)
		return;
	if (strm->rpos + 8 > strm->used)
		return;
	*val = camel_get_8(strm->data + strm->rpos);
	strm->rpos += 8;
}

void camel_feedback_msg_encode(camel_bin_stream_t* strm, camel_feedback_msg_t* msg)
{
	camel_bin_stream_rewind(strm, 1);
	camel_mach_uint32_write(strm, msg->frame_id);
	camel_mach_uint32_write(strm, (uint32_t)msg->frame_size);
	camel_mach_uint32_write(strm, msg->packet_count);
	camel_mach_uint32_write(strm, msg->first_packet_size);
	camel_mach_uint16_write(strm, msg->first_transport_seq);
	camel_mach_uint16_write(strm, msg->last_transport_seq);
	camel_mach_uint64_write(strm, msg->first_ts);
	camel_mach_uint64_write(strm, msg->last_ts);
	camel_mach_uint64_write(strm, msg->feedback_send_ts_us);
	camel_mach_uint32_write(strm, msg->interval_count);
	for (uint32_t i = 0; i < msg->interval_count && i < CAMEL_FEEDBACK_MAX_INTERVALS; i++)
		camel_mach_uint32_write(strm, msg->interval_received_bytes[i]);
}

void camel_feedback_msg_decode(camel_bin_stream_t* strm, camel_feedback_msg_t* msg)
{
	uint32_t frame_size;

	camel_mach_uint32_read(strm, &msg->frame_id);
	camel_mach_uint32_read(strm, &frame_size);
	msg->frame_size = frame_size;
	camel_mach_uint32_read(strm, &msg->packet_count);
	camel_mach_uint32_read(strm, &msg->first_packet_size);
	camel_mach_uint16_read(strm, &msg->first_transport_seq);
	camel_mach_uint16_read(strm, &msg->last_transport_seq);
	camel_mach_uint64_read(strm, &msg->first_ts);
	camel_mach_uint64_read(strm, &msg->last_ts);
	camel_mach_uint64_read(strm, &msg->feedback_send_ts_us);
	camel_mach_uint32_read(strm, &msg->interval_count);
	if (msg->interval_count > CAMEL_FEEDBACK_MAX_INTERVALS)
		msg->interval_count = CAMEL_FEEDBACK_MAX_INTERVALS;
	for (uint32_t i = 0; i < msg->interval_count; i++)
		camel_mach_uint32_read(strm, &msg->interval_received_bytes[i]);
}

void camel_group_feedback_msg_encode(camel_bin_stream_t* strm, camel_group_feedback_msg_t* msg)
{
	camel_bin_stream_rewind(strm, 1);
	camel_mach_uint32_write(strm, msg->group_id);
	camel_mach_uint32_write(strm, msg->group_size_bytes);
	camel_mach_uint32_write(strm, msg->packet_count);
	camel_mach_uint32_write(strm, msg->first_packet_size);
	camel_mach_uint64_write(strm, msg->first_recv_ts_us);
	camel_mach_uint64_write(strm, msg->last_recv_ts_us);
	camel_mach_uint32_write(strm, msg->interval_count);
	for (uint32_t i = 0; i < msg->interval_count && i < CAMEL_FEEDBACK_MAX_INTERVALS; i++)
		camel_mach_uint32_write(strm, msg->interval_received_bytes[i]);
}

void camel_group_feedback_msg_decode(camel_bin_stream_t* strm, camel_group_feedback_msg_t* msg)
{
	memset(msg, 0, sizeof(*msg));
	camel_mach_uint32_read(strm, &msg->group_id);
	camel_mach_uint32_read(strm, &msg->group_size_bytes);
	camel_mach_uint32_read(strm, &msg->packet_count);
	camel_mach_uint32_read(strm, &msg->first_packet_size);
	camel_mach_uint64_read(strm, &msg->first_recv_ts_us);
	camel_mach_uint64_read(strm, &msg->last_recv_ts_us);
	camel_mach_uint32_read(strm, &msg->interval_count);
	if (msg->interval_count > CAMEL_FEEDBACK_MAX_INTERVALS)
		msg->interval_count = CAMEL_FEEDBACK_MAX_INTERVALS;
	for (uint32_t i = 0; i < msg->interval_count; i++)
		camel_mach_uint32_read(strm, &msg->interval_received_bytes[i]);
}

void camel_transport_feedback_msg_encode(camel_bin_stream_t* strm, camel_transport_feedback_msg_t* msg)
{
	camel_bin_stream_rewind(strm, 1);
	camel_mach_uint16_write(strm, msg->sample_count);
	for (uint32_t i = 0; i < (uint32_t)msg->sample_count && i < CAMEL_TRANSPORT_FEEDBACK_MAX_SAMPLES; i++) {
		camel_mach_uint16_write(strm, msg->samples[i].transport_seq);
		camel_mach_uint64_write(strm, msg->samples[i].recv_ts_us);
	}
}

void camel_transport_feedback_msg_decode(camel_bin_stream_t* strm, camel_transport_feedback_msg_t* msg)
{
	uint16_t count;
	memset(msg, 0, sizeof(*msg));
	camel_mach_uint16_read(strm, &count);
	if (count > CAMEL_TRANSPORT_FEEDBACK_MAX_SAMPLES)
		count = CAMEL_TRANSPORT_FEEDBACK_MAX_SAMPLES;
	msg->sample_count = count;
	for (uint32_t i = 0; i < (uint32_t)msg->sample_count; i++) {
		camel_mach_uint16_read(strm, &msg->samples[i].transport_seq);
		camel_mach_uint64_read(strm, &msg->samples[i].recv_ts_us);
	}
}

void camel_cumack_msg_encode(camel_bin_stream_t* strm, camel_cumack_msg_t* msg)
{
	camel_bin_stream_rewind(strm, 1);
	camel_mach_uint16_write(strm, msg->largest_acked_seq);
}

void camel_cumack_msg_decode(camel_bin_stream_t* strm, camel_cumack_msg_t* msg)
{
	memset(msg, 0, sizeof(*msg));
	camel_mach_uint16_read(strm, &msg->largest_acked_seq);
}

void camel_ack_ranges_msg_encode(camel_bin_stream_t* strm, camel_ack_ranges_msg_t* msg)
{
	camel_bin_stream_rewind(strm, 1);
	camel_mach_uint16_write(strm, msg->range_count);
	for (uint32_t i = 0; i < (uint32_t)msg->range_count && i < CAMEL_ACK_RANGES_MAX_RANGES; i++) {
		camel_mach_uint16_write(strm, msg->ranges[i].start_seq);
		camel_mach_uint16_write(strm, msg->ranges[i].end_seq);
	}
}

void camel_ack_ranges_msg_decode(camel_bin_stream_t* strm, camel_ack_ranges_msg_t* msg)
{
	uint16_t count;
	memset(msg, 0, sizeof(*msg));
	camel_mach_uint16_read(strm, &count);
	if (count > CAMEL_ACK_RANGES_MAX_RANGES)
		count = CAMEL_ACK_RANGES_MAX_RANGES;
	msg->range_count = count;
	for (uint32_t i = 0; i < (uint32_t)msg->range_count; i++) {
		camel_mach_uint16_read(strm, &msg->ranges[i].start_seq);
		camel_mach_uint16_read(strm, &msg->ranges[i].end_seq);
	}
}
