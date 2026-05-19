/*-
* Copyright (c) 2026 burst-camel contributors.
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

typedef struct camel_receiver_t camel_receiver_t;

/*
 * Create a receiver instance.
 * - group_feedback_cb: emits one aggregate feedback message when a packet group ends.
 * - packet_ack_cb: emits packet-level ACK samples (transport feedback).
 */
camel_receiver_t* camel_receiver_create(void* handler,
	camel_send_feedback_func group_feedback_cb,
	camel_send_feedback_func packet_ack_cb);
void camel_receiver_destroy(camel_receiver_t* r);

/*
 * Feed one received packet to the receiver.
 * Set is_group_end=1 for the last packet of the group.
 */
void camel_receiver_on_packet_received(camel_receiver_t* r,
	uint32_t group_id,
	uint16_t transport_seq,
	size_t payload_size,
	int is_group_end);

/*
 * Feed one received packet with its original byte offset in the packet group.
 * Use this form when loss-aware interval feedback is required.
 */
void camel_receiver_on_packet_received_with_offset(camel_receiver_t* r,
	uint32_t group_id,
	uint16_t transport_seq,
	size_t payload_size,
	uint32_t frame_offset_bytes,
	uint32_t frame_size_bytes,
	int is_group_end);

#ifdef __cplusplus
}
#endif

#endif
