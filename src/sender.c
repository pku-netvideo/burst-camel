/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "sender.h"

#include "pacer.h"
#include "bandwidth_estimator.h"
#include "congestion_detector.h"
#include "burst_controller.h"
#include "packet_history.h"
#include "gcc_fallback.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define CAMEL_MIN_PACE_BITRATE (500 * 1000)
#define CAMEL_DECISION_INTERVAL_MS 200
#define CAMEL_BITRATE_CHANGE_FRACTION 0.05
#define CAMEL_PACER_TICK_MS 5
#define CAMEL_MAX_GROUP_NUM 1024

static uint64_t camel_get_sys_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

typedef struct
{
	uint32_t group_id;
	uint32_t sent_size_bytes;
	uint32_t sent_packet_count;
	uint32_t first_packet_size;
	uint16_t first_transport_seq;
	uint8_t ended;
	uint64_t ended_ts_us;
	uint64_t first_send_ts_us;
	uint64_t last_send_ts_us;
	uint32_t acked_bytes;
	uint32_t acked_packets;
	uint64_t first_acked_ts_us;
	uint64_t last_acked_ts_us;
	uint32_t acked_interval_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
	int valid;
} camel_group_send_info_t;

typedef struct
{
	uint32_t group_id;
	uint32_t recv_size_bytes;
	uint32_t recv_packet_count;
	uint64_t first_recv_ts_us;
	uint64_t last_recv_ts_us;
	uint32_t interval_count;
	uint32_t interval_received_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
	int valid;
} camel_group_feedback_info_t;

struct camel_sender_t
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
	camel_packet_history_t packet_history;

	camel_group_send_info_t groups[CAMEL_MAX_GROUP_NUM];
	camel_group_feedback_info_t group_feedback[CAMEL_MAX_GROUP_NUM];
	uint64_t group_first_rtt_us[CAMEL_MAX_GROUP_NUM];
	uint8_t group_first_rtt_valid[CAMEL_MAX_GROUP_NUM];
	uint16_t last_cumack_seq;
	uint8_t last_cumack_valid;

	camel_pacer_t* pacer;

	camel_estimator_t estimator;
	camel_congestion_detector_t detector;
	camel_burst_controller_t burst_ctrl;

	size_t congestion_window_bytes;
	uint32_t camel_target_bitrate_bps;
	double camel_gamma;
	double camel_last_slope_us_per_byte;

	uint64_t inflight_bytes;

	camel_gcc_fallback_t gcc_fallback;
	uint32_t gcc_target_bitrate_bps;
	int fallback_enabled;
	int in_fallback;

	int fatal_error;

	pthread_t thread;
	int thread_running;
	int stop_thread;

	camel_sender_config_t cfg;
	camel_sender_warning_cb warning_cb;
	void* warning_ctx;
};

static uint32_t camel_sender_clamp_bitrate(camel_sender_t* s, uint32_t bitrate_bps)
{
	if (s == NULL)
		return bitrate_bps;
	if (bitrate_bps < s->min_bitrate)
		bitrate_bps = s->min_bitrate;
	if (s->max_bitrate > 0 && bitrate_bps > s->max_bitrate)
		bitrate_bps = s->max_bitrate;
	return bitrate_bps;
}

static int camel_sender_bitrate_materially_changed(uint32_t previous_bps, uint32_t current_bps)
{
	uint32_t delta;
	if (previous_bps == 0)
		return 1;
	delta = previous_bps > current_bps ? previous_bps - current_bps : current_bps - previous_bps;
	return (double)delta >= (double)previous_bps * CAMEL_BITRATE_CHANGE_FRACTION;
}

static int camel_sender_should_notify_camel(camel_sender_t* s, int64_t now_ts_ms, uint32_t target_bps)
{
	if (s->trigger_cb == NULL)
		return 0;
	if (s->notify_ts_ms < 0)
		return 1;
	if (now_ts_ms - s->notify_ts_ms < CAMEL_DECISION_INTERVAL_MS)
		return 0;
	return camel_sender_bitrate_materially_changed(s->last_bitrate_bps, target_bps);
}

static uint32_t camel_sender_compute_camel_target(camel_sender_t* s)
{
	uint32_t target_bps = s->camel_target_bitrate_bps;
	if (target_bps == 0 && s->estimator.valid_samples > 0)
		target_bps = (uint32_t)((double)camel_estimator_get_bandwidth(&s->estimator) * s->camel_gamma);
	return camel_sender_clamp_bitrate(s, target_bps);
}

