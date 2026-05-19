/*
* Copyright (c) 2026 burst-camel contributors.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/

#include "bandwidth_estimator.h"
#include <string.h>

static double camel_estimator_alpha(double ewma_alpha)
{
    if (ewma_alpha <= 0.0 || ewma_alpha > 1.0)
        return 0.1;
    return ewma_alpha;
}

void camel_estimator_init(camel_estimator_t* est, double ewma_alpha)
{
    if (est == NULL)
        return;

    memset(est, 0, sizeof(*est));
    est->ewma_alpha = camel_estimator_alpha(ewma_alpha);
    est->delay_window_ms = 5000;
    est->delay_window_size = 8;
    est->delay_window_by_samples = 0;
}

void camel_estimator_reset(camel_estimator_t* est)
{
    if (est == NULL)
        return;
    double alpha = est->ewma_alpha;
    uint32_t delay_window_ms = est->delay_window_ms;
    uint32_t delay_window_size = est->delay_window_size;
    int delay_window_by_samples = est->delay_window_by_samples;
    memset(est, 0, sizeof(*est));
    est->ewma_alpha = camel_estimator_alpha(alpha);
    est->delay_window_ms = delay_window_ms;
    est->delay_window_size = delay_window_size;
    est->delay_window_by_samples = delay_window_by_samples;
}

void camel_estimator_set_delay_window(camel_estimator_t* est, int window_by_samples, uint32_t window_value)
{
    if (est == NULL)
        return;
    est->delay_window_by_samples = window_by_samples != 0;
    if (est->delay_window_by_samples) {
        est->delay_window_size = window_value > 0 ? window_value : 8;
    } else {
        est->delay_window_ms = window_value > 0 ? window_value : 5000;
    }
}

static void camel_estimator_update_min_delay(camel_estimator_t* est, int64_t now_ts_ms)
{
    if (est == NULL)
        return;
    if (est->delay_count == 0) {
        est->min_delay_us = 0;
        return;
    }

    uint32_t effective_count = est->delay_count;
    if (est->delay_window_by_samples && est->delay_window_size > 0 && effective_count > est->delay_window_size)
        effective_count = est->delay_window_size;

    uint64_t min_delay = 0;
    uint32_t start_idx = (est->delay_next_index + CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES - effective_count)
                         % CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES;

    for (uint32_t i = 0; i < effective_count; i++) {
        uint32_t idx = (start_idx + i) % CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES;
        if (!est->delay_window_by_samples) {
            int64_t age_ms = now_ts_ms - est->delay_samples_ts_ms[idx];
            if (age_ms < 0 || (uint32_t)age_ms > est->delay_window_ms)
                continue;
        }
        uint64_t d = est->delay_samples_us[idx];
        if (d == 0)
            continue;
        if (min_delay == 0 || d < min_delay)
            min_delay = d;
    }

    est->min_delay_us = min_delay;
}

int camel_estimator_add_sample(camel_estimator_t* est, const camel_frame_sample_t* sample, int64_t now_ts_ms)
{
    uint64_t duration_us;
    uint64_t bandwidth_bps;

    if (est == NULL || sample == NULL)
        return -1;

    if (sample->packet_count < 2)
        return -1;

    if (sample->last_recv_ts_us <= sample->first_recv_ts_us)
        return -1;

    duration_us = sample->last_recv_ts_us - sample->first_recv_ts_us;
    bandwidth_bps = (sample->bytes_excluding_first * 8ULL * 1000000ULL) / duration_us;

    est->last_bandwidth_bps = bandwidth_bps;

    if (est->valid_samples == 0)
        est->avg_bandwidth_bps = (double)bandwidth_bps;
    else
        est->avg_bandwidth_bps = est->ewma_alpha * (double)bandwidth_bps +
                                 (1.0 - est->ewma_alpha) * est->avg_bandwidth_bps;

    est->delay_samples_us[est->delay_next_index] = sample->delay_us;
    est->delay_samples_ts_ms[est->delay_next_index] = now_ts_ms;
    est->delay_next_index = (est->delay_next_index + 1) % CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES;
    if (est->delay_count < CAMEL_ESTIMATOR_MAX_DELAY_SAMPLES)
        est->delay_count++;
    camel_estimator_update_min_delay(est, now_ts_ms);

    est->valid_samples++;

    return 0;
}

uint64_t camel_estimator_get_bdp_bytes(const camel_estimator_t* est)
{
    if (est == NULL)
        return 0;

    if (est->valid_samples == 0 || est->min_delay_us == 0)
        return 0;

    return (uint64_t)(est->avg_bandwidth_bps * (double)est->min_delay_us / 8000000.0);
}

uint64_t camel_estimator_get_bandwidth(const camel_estimator_t* est)
{
    if (est == NULL)
        return 0;

    if (est->valid_samples == 0)
        return 0;

    return (uint64_t)(est->avg_bandwidth_bps + 0.5);
}
