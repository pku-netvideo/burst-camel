/*-
* Copyright (c) 2026 burst-camel contributors.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "receiver.h"
#include "burst_controller.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#define CAMEL_RECEIVER_MAX_PACKETS 2048

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

typedef struct
{
	uint16_t transport_seq;
	uint32_t payload_size;
	uint64_t recv_ts_us;
} camel_receiver_packet_t;

struct camel_receiver_t
{
	void* handler;
	camel_send_feedback_func group_feedback_cb;
	camel_send_feedback_func packet_ack_cb;

	camel_bin_stream_t strm;

	uint32_t cur_group_id;
	camel_receiver_packet_t packets[CAMEL_RECEIVER_MAX_PACKETS];
	uint32_t packet_count;
	uint64_t first_recv_ts_us;
	uint64_t last_recv_ts_us;
};

static int camel_receiver_packet_seq_comp(const void* a, const void* b)
{
	const camel_receiver_packet_t* p1 = (const camel_receiver_packet_t*)a;
	const camel_receiver_packet_t* p2 = (const camel_receiver_packet_t*)b;
	if (p1->transport_seq < p2->transport_seq)
		return -1;
	if (p1->transport_seq > p2->transport_seq)
		return 1;
	return 0;
}

static void camel_receiver_reset_group(camel_receiver_t* r, uint32_t group_id)
{
	r->cur_group_id = group_id;
	r->packet_count = 0;
	r->first_recv_ts_us = 0;
	r->last_recv_ts_us = 0;
}

static void camel_receiver_send_packet_ack(camel_receiver_t* r, uint16_t transport_seq, uint64_t recv_ts_us)
{
	if (r == NULL || r->packet_ack_cb == NULL)
		return;

	camel_transport_feedback_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.sample_count = 1;
	msg.samples[0].transport_seq = transport_seq;
	msg.samples[0].recv_ts_us = recv_ts_us;
	camel_transport_feedback_msg_encode(&r->strm, &msg);
	r->packet_ack_cb(r->handler, r->strm.data, (int)r->strm.used);
}

static void camel_receiver_emit_group_feedback(camel_receiver_t* r)
{
	if (r == NULL || r->group_feedback_cb == NULL)
		return;
	if (r->packet_count == 0)
		return;

	camel_receiver_packet_t sorted[CAMEL_RECEIVER_MAX_PACKETS];
	uint32_t count = r->packet_count;
	if (count > CAMEL_RECEIVER_MAX_PACKETS)
		count = CAMEL_RECEIVER_MAX_PACKETS;
	memcpy(sorted, r->packets, sizeof(camel_receiver_packet_t) * count);
	qsort(sorted, (size_t)count, sizeof(camel_receiver_packet_t), camel_receiver_packet_seq_comp);

	camel_group_feedback_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.group_id = r->cur_group_id;
	msg.packet_count = count;
	msg.first_recv_ts_us = sorted[0].recv_ts_us;
	msg.last_recv_ts_us = sorted[count - 1].recv_ts_us;

	uint64_t offset = 0;
	msg.first_packet_size = sorted[0].payload_size;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t size = sorted[i].payload_size;
		while (size > 0) {
			uint32_t interval = (uint32_t)(offset / CAMEL_BURST_INTERVAL_BYTES);
			uint64_t interval_end = ((uint64_t)interval + 1) * (uint64_t)CAMEL_BURST_INTERVAL_BYTES;
			uint32_t bytes = (uint32_t)((uint64_t)size < interval_end - offset ? (uint64_t)size : interval_end - offset);
			if (interval >= CAMEL_FEEDBACK_MAX_INTERVALS) {
				size = 0;
				break;
			}
			msg.interval_received_bytes[interval] += bytes;
			uint32_t next_count = interval + 1;
			if (next_count > msg.interval_count)
				msg.interval_count = next_count;
			offset += bytes;
			size -= bytes;
		}
	}
	msg.group_size_bytes = (uint32_t)offset;

	camel_group_feedback_msg_encode(&r->strm, &msg);
	r->group_feedback_cb(r->handler, r->strm.data, (int)r->strm.used);
}

camel_receiver_t* camel_receiver_create(void* handler,
	camel_send_feedback_func group_feedback_cb,
	camel_send_feedback_func packet_ack_cb)
{
	camel_receiver_t* r = (camel_receiver_t*)calloc(1, sizeof(camel_receiver_t));
	if (r == NULL)
		return NULL;

	r->handler = handler;
	r->group_feedback_cb = group_feedback_cb;
	r->packet_ack_cb = packet_ack_cb;
	(void)camel_bin_stream_init(&r->strm);
	camel_receiver_reset_group(r, 0);
	return r;
}

void camel_receiver_on_packet_received(camel_receiver_t* r,
	uint32_t group_id,
	uint16_t transport_seq,
	size_t payload_size,
	int is_group_end)
{
	uint64_t cur_ts = camel_get_sys_us();

	if (r == NULL)
		return;

	if (r->cur_group_id == 0) {
		camel_receiver_reset_group(r, group_id);
	} else if (r->cur_group_id != group_id) {
		camel_receiver_emit_group_feedback(r);
		camel_receiver_reset_group(r, group_id);
	}

	camel_receiver_send_packet_ack(r, transport_seq, cur_ts);

	if (r->packet_count < CAMEL_RECEIVER_MAX_PACKETS) {
		r->packets[r->packet_count].transport_seq = transport_seq;
		r->packets[r->packet_count].payload_size = (uint32_t)payload_size;
		r->packets[r->packet_count].recv_ts_us = cur_ts;
		r->packet_count++;
	}

	if (r->first_recv_ts_us == 0)
		r->first_recv_ts_us = cur_ts;
	else
		r->first_recv_ts_us = camel_min_u64(r->first_recv_ts_us, cur_ts);
	r->last_recv_ts_us = camel_max_u64(r->last_recv_ts_us, cur_ts);

	if (is_group_end) {
		camel_receiver_emit_group_feedback(r);
		camel_receiver_reset_group(r, 0);
	}
}

void camel_receiver_destroy(camel_receiver_t* r)
{
	if (r == NULL)
		return;
	camel_bin_stream_destroy(&r->strm);
	free(r);
}