static uint32_t camel_sender_interval_sent_bytes(uint32_t total_size, uint32_t interval)
{
	uint64_t offset = (uint64_t)interval * (uint64_t)CAMEL_BURST_INTERVAL_BYTES;
	if (offset >= (uint64_t)total_size)
		return 0;
	uint64_t remain = (uint64_t)total_size - offset;
	return (uint32_t)(remain < (uint64_t)CAMEL_BURST_INTERVAL_BYTES ? remain : (uint64_t)CAMEL_BURST_INTERVAL_BYTES);
}

static void camel_sender_warn(camel_sender_t* s, const char* code, const char* msg)
{
	if (s == NULL)
		return;
	if (!s->cfg.enable_warnings)
		return;
	if (s->warning_cb == NULL)
		return;
	s->warning_cb(s->warning_ctx, code, msg);
}

static void camel_sender_add_interval_bytes(uint32_t* intervals, uint32_t offset_bytes, uint32_t bytes)
{
	while (bytes > 0) {
		uint32_t interval = offset_bytes / CAMEL_BURST_INTERVAL_BYTES;
		if (interval >= CAMEL_FEEDBACK_MAX_INTERVALS)
			return;
		uint32_t interval_end = (interval + 1U) * CAMEL_BURST_INTERVAL_BYTES;
		uint32_t add = bytes;
		if (offset_bytes + add > interval_end)
			add = interval_end - offset_bytes;
		intervals[interval] += add;
		offset_bytes += add;
		bytes -= add;
	}
}

static void camel_sender_try_finalize_group(camel_sender_t* s, uint32_t idx);

static void camel_sender_process_acked_seq(camel_sender_t* s, uint16_t transport_seq, uint64_t recv_ts_us, int has_recv_ts)
{
	camel_packet_history_entry_t e;
	if (camel_packet_history_get(&s->packet_history, transport_seq, &e) != 0)
		return;
	int ack_mark = camel_packet_history_mark_acked(&s->packet_history, transport_seq);
	if (ack_mark != 0)
		return;

	if (s->inflight_bytes >= e.payload_size)
		s->inflight_bytes -= e.payload_size;
	else
		s->inflight_bytes = 0;
	if (s->pacer != NULL)
		camel_pacer_set_outstanding(s->pacer, (size_t)s->inflight_bytes);

	uint64_t now_us = camel_get_sys_us();
	uint64_t ack_ts_us = has_recv_ts && recv_ts_us > 0 ? recv_ts_us : now_us;
	uint64_t rtt_us = now_us > e.send_ts_us ? now_us - e.send_ts_us : 1;
	if (rtt_us == 0)
		rtt_us = 1;

	uint32_t group_id = e.frame_id;
	uint32_t idx = group_id % CAMEL_MAX_GROUP_NUM;
	if (!s->groups[idx].valid || s->groups[idx].group_id != group_id)
		return;

	s->groups[idx].acked_bytes += e.payload_size;
	s->groups[idx].acked_packets++;
	if (s->groups[idx].first_acked_ts_us == 0)
		s->groups[idx].first_acked_ts_us = ack_ts_us;
	if (ack_ts_us <= s->groups[idx].last_acked_ts_us)
		ack_ts_us = s->groups[idx].last_acked_ts_us + 1U;
	s->groups[idx].last_acked_ts_us = ack_ts_us;
	camel_sender_add_interval_bytes(s->groups[idx].acked_interval_bytes, e.frame_offset_bytes, e.payload_size);

	if (!has_recv_ts)
		recv_ts_us = ack_ts_us;

	if (s->in_fallback) {
		uint32_t target = camel_gcc_fallback_on_packet(&s->gcc_fallback, e.send_ts_us, recv_ts_us, e.payload_size);
		s->gcc_target_bitrate_bps = camel_sender_clamp_bitrate(s, target);
		if (s->pacer != NULL)
			camel_pacer_set_estimate_bitrate(s->pacer, s->gcc_target_bitrate_bps);
	}

	if (!s->group_first_rtt_valid[idx] &&
		s->groups[idx].first_transport_seq == e.transport_seq) {
		s->group_first_rtt_us[idx] = rtt_us;
		s->group_first_rtt_valid[idx] = 1;
		camel_sender_try_finalize_group(s, idx);
	}
	else if (s->groups[idx].ended) {
		camel_sender_try_finalize_group(s, idx);
	}
}

