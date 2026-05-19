/*-
* Copyright (c) 2026 burst-camel contributors.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#include "gcc_fallback.h"

#include <stddef.h>

static uint32_t camel_gcc_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
	if (hi > 0 && v > hi)
		return hi;
	if (v < lo)
		return lo;
	return v;
}

void camel_gcc_fallback_init(camel_gcc_fallback_t* g, uint32_t min_bps, uint32_t max_bps, uint32_t start_bps)
{
	if (g == NULL)
		return;
	g->min_bitrate_bps = min_bps > 0 ? min_bps : 500 * 1000;
	g->max_bitrate_bps = max_bps;
	g->target_bitrate_bps = camel_gcc_clamp_u32(start_bps > 0 ? start_bps : g->min_bitrate_bps,
		g->min_bitrate_bps, g->max_bitrate_bps);
	g->min_delay_us = 0;
	g->last_delay_us = 0;
}

uint32_t camel_gcc_fallback_on_packet(camel_gcc_fallback_t* g,
	uint64_t send_ts_us,
	uint64_t recv_ts_us,
	uint32_t payload_size)
{
	(void)payload_size;

	if (g == NULL)
		return 0;

	uint64_t delay_us = 0;
	if (recv_ts_us > send_ts_us)
		delay_us = recv_ts_us - send_ts_us;
	if (delay_us == 0)
		delay_us = 1;

	if (g->min_delay_us == 0 || delay_us < g->min_delay_us)
		g->min_delay_us = delay_us;

	uint64_t threshold_us = g->min_delay_us + 15000;
	int overuse = 0;
	if (delay_us > threshold_us && delay_us >= g->last_delay_us)
		overuse = 1;

	if (overuse) {
		double reduced = (double)g->target_bitrate_bps * 0.85;
		uint32_t next = (uint32_t)(reduced + 0.5);
		g->target_bitrate_bps = camel_gcc_clamp_u32(next, g->min_bitrate_bps, g->max_bitrate_bps);
	} else {
		double inc = (double)g->target_bitrate_bps * 0.05;
		if (inc < 1000.0)
			inc = 1000.0;
		uint32_t next = g->target_bitrate_bps + (uint32_t)(inc + 0.5);
		g->target_bitrate_bps = camel_gcc_clamp_u32(next, g->min_bitrate_bps, g->max_bitrate_bps);
	}

	g->last_delay_us = delay_us;
	return g->target_bitrate_bps;
}
