/*
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/

#include "bandwidth_estimator.h"

void camel_estimator_init(camel_estimator_t* est, double ewma_alpha)
{
    if (est == NULL)
        return;
    est->avg_bandwidth_bps = 0.0;
    est->min_delay_us = UINT64_MAX;
    est->last_bandwidth_bps = 0;
    est->valid_samples = 0;
    est->ewma_alpha = ewma_alpha;
}

void camel_estimator_reset(camel_estimator_t* est)
{
    if (est == NULL)
        return;
    est->avg_bandwidth_bps = 0.0;
    est->min_delay_us = UINT64_MAX;
    est->last_bandwidth_bps = 0;
    est->valid_samples = 0;
}

int camel_estimator_add_sample(camel_estimator_t* est, const camel_frame_sample_t* sample)
{
    uint64_t duration_us;
    uint64_t bandwidth_bps;
    double avg_bandwidth;

    if (est == NULL || sample == NULL)
        return -1;

    if (sample->packet_count < 2)
        return -1;

    if (sample->last_recv_ts_us <= sample->first_recv_ts_us)
        return -1;

    if (sample->first_recv_ts_us == 0)
        return -1;

    if (sample->delay_us < est->min_delay_us)
        est->min_delay_us = sample->delay_us;

    duration_us = sample->last_recv_ts_us - sample->first_recv_ts_us;
    if (duration_us == 0)
        return -1;

    bandwidth_bps = (sample->bytes_excluding_first * 8ULL * 1000000ULL) / duration_us;

    est->last_bandwidth_bps = bandwidth_bps;
    est->valid_samples++;

    if (est->avg_bandwidth_bps == 0.0) {
        est->avg_bandwidth_bps = (double)bandwidth_bps;
    } else {
        avg_bandwidth = est->avg_bandwidth_bps;
        est->avg_bandwidth_bps = est->ewma_alpha * (double)bandwidth_bps +
                                 (1.0 - est->ewma_alpha) * avg_bandwidth;
    }

    return 0;
}

uint64_t camel_estimator_get_bdp_bytes(const camel_estimator_t* est)
{
    if (est == NULL)
        return 0;

    if (est->avg_bandwidth_bps == 0.0 || est->min_delay_us == UINT64_MAX)
        return 0;

    return (uint64_t)(est->avg_bandwidth_bps * (double)est->min_delay_us / 8000000.0);
}

uint64_t camel_estimator_get_bandwidth(const camel_estimator_t* est)
{
    if (est == NULL)
        return 0;

    return (uint64_t)est->avg_bandwidth_bps;
}