static void camel_sender_try_process_group_sample(camel_sender_t* s, uint32_t idx)
{
	if (s == NULL)
		return;
	if (!s->groups[idx].valid)
		return;
	if (!s->group_feedback[idx].valid)
		return;
	if (!s->group_first_rtt_valid[idx])
		return;
	if (s->groups[idx].group_id != s->group_feedback[idx].group_id)
		return;

	uint64_t delay_us = s->group_first_rtt_us[idx];
	if (delay_us == 0)
		delay_us = 1;

	uint32_t sent_packet_count = s->groups[idx].sent_packet_count;
	uint32_t sent_size = s->groups[idx].sent_size_bytes;
	uint32_t first_packet_size = s->groups[idx].first_packet_size;

	if (sent_packet_count >= 2 && sent_size > first_packet_size) {
		camel_frame_sample_t sample;
		memset(&sample, 0, sizeof(sample));
		sample.group_id = s->groups[idx].group_id;
		sample.packet_count = sent_packet_count;
		sample.bytes_excluding_first = (uint64_t)(sent_size - first_packet_size);
		sample.first_recv_ts_us = s->group_feedback[idx].first_recv_ts_us;
		sample.last_recv_ts_us = s->group_feedback[idx].last_recv_ts_us;
		sample.delay_us = delay_us;

		if (camel_estimator_add_sample(&s->estimator, &sample) == 0) {
			camel_congestion_result_t result =
				camel_congestion_detector_add_sample(&s->detector, s->inflight_bytes, sample.delay_us);
			s->camel_gamma = result.gamma;
			s->camel_last_slope_us_per_byte = result.slope_us_per_byte;
			s->congestion_window_bytes = (size_t)((double)camel_estimator_get_bdp_bytes(&s->estimator) * result.gamma);
			if (!s->in_fallback) {
				s->camel_target_bitrate_bps = camel_sender_clamp_bitrate(s,
					(uint32_t)((double)camel_estimator_get_bandwidth(&s->estimator) * result.gamma));
				if (s->pacer != NULL)
					camel_pacer_set_estimate_bitrate(s->pacer, s->camel_target_bitrate_bps);
			}
			if (s->pacer != NULL)
				camel_pacer_set_congestion_window(s->pacer, s->congestion_window_bytes);
		}
	}

	s->group_feedback[idx].valid = 0;
	s->group_first_rtt_valid[idx] = 0;
}

