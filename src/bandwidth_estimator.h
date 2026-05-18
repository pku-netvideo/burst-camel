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

typedef struct {
    uint32_t    frame_id;
    uint32_t    packet_count;
    uint64_t    bytes_excluding_first;
    uint64_t    first_recv_ts_us;
    uint64_t    last_recv_ts_us;
    uint64_t    delay_us;
} camel_frame_sample_t;

typedef struct {
    double      avg_bandwidth_bps;
    uint64_t    min_delay_us;
    uint64_t    last_bandwidth_bps;
    uint32_t    valid_samples;
    double      ewma_alpha;
} camel_estimator_t;

void        camel_estimator_init(camel_estimator_t* est, double ewma_alpha);
void        camel_estimator_reset(camel_estimator_t* est);
int         camel_estimator_add_sample(camel_estimator_t* est, const camel_frame_sample_t* sample);
uint64_t    camel_estimator_get_bdp_bytes(const camel_estimator_t* est);
uint64_t    camel_estimator_get_bandwidth(const camel_estimator_t* est);

#ifdef __cplusplus
}
#endif

#endif
