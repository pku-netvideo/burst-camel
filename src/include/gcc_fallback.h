/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_gcc_fallback_h_
#define __camel_gcc_fallback_h_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint32_t min_bitrate_bps;
	uint32_t max_bitrate_bps;
	uint32_t target_bitrate_bps;
	uint64_t min_delay_us;
	uint64_t last_delay_us;
} camel_gcc_fallback_t;

void camel_gcc_fallback_init(camel_gcc_fallback_t* g, uint32_t min_bps, uint32_t max_bps, uint32_t start_bps);
uint32_t camel_gcc_fallback_on_packet(camel_gcc_fallback_t* g,
	uint64_t send_ts_us,
	uint64_t recv_ts_us,
	uint32_t payload_size);

#ifdef __cplusplus
}
#endif

#endif