static void camel_sender_try_finalize_group(camel_sender_t* s, uint32_t idx)
{
	if (s == NULL)
		return;
	if (!s->groups[idx].valid)
		return;

	if (s->group_feedback[idx].valid && s->group_first_rtt_valid[idx]) {
		camel_sender_try_process_group_sample(s, idx);
		memset(&s->groups[idx], 0, sizeof(s->groups[idx]));
		s->groups[idx].valid = 0;
		return;
	}

	if (!s->cfg.enable_synthetic_group_feedback)
		return;
	if (!s->groups[idx].ended)
		return;
	if (s->groups[idx].ended_ts_us == 0)
		return;
	uint64_t now_us = camel_get_sys_us();
	if (s->cfg.group_idle_timeout_ms > 0) {
		uint64_t grace_us = (uint64_t)s->cfg.group_idle_timeout_ms * 1000ULL;
		if (now_us - s->groups[idx].ended_ts_us < grace_us)
			return;
	}
	if (s->groups[idx].acked_packets < 2)
		return;
	if (s->groups[idx].acked_bytes <= s->groups[idx].first_packet_size)
		return;

	uint64_t delay_us = 0;
	if (s->group_first_rtt_valid[idx])
		delay_us = s->group_first_rtt_us[idx];
	else {
		if (s->groups[idx].first_acked_ts_us > 0 && s->groups[idx].first_send_ts_us > 0)
			delay_us = s->groups[idx].first_acked_ts_us > s->groups[idx].first_send_ts_us
				? s->groups[idx].first_acked_ts_us - s->groups[idx].first_send_ts_us
				: 1;
		if (delay_us == 0)
			delay_us = 1;
		camel_sender_warn(s, "MISSING_FIRST_PACKET_ACK", "first packet not acked; using degraded delay");
	}

	camel_sender_warn(s, "SYNTHETIC_GROUP_FEEDBACK", "using synthetic group feedback");

	camel_frame_sample_t sample;
	memset(&sample, 0, sizeof(sample));
	sample.group_id = s->groups[idx].group_id;
	sample.packet_count = s->groups[idx].acked_packets;
	sample.bytes_excluding_first = (uint64_t)(s->groups[idx].acked_bytes - s->groups[idx].first_packet_size);
	sample.first_recv_ts_us = s->groups[idx].first_acked_ts_us;
	sample.last_recv_ts_us = s->groups[idx].last_acked_ts_us;
	sample.delay_us = delay_us;

	if (camel_estimator_add_sample(&s->estimator, &sample) == 0) {
		camel_congestion_result_t result =
			camel_congestion_detector_add_sample(&s->detector, s->inflight_bytes, sample.delay_us);
		s->camel_gamma = result.gamma;
		s->camel_last_slope_us_per_byte = result.slope_us_per_byte;
		s->congestion_window_bytes = (size_t)((double)camel_estimator_get_bdp_bytes(&s->estimator) * result.gamma);
		if (!s->in_fallback) {
			s->camel_target_bitrate_bps = camel_sender_clamp_bitrate(s,
				(uint32_t)((double)camel_estimator_get_bandwidth(&s->estimator) * result.gamma));
			if (s->pacer != NULL)
				camel_pacer_set_estimate_bitrate(s->pacer, s->camel_target_bitrate_bps);
		}
		if (s->pacer != NULL)
			camel_pacer_set_congestion_window(s->pacer, s->congestion_window_bytes);
	}

	if (s->cfg.enable_synthetic_interval_shape) {
		camel_sender_warn(s, "SYNTHETIC_INTERVAL_SHAPE", "using synthetic interval shape");
		uint32_t interval_count = (uint32_t)CAMEL_FEEDBACK_MAX_INTERVALS;
		for (uint32_t i = 0; i < interval_count; i++) {
			uint32_t sent = camel_sender_interval_sent_bytes(s->groups[idx].sent_size_bytes, i);
			uint32_t received = s->groups[idx].acked_interval_bytes[i] < sent ? s->groups[idx].acked_interval_bytes[i] : sent;
			uint32_t lost = sent - received;
			if (sent > 0)
				camel_burst_controller_record_interval_counts(&s->burst_ctrl, i, sent, lost);
		}

		(void)camel_burst_controller_maybe_update(&s->burst_ctrl, (uint64_t)(camel_get_sys_us() / 1000ULL));
		if (s->pacer != NULL)
			camel_pacer_set_max_burst_bytes(s->pacer, s->burst_ctrl.current_burst_bytes);
	}

	memset(&s->groups[idx], 0, sizeof(s->groups[idx]));
	s->groups[idx].valid = 0;
	s->group_first_rtt_valid[idx] = 0;
	s->group_feedback[idx].valid = 0;
}

