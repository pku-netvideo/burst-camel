/*
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/

#include "congestion_detector.h"
#include <stddef.h>

void camel_congestion_detector_init(camel_congestion_detector_t* det,
    int window_by_samples, uint32_t window_value, double threshold_us_per_byte, double min_gamma)
{
    if (det == NULL)
        return;
    
    det->count = 0;
    det->next_index = 0;
    det->window_by_samples = window_by_samples != 0;
    det->window_ms = window_value > 0 ? window_value : 5000;
    det->window_size = window_value > 0 ? window_value : 8;
    det->threshold_us_per_byte = threshold_us_per_byte;
    det->gamma = 1.0;
    det->min_gamma = min_gamma;
    det->last_slope_us_per_byte = 0.0;
}

void camel_congestion_detector_reset(camel_congestion_detector_t* det)
{
    if (det == NULL)
        return;
    
    det->count = 0;
    det->next_index = 0;
    det->gamma = 1.0;
    det->last_slope_us_per_byte = 0.0;
}

camel_congestion_result_t camel_congestion_detector_add_sample(camel_congestion_detector_t* det,
    uint64_t inflight_bytes, uint64_t delay_us, int64_t now_ts_ms)
{
    camel_congestion_result_t result = {0, 0.0, 1.0};
    double slope = 0.0;
    
    if (det == NULL)
        return result;
    
    if (inflight_bytes == 0)
        return result;
    result.gamma = det->gamma;
    
    det->samples[det->next_index].inflight_bytes = inflight_bytes;
    det->samples[det->next_index].delay_us = delay_us;
    det->samples[det->next_index].ts_ms = now_ts_ms;
    det->next_index = (det->next_index + 1) % CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES;
    
    if (det->count < CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES)
        det->count++;
    
    if (det->count < 2)
        return result;

    uint32_t effective_count = det->count;
    if (det->window_by_samples && det->window_size > 0 && effective_count > det->window_size)
        effective_count = det->window_size;

    uint32_t start_idx = (det->next_index + CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES - effective_count)
                         % CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES;
    
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_xx = 0.0;
    uint32_t valid_count = 0;
    
    for (uint32_t i = 0; i < effective_count; i++) {
        uint32_t idx = (start_idx + i) % CAMEL_CONGESTION_DETECTOR_MAX_SAMPLES;
        if (!det->window_by_samples) {
            int64_t age_ms = now_ts_ms - det->samples[idx].ts_ms;
            if (age_ms < 0 || (uint32_t)age_ms > det->window_ms)
                continue;
        }
        double x = (double)det->samples[idx].inflight_bytes;
        double y = (double)det->samples[idx].delay_us;
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
        valid_count++;
    }

    if (valid_count < 2)
        return result;
    
    double denominator = sum_xx - sum_x * sum_x / valid_count;
    
    if (denominator > 0.0) {
        slope = (sum_xy - sum_x * sum_y / valid_count) / denominator;
    } else {
        slope = 0.0;
    }
    
    det->last_slope_us_per_byte = slope;
    result.slope_us_per_byte = slope;
    
    if (slope > det->threshold_us_per_byte) {
        det->gamma *= 0.95;
        if (det->gamma < det->min_gamma)
            det->gamma = det->min_gamma;
        result.congested = 1;
    } else {
        det->gamma = 1.0;
    }
    
    result.gamma = det->gamma;
    
    return result;
}
