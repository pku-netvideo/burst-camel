/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "sender.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAMEL_MIN_PACE_BITRATE (500 * 1000)
#define CAMEL_DECISION_INTERVAL_MS 200
#define CAMEL_BITRATE_CHANGE_FRACTION 0.05

static uint64_t camel_get_sys_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

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

static uint32_t camel_sender_interval_sent_bytes(size_t frame_size, uint32_t interval)
{
	size_t offset = (size_t)interval * CAMEL_BURST_INTERVAL_BYTES;
	size_t remain;

	if (offset >= frame_size)
		return 0;

	remain = frame_size - offset;
	return (uint32_t)(remain < (size_t)CAMEL_BURST_INTERVAL_BYTES ? remain : (size_t)CAMEL_BURST_INTERVAL_BYTES);
}

static void camel_sender_update_burst_controller(camel_sender_t* s, const camel_feedback_msg_t* msg, int64_t now_ts_ms)
{
	uint32_t interval_count = msg->interval_count;
	uint32_t fit_idx = msg->frame_id % CAMEL_MAX_FRAME_NUM;
	size_t sent_frame_size = (s->fit.frame_id[fit_idx] == msg->frame_id)
		? s->fit.frame_size[fit_idx]
		: msg->frame_size;

	if (interval_count == 0)
		return;

	if (interval_count > CAMEL_FEEDBACK_MAX_INTERVALS)
		interval_count = CAMEL_FEEDBACK_MAX_INTERVALS;

	for (uint32_t i = 0; i < interval_count; i++) {
		uint32_t sent = camel_sender_interval_sent_bytes(sent_frame_size, i);
		uint32_t received = msg->interval_received_bytes[i] < sent ? msg->interval_received_bytes[i] : sent;
		uint32_t lost = sent - received;

		if (sent > 0)
			camel_burst_controller_record_interval_counts(&s->burst_ctrl, i, sent, lost);
	}

	(void)camel_burst_controller_maybe_update(&s->burst_ctrl, (uint64_t)now_ts_ms);
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

	(void)camel_bin_stream_init(&s->strm);

	memset(&s->fit, 0, sizeof(s->fit));
	camel_estimator_init(&s->estimator, 0.1);
	camel_congestion_detector_init(&s->detector, 8, 0.5, 0.2);
	camel_burst_controller_init(&s->burst_ctrl, 2048, 12 * 1024, 64 * 1024);

	return s;
}

void camel_sender_destroy(camel_sender_t* s)
{
	if (s == NULL)
		return;

	camel_bin_stream_destroy(&s->strm);
	free(s);
}

void camel_sender_send_frame(camel_sender_t* s, uint32_t frame_id, size_t size)
{
	if (s == NULL)
		return;

	uint64_t now = camel_get_sys_us();
	uint32_t idx = frame_id % CAMEL_MAX_FRAME_NUM;
	s->fit.frame_id[idx] = frame_id;
	s->fit.frame_send_ts[idx] = now;
	s->fit.frame_size[idx] = size;
	s->inflight_bytes += (uint64_t)size;
}

void camel_sender_on_feedback(camel_sender_t* s, const uint8_t* feedback, int feedback_size)
{
	camel_feedback_msg_t msg;

	if (s == NULL)
		return;

	if (feedback_size <= 0 || feedback == NULL)
		return;

	if ((size_t)feedback_size > s->strm.size) {
		free(s->strm.data);
		s->strm.data = (uint8_t*)malloc((size_t)feedback_size);
		s->strm.size = (size_t)feedback_size;
	}

	memcpy(s->strm.data, feedback, (size_t)feedback_size);
	s->strm.used = (size_t)feedback_size;
	camel_bin_stream_rewind(&s->strm, 0);
	memset(&msg, 0, sizeof(msg));
	camel_feedback_msg_decode(&s->strm, &msg);

	camel_sender_update_burst_controller(s, &msg, (int64_t)(camel_get_sys_us() / 1000));

	if (s->inflight_bytes >= msg.frame_size)
		s->inflight_bytes -= (uint64_t)msg.frame_size;
	else
		s->inflight_bytes = 0;

	uint32_t idx = msg.frame_id % CAMEL_MAX_FRAME_NUM;
	uint64_t send_ts = (s->fit.frame_id[idx] == msg.frame_id) ? s->fit.frame_send_ts[idx] : 0;

	if (send_ts > 0 && msg.packet_count >= 2 && msg.frame_size > msg.first_packet_size) {
		uint64_t recv_fb_ts = camel_get_sys_us();
		uint64_t delay_us = recv_fb_ts > send_ts ? recv_fb_ts - send_ts : 1;
		if (delay_us == 0)
			delay_us = 1;

		camel_frame_sample_t sample;
		memset(&sample, 0, sizeof(sample));
		sample.frame_id = msg.frame_id;
		sample.packet_count = msg.packet_count;
		sample.bytes_excluding_first = msg.frame_size - msg.first_packet_size;
		sample.first_recv_ts_us = msg.first_ts;
		sample.last_recv_ts_us = msg.last_ts;
		sample.delay_us = delay_us;

		if (camel_estimator_add_sample(&s->estimator, &sample) == 0) {
			camel_congestion_result_t result =
				camel_congestion_detector_add_sample(&s->detector, s->inflight_bytes, sample.delay_us);
			s->camel_gamma = result.gamma;
			s->camel_last_slope_us_per_byte = result.slope_us_per_byte;
			s->congestion_window_bytes = (size_t)((double)camel_estimator_get_bdp_bytes(&s->estimator) * result.gamma);
			s->camel_target_bitrate_bps = camel_sender_clamp_bitrate(s,
				(uint32_t)((double)camel_estimator_get_bandwidth(&s->estimator) * result.gamma));
		}
	}
}

void camel_sender_heartbeat(camel_sender_t* s, int64_t now_ts_ms)
{
	uint32_t camel_target_bps = 0;
	int notify_camel = 0;

	if (s == NULL)
		return;

	(void)camel_burst_controller_maybe_update(&s->burst_ctrl, (uint64_t)now_ts_ms);

	if (s->estimator.valid_samples > 0) {
		camel_target_bps = camel_sender_compute_camel_target(s);
		s->camel_target_bitrate_bps = camel_target_bps;
		notify_camel = camel_sender_should_notify_camel(s, now_ts_ms, camel_target_bps);
		if (notify_camel) {
			s->last_bitrate_bps = camel_target_bps;
			s->notify_ts_ms = now_ts_ms;
		}
		if (notify_camel)
			s->trigger_cb(s->trigger, camel_target_bps, 0, 0);
		return;
	}
}