camel_sender_t* camel_sender_create(void* trigger,
	camel_bitrate_changed_func bitrate_cb,
	void* handler,
	camel_pace_send_func send_cb,
	int queue_ms,
	int padding,
	camel_app_layer_predict_func app_func)
{
	(void)queue_ms;
	(void)padding;

	camel_sender_t* s = (camel_sender_t*)calloc(1, sizeof(camel_sender_t));
	if (s == NULL)
		return NULL;

	s->trigger = trigger;
	s->trigger_cb = bitrate_cb;
	s->handler = handler;
	s->send_cb = send_cb;
	s->app_func = app_func;

	s->notify_ts_ms = -1;
	s->min_bitrate = CAMEL_MIN_PACE_BITRATE;
	s->max_bitrate = 0;
	s->last_bitrate_bps = 0;

	s->camel_target_bitrate_bps = CAMEL_MIN_PACE_BITRATE;
	s->camel_gamma = 1.0;
	s->camel_last_slope_us_per_byte = 0.0;
	s->fatal_error = 0;
	s->fallback_enabled = 1;
	s->in_fallback = 0;
	s->gcc_target_bitrate_bps = 0;
	memset(&s->gcc_fallback, 0, sizeof(s->gcc_fallback));
	s->thread_running = 0;
	s->stop_thread = 0;
	s->last_cumack_seq = 0;
	s->last_cumack_valid = 0;
	memset(&s->cfg, 0, sizeof(s->cfg));
	s->cfg.enable_warnings = 1;
	s->cfg.enable_synthetic_group_feedback = 1;
	s->cfg.enable_synthetic_interval_shape = 1;
	s->cfg.group_idle_timeout_ms = 5;
	s->warning_cb = NULL;
	s->warning_ctx = NULL;

	(void)camel_bin_stream_init(&s->strm);

	memset(s->groups, 0, sizeof(s->groups));
	memset(s->group_feedback, 0, sizeof(s->group_feedback));
	memset(s->group_first_rtt_us, 0, sizeof(s->group_first_rtt_us));
	memset(s->group_first_rtt_valid, 0, sizeof(s->group_first_rtt_valid));
	(void)camel_packet_history_init(&s->packet_history, 4096);

	camel_estimator_init(&s->estimator, 0.1);
	camel_congestion_detector_init(&s->detector, 8, 0.5, 0.2);
	camel_burst_controller_init(&s->burst_ctrl, 2048, 12 * 1024, 64 * 1024);

	s->pacer = (camel_pacer_t*)calloc(1, sizeof(camel_pacer_t));
	if (s->pacer != NULL) {
		if (camel_pacer_init(s->pacer, s->handler, s->send_cb, 4096) != 0) {
			free(s->pacer);
			s->pacer = NULL;
		}
	}
	if (s->pacer != NULL) {
		camel_pacer_set_bitrate_limits(s->pacer, s->min_bitrate);
		camel_pacer_set_max_burst_bytes(s->pacer, s->burst_ctrl.current_burst_bytes);
	}

	return s;
}

void camel_sender_set_config(camel_sender_t* s, const camel_sender_config_t* cfg)
{
	if (s == NULL || cfg == NULL)
		return;
	s->cfg = *cfg;
}

void camel_sender_set_warning_cb(camel_sender_t* s, camel_sender_warning_cb cb, void* cb_ctx)
{
	if (s == NULL)
		return;
	s->warning_cb = cb;
	s->warning_ctx = cb_ctx;
}

static void camel_sender_stop_thread(camel_sender_t* s)
{
	if (s == NULL)
		return;
	if (!s->thread_running)
		return;
	s->stop_thread = 1;
	(void)pthread_join(s->thread, NULL);
	s->thread_running = 0;
	s->stop_thread = 0;
}

void camel_sender_destroy(camel_sender_t* s)
{
	if (s == NULL)
		return;

	camel_sender_stop_thread(s);

	camel_packet_history_destroy(&s->packet_history);
	if (s->pacer != NULL) {
		camel_pacer_destroy(s->pacer);
		free(s->pacer);
		s->pacer = NULL;
	}
	camel_bin_stream_destroy(&s->strm);
	free(s);
}

void camel_sender_on_packet_sent(camel_sender_t* s,
	uint32_t group_id,
	uint16_t transport_seq,
	size_t payload_size,
	int is_group_end)
{
	if (s == NULL || s->fatal_error)
		return;

	uint64_t now_us = camel_get_sys_us();
	uint32_t idx = group_id % CAMEL_MAX_GROUP_NUM;

	if (!s->groups[idx].valid || s->groups[idx].group_id != group_id) {
		memset(&s->groups[idx], 0, sizeof(s->groups[idx]));
		s->groups[idx].group_id = group_id;
		s->groups[idx].valid = 1;
		s->group_first_rtt_valid[idx] = 0;
		s->group_feedback[idx].valid = 0;
		s->groups[idx].first_send_ts_us = now_us;
	}
	s->groups[idx].last_send_ts_us = now_us;

	uint32_t offset = s->groups[idx].sent_size_bytes;
	camel_packet_history_add(&s->packet_history, transport_seq, group_id, offset, (uint32_t)payload_size, now_us);

	if (s->groups[idx].sent_packet_count == 0) {
		s->groups[idx].first_packet_size = (uint32_t)payload_size;
		s->groups[idx].first_transport_seq = transport_seq;
	}
	s->groups[idx].sent_packet_count++;
	s->groups[idx].sent_size_bytes += (uint32_t)payload_size;

	s->inflight_bytes += (uint64_t)payload_size;
	if (s->pacer != NULL)
		camel_pacer_set_outstanding(s->pacer, (size_t)s->inflight_bytes);

	if (is_group_end) {
		s->groups[idx].ended = 1;
		if (s->groups[idx].ended_ts_us == 0)
			s->groups[idx].ended_ts_us = now_us;
		camel_sender_try_finalize_group(s, idx);
	}
}

