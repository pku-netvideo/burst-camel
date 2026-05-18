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

#include "callbacks.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct camel_sender_t camel_sender_t;

typedef void (*camel_sender_warning_cb)(void* ctx, const char* code, const char* msg);

typedef struct
{
	int enable_warnings;
	int enable_synthetic_group_feedback;
	int enable_synthetic_interval_shape;
	uint32_t group_idle_timeout_ms;
} camel_sender_config_t;

typedef struct
{
	uint32_t pacing_bitrate_bps;
	uint32_t min_pacing_bitrate_bps;
	size_t congestion_window_bytes;
	size_t outstanding_bytes;
	size_t max_burst_bytes;
	uint32_t queue_size;
	int64_t budget_bytes;
	int64_t last_update_ts_ms;
} camel_pacer_stats_t;

/*
 * Create a sender instance.
 * - bitrate_cb is invoked when a new target bitrate should be applied.
 * - send_cb is used by the built-in pacer to emit packets.
 */
camel_sender_t* camel_sender_create(void* trigger,
	camel_bitrate_changed_func bitrate_cb,
	void* handler,
	camel_pace_send_func send_cb,
	int queue_ms,
	int padding,
	camel_app_layer_predict_func app_func);

void camel_sender_destroy(camel_sender_t* s);

void camel_sender_set_config(camel_sender_t* s, const camel_sender_config_t* cfg);
void camel_sender_set_warning_cb(camel_sender_t* s, camel_sender_warning_cb cb, void* cb_ctx);

void camel_sender_end_group(camel_sender_t* s, uint32_t group_id);

void camel_sender_on_cumulative_ack(camel_sender_t* s, const uint8_t* payload, int payload_size);
void camel_sender_on_ack_ranges(camel_sender_t* s, const uint8_t* payload, int payload_size);

/*
 * Record a transmitted packet.
 * Set is_group_end=1 for the last packet of the group.
 */
void camel_sender_on_packet_sent(camel_sender_t* s,
	uint32_t group_id,
	uint16_t transport_seq,
	size_t payload_size,
	int is_group_end);

/*
 * Feed packet-level ACK samples (transport feedback) to the sender.
 */
void camel_sender_on_packet_ack(camel_sender_t* s, const uint8_t* payload, int payload_size);

/*
 * Feed an aggregate group feedback message to the sender.
 */
void camel_sender_on_group_feedback(camel_sender_t* s, const uint8_t* payload, int payload_size);

/*
 * Manual-time mode: drive one heartbeat tick.
 */
void camel_sender_heartbeat(camel_sender_t* s, int64_t now_ts_ms);

size_t camel_sender_get_burst_bytes(const camel_sender_t* s);
int camel_sender_in_fallback(const camel_sender_t* s);
void camel_sender_set_fallback_enabled(camel_sender_t* s, int enabled);

/*
 * Real-time mode: start/stop the internal pacing + heartbeat thread.
 */
int camel_sender_start(camel_sender_t* s);
void camel_sender_stop(camel_sender_t* s);

int camel_sender_pacer_insert_packet(camel_sender_t* s, uint32_t packet_id, int retrans, size_t size, int padding, int64_t now_ts_ms);
void camel_sender_pacer_try_transmit(camel_sender_t* s, int64_t now_ts_ms);
int camel_sender_get_pacer_stats(const camel_sender_t* s, camel_pacer_stats_t* out);

#ifdef __cplusplus
}
#endif

#endif
