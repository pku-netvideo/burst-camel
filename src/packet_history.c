/*-
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "packet_history.h"

#include <stdlib.h>
#include <string.h>

static uint32_t camel_packet_history_clamp_capacity(uint32_t capacity)
{
	if (capacity == 0)
		return 4096;
	if (capacity < 256)
		return 256;
	return capacity;
}

int camel_packet_history_init(camel_packet_history_t* h, uint32_t capacity)
{
	if (h == NULL)
		return -1;

	memset(h, 0, sizeof(*h));
	h->capacity = camel_packet_history_clamp_capacity(capacity);
	h->entries = (camel_packet_history_entry_t*)calloc(h->capacity, sizeof(camel_packet_history_entry_t));
	if (h->entries == NULL) {
		memset(h, 0, sizeof(*h));
		return -1;
	}
	return 0;
}

void camel_packet_history_destroy(camel_packet_history_t* h)
{
	if (h == NULL)
		return;
	free(h->entries);
	memset(h, 0, sizeof(*h));
}

void camel_packet_history_reset(camel_packet_history_t* h)
{
	if (h == NULL || h->entries == NULL)
		return;
	memset(h->entries, 0, sizeof(camel_packet_history_entry_t) * h->capacity);
}

void camel_packet_history_add(camel_packet_history_t* h,
	uint16_t transport_seq,
	uint32_t frame_id,
	uint32_t frame_offset_bytes,
	uint32_t payload_size,
	uint64_t send_ts_us)
{
	if (h == NULL || h->entries == NULL || h->capacity == 0)
		return;

	uint32_t idx = (uint32_t)transport_seq % h->capacity;
	camel_packet_history_entry_t* e = &h->entries[idx];
	e->transport_seq = transport_seq;
	e->frame_id = frame_id;
	e->frame_offset_bytes = frame_offset_bytes;
	e->payload_size = payload_size;
	e->send_ts_us = send_ts_us;
	e->acked = 0;
}

int camel_packet_history_get(const camel_packet_history_t* h,
	uint16_t transport_seq,
	camel_packet_history_entry_t* out)
{
	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));

	if (h == NULL || h->entries == NULL || h->capacity == 0)
		return -1;

	uint32_t idx = (uint32_t)transport_seq % h->capacity;
	const camel_packet_history_entry_t* e = &h->entries[idx];
	if (e->transport_seq != transport_seq)
		return -1;

	*out = *e;
	return 0;
}

int camel_packet_history_mark_acked(camel_packet_history_t* h, uint16_t transport_seq)
{
	if (h == NULL || h->entries == NULL || h->capacity == 0)
		return -1;

	uint32_t idx = (uint32_t)transport_seq % h->capacity;
	camel_packet_history_entry_t* e = &h->entries[idx];
	if (e->transport_seq != transport_seq)
		return -1;
	if (e->acked)
		return 1;
	e->acked = 1;
	return 0;
}