void camel_sender_end_group(camel_sender_t* s, uint32_t group_id)
{
	if (s == NULL || s->fatal_error)
		return;
	uint32_t idx = group_id % CAMEL_MAX_GROUP_NUM;
	if (!s->groups[idx].valid || s->groups[idx].group_id != group_id)
		return;
	s->groups[idx].ended = 1;
	if (s->groups[idx].ended_ts_us == 0)
		s->groups[idx].ended_ts_us = camel_get_sys_us();
	camel_sender_try_finalize_group(s, idx);
}

void camel_sender_on_packet_ack(camel_sender_t* s, const uint8_t* payload, int payload_size)
{
	if (s == NULL || s->fatal_error)
		return;
	if (payload == NULL || payload_size <= 0)
		return;

	if ((size_t)payload_size > s->strm.size) {
		free(s->strm.data);
		s->strm.data = (uint8_t*)malloc((size_t)payload_size);
		s->strm.size = (size_t)payload_size;
	}

	memcpy(s->strm.data, payload, (size_t)payload_size);
	s->strm.used = (size_t)payload_size;
	camel_bin_stream_rewind(&s->strm, 0);

	camel_transport_feedback_msg_t msg;
	camel_transport_feedback_msg_decode(&s->strm, &msg);

	for (uint32_t i = 0; i < msg.sample_count; i++) {
		camel_sender_process_acked_seq(s, msg.samples[i].transport_seq, msg.samples[i].recv_ts_us, 1);
	}
}

void camel_sender_on_cumulative_ack(camel_sender_t* s, const uint8_t* payload, int payload_size)
{
	if (s == NULL || s->fatal_error)
		return;
	if (payload == NULL || payload_size <= 0)
		return;

	if ((size_t)payload_size > s->strm.size) {
		free(s->strm.data);
		s->strm.data = (uint8_t*)malloc((size_t)payload_size);
		s->strm.size = (size_t)payload_size;
	}

	memcpy(s->strm.data, payload, (size_t)payload_size);
	s->strm.used = (size_t)payload_size;
	camel_bin_stream_rewind(&s->strm, 0);

	camel_cumack_msg_t msg;
	camel_cumack_msg_decode(&s->strm, &msg);

	uint16_t start = s->last_cumack_valid ? (uint16_t)(s->last_cumack_seq + 1U) : msg.largest_acked_seq;
	if (!s->last_cumack_valid)
		camel_sender_warn(s, "ACK_FORMAT_DEGRADED", "cumulative ACK without baseline; only acking largest_acked_seq");

	uint32_t processed = 0;
	for (uint32_t seq = start; seq <= msg.largest_acked_seq && processed < 4096; seq++, processed++)
		camel_sender_process_acked_seq(s, (uint16_t)seq, 0, 0);

	s->last_cumack_seq = msg.largest_acked_seq;
	s->last_cumack_valid = 1;
}

void camel_sender_on_ack_ranges(camel_sender_t* s, const uint8_t* payload, int payload_size)
{
	if (s == NULL || s->fatal_error)
		return;
	if (payload == NULL || payload_size <= 0)
		return;

	if ((size_t)payload_size > s->strm.size) {
		free(s->strm.data);
		s->strm.data = (uint8_t*)malloc((size_t)payload_size);
		s->strm.size = (size_t)payload_size;
	}

	memcpy(s->strm.data, payload, (size_t)payload_size);
	s->strm.used = (size_t)payload_size;
	camel_bin_stream_rewind(&s->strm, 0);

	camel_ack_ranges_msg_t msg;
	camel_ack_ranges_msg_decode(&s->strm, &msg);

	uint32_t processed = 0;
	for (uint32_t r = 0; r < msg.range_count && processed < 4096; r++) {
		uint16_t start = msg.ranges[r].start_seq;
		uint16_t end = msg.ranges[r].end_seq;
		if (end < start)
			continue;
		for (uint32_t seq = start; seq <= end && processed < 4096; seq++, processed++)
			camel_sender_process_acked_seq(s, (uint16_t)seq, 0, 0);
	}
	if (processed >= 4096)
		camel_sender_warn(s, "ACK_FORMAT_DEGRADED", "ack ranges truncated");
}

