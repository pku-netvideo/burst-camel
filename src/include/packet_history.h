/*-
* Copyright (c) 2026 burst-camel contributors.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_packet_history_h_
#define __camel_packet_history_h_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint16_t	transport_seq;
	uint32_t	frame_id;
	uint32_t	frame_offset_bytes;
	uint32_t	payload_size;
	uint64_t	send_ts_us;
	uint8_t		acked;
} camel_packet_history_entry_t;

typedef struct {
	camel_packet_history_entry_t* entries;
	uint32_t capacity;
	uint64_t overwrote_unacked;
} camel_packet_history_t;

int camel_packet_history_init(camel_packet_history_t* h, uint32_t capacity);
void camel_packet_history_destroy(camel_packet_history_t* h);
void camel_packet_history_reset(camel_packet_history_t* h);

int camel_packet_history_add(camel_packet_history_t* h,
	uint16_t transport_seq,
	uint32_t frame_id,
	uint32_t frame_offset_bytes,
	uint32_t payload_size,
	uint64_t send_ts_us);

int camel_packet_history_get(const camel_packet_history_t* h,
	uint16_t transport_seq,
	camel_packet_history_entry_t* out);

int camel_packet_history_mark_acked(camel_packet_history_t* h, uint16_t transport_seq);

#ifdef __cplusplus
}
#endif

#endif
