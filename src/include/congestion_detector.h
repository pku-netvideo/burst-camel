/*
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_congestion_detector_h_
#define __camel_congestion_detector_h_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES 32

typedef struct {
    uint64_t    inflight_bytes;
    uint64_t    delay_us;
} camel_congestion_sample_t;

typedef struct {
    int         congested;
    double      slope_us_per_byte;
    double      gamma;
} camel_congestion_result_t;

typedef struct {
    camel_congestion_sample_t samples[CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES];
    uint32_t                  count;
    uint32_t                  next_index;
    uint32_t                  window_size;
    double                    threshold_us_per_byte;
    double                    gamma;
    double                    min_gamma;
    double                    last_slope_us_per_byte;
} camel_congestion_detector_t;

void camel_congestion_detector_init(camel_congestion_detector_t* det,
    uint32_t window_size, double threshold_us_per_byte, double min_gamma);
void camel_congestion_detector_reset(camel_congestion_detector_t* det);
camel_congestion_result_t camel_congestion_detector_add_sample(camel_congestion_detector_t* det,
    uint64_t inflight_bytes, uint64_t delay_us);

#ifdef __cplusplus
}
#endif

#endif