void camel_sender_on_group_feedback(camel_sender_t* s, const uint8_t* payload, int payload_size)
{
	if (s == NULL || s->fatal_error)
		return;
	if (payload == NULL || payload_size <= 0)
		return;

	if ((size_t)payload_size > s->strm.size) {
		free(s->strm.data);
		s->strm.data = (uint8_t*)malloc((size_t)payload_size);
		s->strm.size = (size_t)payload_size;
	}

	memcpy(s->strm.data, payload, (size_t)payload_size);
	s->strm.used = (size_t)payload_size;
	camel_bin_stream_rewind(&s->strm, 0);

	camel_group_feedback_msg_t msg;
	camel_group_feedback_msg_decode(&s->strm, &msg);

	uint32_t idx = msg.group_id % CAMEL_MAX_GROUP_NUM;
	uint32_t sent_size = (s->groups[idx].valid && s->groups[idx].group_id == msg.group_id)
		? s->groups[idx].sent_size_bytes
		: msg.group_size_bytes;

	uint32_t interval_count = msg.interval_count;
	if (interval_count > CAMEL_FEEDBACK_MAX_INTERVALS)
		interval_count = CAMEL_FEEDBACK_MAX_INTERVALS;
	for (uint32_t i = 0; i < interval_count; i++) {
		uint32_t sent = camel_sender_interval_sent_bytes(sent_size, i);
		uint32_t received = msg.interval_received_bytes[i] < sent ? msg.interval_received_bytes[i] : sent;
		uint32_t lost = sent - received;
		if (sent > 0)
			camel_burst_controller_record_interval_counts(&s->burst_ctrl, i, sent, lost);
	}

	(void)camel_burst_controller_maybe_update(&s->burst_ctrl, (uint64_t)(camel_get_sys_us() / 1000ULL));
	if (s->pacer != NULL)
		camel_pacer_set_max_burst_bytes(s->pacer, s->burst_ctrl.current_burst_bytes);

	if (s->burst_ctrl.fallback_mode && !s->in_fallback) {
		if (!s->fallback_enabled) {
			s->fatal_error = 1;
			return;
		}
		camel_gcc_fallback_init(&s->gcc_fallback, s->min_bitrate, s->max_bitrate,
			s->camel_target_bitrate_bps > 0 ? s->camel_target_bitrate_bps : s->min_bitrate);
		s->gcc_target_bitrate_bps = s->gcc_fallback.target_bitrate_bps;
		s->in_fallback = 1;
		if (s->pacer != NULL)
			camel_pacer_set_estimate_bitrate(s->pacer, s->gcc_target_bitrate_bps);
	}
	else if (!s->burst_ctrl.fallback_mode && s->in_fallback) {
		s->in_fallback = 0;
		s->gcc_target_bitrate_bps = 0;
	}

	memset(&s->group_feedback[idx], 0, sizeof(s->group_feedback[idx]));
	s->group_feedback[idx].group_id = msg.group_id;
	s->group_feedback[idx].recv_size_bytes = msg.group_size_bytes;
	s->group_feedback[idx].recv_packet_count = msg.packet_count;
	s->group_feedback[idx].first_recv_ts_us = msg.first_recv_ts_us;
	s->group_feedback[idx].last_recv_ts_us = msg.last_recv_ts_us;
	s->group_feedback[idx].interval_count = interval_count;
	memcpy(s->group_feedback[idx].interval_received_bytes, msg.interval_received_bytes,
		sizeof(uint32_t) * interval_count);
	s->group_feedback[idx].valid = 1;

	camel_sender_try_finalize_group(s, idx);
}

