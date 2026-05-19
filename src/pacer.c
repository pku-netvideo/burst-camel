/*-
* Copyright (c) 2026 burst-camel contributors.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "pacer.h"

#include <stdlib.h>
#include <string.h>

static void camel_pacer_lock(camel_pacer_t* p)
{
	if (p == NULL || !p->mu_inited)
		return;
	(void)pthread_mutex_lock(&p->mu);
}

static void camel_pacer_unlock(camel_pacer_t* p)
{
	if (p == NULL || !p->mu_inited)
		return;
	(void)pthread_mutex_unlock(&p->mu);
}

static uint32_t camel_pacer_clamp_capacity(uint32_t capacity)
{
	if (capacity == 0)
		return 4096;
	if (capacity < 64)
		return 64;
	return capacity;
}

static uint32_t camel_pacer_clamp_bitrate(uint32_t bps, uint32_t min_bps)
{
	if (bps < min_bps)
		return min_bps;
	return bps;
}

int camel_pacer_init(camel_pacer_t* p, void* handler, camel_pace_send_func send_cb, uint32_t capacity)
{
	if (p == NULL)
		return -1;
	memset(p, 0, sizeof(*p));

	if (pthread_mutex_init(&p->mu, NULL) == 0)
		p->mu_inited = 1;
	else
		p->mu_inited = 0;

	p->handler = handler;
	p->send_cb = send_cb;
	p->capacity = camel_pacer_clamp_capacity(capacity);
	p->queue = (camel_pacer_packet_t*)calloc(p->capacity, sizeof(camel_pacer_packet_t));
	if (p->queue == NULL) {
		if (p->mu_inited) {
			(void)pthread_mutex_destroy(&p->mu);
			p->mu_inited = 0;
		}
		memset(p, 0, sizeof(*p));
		return -1;
	}

	p->min_pacing_bitrate_bps = 500 * 1000;
	p->pacing_bitrate_bps = p->min_pacing_bitrate_bps;
	p->congestion_window_bytes = (size_t)-1;
	p->max_burst_bytes = 12 * 1024;
	p->last_update_ts_ms = -1;
	p->budget_bytes = 0;
	return 0;
}

void camel_pacer_destroy(camel_pacer_t* p)
{
	if (p == NULL)
		return;
	free(p->queue);
	if (p->mu_inited) {
		(void)pthread_mutex_destroy(&p->mu);
		p->mu_inited = 0;
	}
	memset(p, 0, sizeof(*p));
}

void camel_pacer_set_estimate_bitrate(camel_pacer_t* p, uint32_t bitrate_bps)
{
	if (p == NULL)
		return;
	camel_pacer_lock(p);
	p->pacing_bitrate_bps = camel_pacer_clamp_bitrate(bitrate_bps, p->min_pacing_bitrate_bps);
	camel_pacer_unlock(p);
}

void camel_pacer_set_bitrate_limits(camel_pacer_t* p, uint32_t min_bitrate_bps)
{
	if (p == NULL)
		return;
	camel_pacer_lock(p);
	p->min_pacing_bitrate_bps = min_bitrate_bps > 0 ? min_bitrate_bps : 500 * 1000;
	p->pacing_bitrate_bps = camel_pacer_clamp_bitrate(p->pacing_bitrate_bps, p->min_pacing_bitrate_bps);
	camel_pacer_unlock(p);
}

void camel_pacer_set_congestion_window(camel_pacer_t* p, size_t cwnd_bytes)
{
	if (p == NULL)
		return;
	camel_pacer_lock(p);
	p->congestion_window_bytes = cwnd_bytes;
	camel_pacer_unlock(p);
}

void camel_pacer_set_outstanding(camel_pacer_t* p, size_t outstanding_bytes)
{
	if (p == NULL)
		return;
	camel_pacer_lock(p);
	p->outstanding_bytes = outstanding_bytes;
	camel_pacer_unlock(p);
}

void camel_pacer_set_max_burst_bytes(camel_pacer_t* p, size_t max_burst_bytes)
{
	if (p == NULL)
		return;
	if (max_burst_bytes == 0)
		return;
	camel_pacer_lock(p);
	p->max_burst_bytes = max_burst_bytes;
	camel_pacer_unlock(p);
}

int camel_pacer_insert_packet(camel_pacer_t* p, uint32_t packet_id, int retrans, size_t size, int padding, int64_t now_ts_ms)
{
	if (p == NULL || p->queue == NULL)
		return -1;
	camel_pacer_lock(p);
	if (p->size >= p->capacity)
	{
		camel_pacer_unlock(p);
		return -1;
	}

	uint32_t pos = p->tail % p->capacity;
	p->queue[pos].packet_id = packet_id;
	p->queue[pos].retrans = retrans;
	p->queue[pos].size = size;
	p->queue[pos].padding = padding;
	p->queue[pos].enqueue_ts_ms = now_ts_ms;

	p->tail = (p->tail + 1) % p->capacity;
	p->size++;
	camel_pacer_unlock(p);
	return 0;
}

static void camel_pacer_update_budget(camel_pacer_t* p, int64_t now_ts_ms)
{
	if (p->last_update_ts_ms < 0) {
		p->last_update_ts_ms = now_ts_ms;
		return;
	}
	int64_t elapsed_ms = now_ts_ms - p->last_update_ts_ms;
	if (elapsed_ms <= 0)
		return;
	p->last_update_ts_ms = now_ts_ms;

	int64_t add_bytes = ((int64_t)p->pacing_bitrate_bps * elapsed_ms) / 8000;
	if (add_bytes <= 0)
		add_bytes = 1;
	p->budget_bytes += add_bytes;

	int64_t max_budget = (int64_t)p->max_burst_bytes;
	if (p->budget_bytes > max_budget)
		p->budget_bytes = max_budget;
}

void camel_pacer_try_transmit(camel_pacer_t* p, int64_t now_ts_ms)
{
	if (p == NULL || p->queue == NULL || p->send_cb == NULL)
		return;

	camel_pacer_lock(p);
	camel_pacer_update_budget(p, now_ts_ms);
	if (p->budget_bytes <= 0)
	{
		camel_pacer_unlock(p);
		return;
	}

	size_t burst_sent = 0;
	while (p->size > 0) {
		if ((int64_t)burst_sent >= p->budget_bytes)
			break;
		if (burst_sent >= p->max_burst_bytes)
			break;
		if (p->congestion_window_bytes != (size_t)-1 && p->outstanding_bytes >= p->congestion_window_bytes)
			break;

		camel_pacer_packet_t* pkt = &p->queue[p->head % p->capacity];
		size_t pkt_size = pkt->size;
		if (pkt_size == 0)
			pkt_size = 1;

		if (burst_sent + pkt_size > p->max_burst_bytes && burst_sent > 0)
			break;
		if ((int64_t)(burst_sent + pkt_size) > p->budget_bytes && burst_sent > 0)
			break;

		camel_pace_send_func cb = p->send_cb;
		void* handler = p->handler;
		uint32_t packet_id = pkt->packet_id;
		int retrans = pkt->retrans;
		size_t size = pkt->size;
		int padding = pkt->padding;
		burst_sent += pkt_size;
		p->outstanding_bytes += pkt_size;

		memset(pkt, 0, sizeof(*pkt));
		p->head = (p->head + 1) % p->capacity;
		p->size--;
		camel_pacer_unlock(p);
		cb(handler, packet_id, retrans, size, padding);
		camel_pacer_lock(p);
	}

	p->budget_bytes -= (int64_t)burst_sent;
	if (p->budget_bytes < 0)
		p->budget_bytes = 0;
	camel_pacer_unlock(p);
}
