/*
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_bandwidth_estimator_h_
#define __camel_bandwidth_estimator_h_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES 128

typedef struct {
    uint32_t    group_id;
    uint32_t    packet_count;
    uint64_t    bytes_excluding_first;
    uint64_t    first_recv_ts_us;
    uint64_t    last_recv_ts_us;
    uint64_t    delay_us;
} camel_group_sample_t;

typedef camel_group_sample_t camel_frame_sample_t;

typedef struct {
    double      avg_bandwidth_bps;
    uint64_t    min_delay_us;
    uint64_t    last_bandwidth_bps;
    uint32_t    valid_samples;
    double      ewma_alpha;
    uint64_t    delay_samples_us[CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES];
    int64_t     delay_samples_ts_ms[CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES];
    uint32_t    delay_count;
    uint32_t    delay_next_index;
    uint32_t    delay_window_ms;
    uint32_t    delay_window_size;
    int         delay_window_by_samples;
} camel_estimator_t;

void        camel_estimator_init(camel_estimator_t* est, double ewma_alpha);
void        camel_estimator_reset(camel_estimator_t* est);
void        camel_estimator_set_delay_window(camel_estimator_t* est, int window_by_samples, uint32_t window_value);
int         camel_estimator_add_sample(camel_estimator_t* est, const camel_frame_sample_t* sample, int64_t now_ts_ms);
uint64_t    camel_estimator_get_bdp_bytes(const camel_estimator_t* est);
uint64_t    camel_estimator_get_bandwidth(const camel_estimator_t* est);

#ifdef __cplusplus
}
#endif

#endif