void camel_sender_heartbeat(camel_sender_t* s, int64_t now_ts_ms)
{
	uint32_t camel_target_bps = 0;
	int notify_camel = 0;

	if (s == NULL)
		return;
	if (s->fatal_error)
		return;

	(void)camel_burst_controller_maybe_update(&s->burst_ctrl, (uint64_t)now_ts_ms);

	uint64_t sys_now_us = camel_get_sys_us();
	for (uint32_t i = 0; i < CAMEL_MAX_GROUP_NUM; i++) {
		if (!s->groups[i].valid)
			continue;
		if (!s->groups[i].ended &&
			s->cfg.group_idle_timeout_ms > 0 &&
			s->groups[i].last_send_ts_us > 0) {
			uint64_t timeout_us = (uint64_t)s->cfg.group_idle_timeout_ms * 1000ULL;
			if (sys_now_us - s->groups[i].last_send_ts_us >= timeout_us) {
				s->groups[i].ended = 1;
				if (s->groups[i].ended_ts_us == 0)
					s->groups[i].ended_ts_us = sys_now_us;
				camel_sender_warn(s, "GROUP_IDLE_TIMEOUT", "group ended by idle timeout");
			}
		}
		if (s->groups[i].ended)
			camel_sender_try_finalize_group(s, i);
	}

	if (s->estimator.valid_samples > 0) {
		if (s->in_fallback) {
			camel_target_bps = s->gcc_target_bitrate_bps > 0 ? s->gcc_target_bitrate_bps : s->min_bitrate;
		} else {
			camel_target_bps = camel_sender_compute_camel_target(s);
		}
		s->camel_target_bitrate_bps = camel_target_bps;
		notify_camel = camel_sender_should_notify_camel(s, now_ts_ms, camel_target_bps);
		if (notify_camel) {
			s->last_bitrate_bps = camel_target_bps;
			s->notify_ts_ms = now_ts_ms;
		}
		if (notify_camel)
			s->trigger_cb(s->trigger, camel_target_bps, 0, 0);
		if (s->pacer != NULL)
			camel_pacer_set_estimate_bitrate(s->pacer, camel_target_bps);
		return;
	}
}

size_t camel_sender_get_burst_bytes(const camel_sender_t* s)
{
	if (s == NULL)
		return 0;
	return s->burst_ctrl.current_burst_bytes;
}

int camel_sender_in_fallback(const camel_sender_t* s)
{
	if (s == NULL)
		return 0;
	return s->in_fallback != 0;
}

void camel_sender_set_fallback_enabled(camel_sender_t* s, int enabled)
{
	if (s == NULL)
		return;
	s->fallback_enabled = enabled != 0;
}

static void* camel_sender_thread_main(void* arg)
{
	camel_sender_t* s = (camel_sender_t*)arg;
	int64_t last_hb_ms = -1;
	while (s != NULL && !s->stop_thread) {
		int64_t now_ms = (int64_t)(camel_get_sys_us() / 1000ULL);
		camel_sender_pacer_try_transmit(s, now_ms);
		if (last_hb_ms < 0 || now_ms - last_hb_ms >= CAMEL_DECISION_INTERVAL_MS) {
			camel_sender_heartbeat(s, now_ms);
			last_hb_ms = now_ms;
		}
		usleep((useconds_t)CAMEL_PACER_TICK_MS * 1000U);
	}
	return NULL;
}

int camel_sender_start(camel_sender_t* s)
{
	if (s == NULL)
		return -1;
	if (s->thread_running)
		return 0;
	s->stop_thread = 0;
	if (pthread_create(&s->thread, NULL, camel_sender_thread_main, s) != 0)
		return -1;
	s->thread_running = 1;
	return 0;
}

void camel_sender_stop(camel_sender_t* s)
{
	camel_sender_stop_thread(s);
}

int camel_sender_pacer_insert_packet(camel_sender_t* s, uint32_t packet_id, int retrans, size_t size, int padding, int64_t now_ts_ms)
{
	if (s == NULL || s->pacer == NULL)
		return -1;
	return camel_pacer_insert_packet(s->pacer, packet_id, retrans, size, padding, now_ts_ms);
}

void camel_sender_pacer_try_transmit(camel_sender_t* s, int64_t now_ts_ms)
{
	if (s == NULL || s->pacer == NULL)
		return;
	camel_pacer_try_transmit(s->pacer, now_ts_ms);
}

int camel_sender_get_pacer_stats(const camel_sender_t* s, camel_pacer_stats_t* out)
{
	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	if (s == NULL || s->pacer == NULL)
		return -1;

	out->pacing_bitrate_bps = s->pacer->pacing_bitrate_bps;
	out->min_pacing_bitrate_bps = s->pacer->min_pacing_bitrate_bps;
	out->congestion_window_bytes = s->pacer->congestion_window_bytes;
	out->outstanding_bytes = s->pacer->outstanding_bytes;
	out->max_burst_bytes = s->pacer->max_burst_bytes;
	out->queue_size = s->pacer->size;
	out->budget_bytes = s->pacer->budget_bytes;
	out->last_update_ts_ms = s->pacer->last_update_ts_ms;
	return 0;
}
