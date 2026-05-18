/*
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_burst_controller_h_
#define __camel_burst_controller_h_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMEL_BURST_INTERVAL_BYTES 2048
#define CAMEL_BURST_MAX_INTERVALS 64

typedef struct {
    uint32_t    sent;
    uint32_t    lost;
} camel_burst_interval_stat_t;

typedef struct {
    size_t                      min_burst_bytes;
    size_t                      max_burst_bytes;
    size_t                      current_burst_bytes;
    uint64_t                    last_update_ts_ms;
    uint32_t                    update_interval_ms;
    int                         fallback_mode;
    camel_burst_interval_stat_t intervals[CAMEL_BURST_MAX_INTERVALS];
} camel_burst_controller_t;

void    camel_burst_controller_init(camel_burst_controller_t* ctrl,
    size_t min_burst_bytes, size_t initial_burst_bytes, size_t max_burst_bytes);
void    camel_burst_controller_reset_stats(camel_burst_controller_t* ctrl);
void    camel_burst_controller_record_interval(camel_burst_controller_t* ctrl,
    uint32_t interval_index, int lost);
void    camel_burst_controller_record_interval_counts(camel_burst_controller_t* ctrl,
    uint32_t interval_index, uint32_t sent, uint32_t lost);
void    camel_burst_controller_record_packet(camel_burst_controller_t* ctrl,
    size_t frame_offset_bytes, int lost);
int     camel_burst_controller_maybe_update(camel_burst_controller_t* ctrl, uint64_t now_ts_ms);

#ifdef __cplusplus
}
#endif

#endif
