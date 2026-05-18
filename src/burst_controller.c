/*
* Copyright (c) 2017-2018 Razor, Inc.
* All rights reserved.
*
* See the file LICENSE for redistribution information.
*/

#include "burst_controller.h"
#include <stddef.h>
#include <string.h>

#define BURST_DEFAULT_MIN_BYTES 2048
#define BURST_DEFAULT_MAX_BYTES (64 * 1024)
#define BURST_UPDATE_INTERVAL_MS 5000
#define BURST_EXCESS_LOSS_THRESHOLD 0.1

static size_t burst_clamp_size(size_t value, size_t min_value, size_t max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static double burst_loss_rate(const camel_burst_interval_stat_t* stat)
{
    if (stat->sent == 0)
        return 0.0;
    return (double)stat->lost / (double)stat->sent;
}

void camel_burst_controller_init(camel_burst_controller_t* ctrl,
    size_t min_burst_bytes, size_t initial_burst_bytes, size_t max_burst_bytes)
{
    if (ctrl == NULL)
        return;
    
    if (min_burst_bytes == 0)
        min_burst_bytes = BURST_DEFAULT_MIN_BYTES;
    if (max_burst_bytes < min_burst_bytes)
        max_burst_bytes = BURST_DEFAULT_MAX_BYTES;
    if (initial_burst_bytes == 0)
        initial_burst_bytes = min_burst_bytes;
    
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->min_burst_bytes = min_burst_bytes;
    ctrl->max_burst_bytes = max_burst_bytes;
    ctrl->current_burst_bytes = burst_clamp_size(initial_burst_bytes, min_burst_bytes, max_burst_bytes);
    ctrl->update_interval_ms = BURST_UPDATE_INTERVAL_MS;
}

void camel_burst_controller_reset_stats(camel_burst_controller_t* ctrl)
{
    if (ctrl == NULL)
        return;
    
    memset(ctrl->intervals, 0, sizeof(ctrl->intervals));
}

void camel_burst_controller_record_interval(camel_burst_controller_t* ctrl,
    uint32_t interval_index, int lost)
{
    camel_burst_controller_record_interval_counts(ctrl, interval_index, 1, lost ? 1 : 0);
}

void camel_burst_controller_record_interval_counts(camel_burst_controller_t* ctrl,
    uint32_t interval_index, uint32_t sent, uint32_t lost)
{
    if (ctrl == NULL)
        return;
    
    if (interval_index >= CAMEL_BURST_MAX_INTERVALS)
        interval_index = CAMEL_BURST_MAX_INTERVALS - 1;
    
    ctrl->intervals[interval_index].sent += sent;
    ctrl->intervals[interval_index].lost += lost;
}

void camel_burst_controller_record_packet(camel_burst_controller_t* ctrl,
    size_t frame_offset_bytes, int lost)
{
    camel_burst_controller_record_interval(ctrl,
        (uint32_t)(frame_offset_bytes / CAMEL_BURST_INTERVAL_BYTES), lost);
}

int camel_burst_controller_maybe_update(camel_burst_controller_t* ctrl, uint64_t now_ts_ms)
{
    uint32_t i;
    int has_excess_loss = 0;
    int has_loss_above_l0 = 0;
    double base_loss;
    
    if (ctrl == NULL)
        return 0;
    
    if (ctrl->last_update_ts_ms != 0 &&
        now_ts_ms < ctrl->last_update_ts_ms + ctrl->update_interval_ms)
        return 0;
    
    ctrl->last_update_ts_ms = now_ts_ms;
    base_loss = 0.0;
    int base_set = 0;
    for (i = 0; i < CAMEL_BURST_MAX_INTERVALS; i++) {
        if (ctrl->intervals[i].sent == 0)
            continue;
        double loss = burst_loss_rate(&ctrl->intervals[i]);
        if (!base_set || loss < base_loss) {
            base_loss = loss;
            base_set = 1;
        }
    }
    ctrl->last_excess_loss_interval = UINT32_MAX;

    for (i = 0; i < CAMEL_BURST_MAX_INTERVALS; i++) {
        double loss;
        if (ctrl->intervals[i].sent == 0)
            continue;
        
        loss = burst_loss_rate(&ctrl->intervals[i]);
        if (loss > base_loss)
            has_loss_above_l0 = 1;
        if (loss > base_loss + BURST_EXCESS_LOSS_THRESHOLD) {
            has_excess_loss = 1;
            ctrl->last_excess_loss_interval = i;
            break;
        }
    }
    
    if (has_excess_loss) {
        if (ctrl->current_burst_bytes <= ctrl->min_burst_bytes) {
            ctrl->current_burst_bytes = ctrl->min_burst_bytes;
            ctrl->fallback_mode = has_loss_above_l0 ? 1 : 0;
        }
        else if (ctrl->current_burst_bytes - ctrl->min_burst_bytes < CAMEL_BURST_INTERVAL_BYTES)
            ctrl->current_burst_bytes = ctrl->min_burst_bytes;
        else
            ctrl->current_burst_bytes -= CAMEL_BURST_INTERVAL_BYTES;
    }
    else {
        ctrl->fallback_mode = 0;
        ctrl->current_burst_bytes = burst_clamp_size(
            ctrl->current_burst_bytes + CAMEL_BURST_INTERVAL_BYTES,
            ctrl->min_burst_bytes, ctrl->max_burst_bytes);
    }
    
    camel_burst_controller_reset_stats(ctrl);
    return 1;
}
