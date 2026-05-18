#include "network_simulator.h"

#include <string.h>
#include <stdlib.h>

#define MAX_PACKET_QUEUE 10000
#define MAX_SENT_GROUPS 1000
#define BURST_INTERVAL_BYTES 2048
#define BURST_MAX_INTERVALS CAMEL_NETWORK_MAX_INTERVALS

void camel_network_simulator_init(camel_network_simulator_t* sim)
{
    if (sim == NULL)
        return;

    memset(sim, 0, sizeof(*sim));

    sim->packet_queue = (camel_queued_packet_t*)calloc(MAX_PACKET_QUEUE, sizeof(camel_queued_packet_t));
    sim->queue_capacity = MAX_PACKET_QUEUE;

    sim->sent_groups = (camel_sent_group_t*)calloc(MAX_SENT_GROUPS, sizeof(camel_sent_group_t));
    sim->sent_groups_capacity = MAX_SENT_GROUPS;

    sim->scenario.source = CAMEL_TRACE_SOURCE_SYNTHETIC;
    sim->scenario.trace_count = 0;
    sim->scenario.trace_states = NULL;
}

void camel_network_simulator_destroy(camel_network_simulator_t* sim)
{
    if (sim == NULL)
        return;

    if (sim->packet_queue) {
        free(sim->packet_queue);
        sim->packet_queue = NULL;
    }

    if (sim->sent_groups) {
        free(sim->sent_groups);
        sim->sent_groups = NULL;
    }

    if (sim->scenario.trace_states) {
        free(sim->scenario.trace_states);
        sim->scenario.trace_states = NULL;
    }
}

int camel_network_simulator_load_scenario(camel_network_simulator_t* sim,
                                        camel_network_state_t* states,
                                        uint32_t state_count)
{
    if (sim == NULL || states == NULL || state_count == 0)
        return -1;

    if (sim->scenario.trace_states)
        free(sim->scenario.trace_states);

    sim->scenario.trace_states = (camel_network_state_t*)malloc(sizeof(camel_network_state_t) * state_count);
    if (sim->scenario.trace_states == NULL)
        return -1;

    memcpy(sim->scenario.trace_states, states, sizeof(camel_network_state_t) * state_count);
    sim->scenario.trace_count = state_count;
    sim->scenario.current_trace_index = 0;
    sim->scenario.source = CAMEL_TRACE_SOURCE_SYNTHETIC;

    sim->current = states[0];
    sim->current.buffer_occupancy = 0.0;

    camel_network_simulator_reset_stats(sim);

    return 0;
}

int camel_network_simulator_add_synthetic_scenario(camel_network_simulator_t* sim,
                                                  uint32_t capacity_bps,
                                                  uint32_t delay_ms,
                                                  double loss_rate,
                                                  uint32_t buffer_bytes)
{
    camel_network_state_t state;
    memset(&state, 0, sizeof(state));
    state.capacity_bps = capacity_bps;
    state.base_delay_ms = delay_ms;
    state.loss_rate = loss_rate;
    state.buffer_bytes = buffer_bytes;
    state.buffer_occupancy = 0.0;

    return camel_network_simulator_load_scenario(sim, &state, 1);
}

static uint32_t camel_network_simulator_get_current_capacity(camel_network_simulator_t* sim)
{
    if (sim->scenario.trace_count == 0)
        return 1000000;

    return sim->scenario.trace_states[sim->scenario.current_trace_index].capacity_bps;
}

static uint32_t camel_network_simulator_get_current_delay(camel_network_simulator_t* sim)
{
    if (sim->scenario.trace_count == 0)
        return 50000;

    return sim->scenario.trace_states[sim->scenario.current_trace_index].base_delay_ms * 1000;
}

static double camel_network_simulator_get_current_loss(camel_network_simulator_t* sim)
{
    if (sim->scenario.trace_count == 0)
        return 0.0;

    return sim->scenario.trace_states[sim->scenario.current_trace_index].loss_rate;
}

void camel_network_simulator_update_time(camel_network_simulator_t* sim, uint64_t now_us)
{
    uint32_t i;
    uint64_t elapsed_us;
    uint32_t capacity_bps;
    uint32_t base_delay_us;
    uint32_t bytes_to_dequeue;

    if (sim == NULL)
        return;

    if (sim->sim_time_us == 0)
        sim->sim_time_us = now_us;

    elapsed_us = now_us - sim->sim_time_us;
    sim->sim_time_us = now_us;

    if (elapsed_us == 0)
        return;

    capacity_bps = camel_network_simulator_get_current_capacity(sim);
    base_delay_us = camel_network_simulator_get_current_delay(sim);

    bytes_to_dequeue = (uint32_t)((uint64_t)capacity_bps * elapsed_us / 8000000);
    if (bytes_to_dequeue == 0 && elapsed_us > 0)
        bytes_to_dequeue = 1;

    for (i = 0; i < bytes_to_dequeue && sim->queue_size > 0; i++) {
        camel_queued_packet_t* pkt = &sim->packet_queue[sim->queue_head];
        if (pkt->size <= bytes_to_dequeue - i) {
            sim->queue_size--;
            sim->queue_head = (sim->queue_head + 1) % sim->queue_capacity;
            sim->buffer_stats.total_departures++;
            i += (uint32_t)pkt->size - 1;
        } else {
            break;
        }
    }

    if (sim->queue_size > 0) {
        uint32_t queue_delay = (uint32_t)((uint64_t)sim->queue_size * 8000000 / capacity_bps);
        sim->buffer_stats.queue_delay_us = queue_delay + base_delay_us;
    } else {
        sim->buffer_stats.queue_delay_us = base_delay_us;
    }

    if (sim->buffer_stats.max_buffer_bytes > 0) {
        sim->buffer_stats.avg_occupancy = (double)sim->buffer_stats.buffer_bytes / (double)sim->buffer_stats.max_buffer_bytes;
    }
}

int camel_network_simulator_send_group(camel_network_simulator_t* sim,
                                      uint32_t group_id,
                                      size_t group_size,
                                      uint32_t packet_count,
                                      uint64_t send_ts_us)
{
    uint32_t i;
    uint32_t bytes_sent = 0;
    uint32_t packet_size;
    uint32_t max_buffer;

    if (sim == NULL)
        return -1;

    max_buffer = sim->scenario.trace_states[sim->scenario.current_trace_index].buffer_bytes;
    if (max_buffer == 0)
        max_buffer = 64 * 1024;

    sim->buffer_stats.max_buffer_bytes = max_buffer;

    for (i = 0; i < packet_count; i++) {
        packet_size = (uint32_t)((i == 0) ? 200 : (group_size - 200) / (packet_count - 1));
        if (i == packet_count - 1)
            packet_size = (uint32_t)(group_size - bytes_sent);

        if (sim->queue_size >= sim->queue_capacity)
            break;

        if (sim->buffer_stats.buffer_bytes + packet_size > max_buffer) {
            sim->buffer_stats.total_drops++;
            sim->buffer_stats.overflow_count++;
            continue;
        }

        uint32_t queue_pos = (sim->queue_tail) % sim->queue_capacity;
        sim->packet_queue[queue_pos].packet_send_ts_us = send_ts_us;
        sim->packet_queue[queue_pos].size = packet_size;
        sim->packet_queue[queue_pos].interval_index = bytes_sent / BURST_INTERVAL_BYTES;

        sim->queue_tail = (sim->queue_tail + 1) % sim->queue_capacity;
        sim->queue_size++;

        sim->buffer_stats.buffer_bytes += packet_size;
        sim->buffer_stats.total_arrivals++;

        bytes_sent += packet_size;
    }

    if (sim->sent_groups_count >= sim->sent_groups_capacity)
        sim->sent_groups_count = 0;

    uint32_t group_idx = sim->sent_groups_count % sim->sent_groups_capacity;
    sim->sent_groups[group_idx].group_id = group_id;
    sim->sent_groups[group_idx].send_ts_us = send_ts_us;
    sim->sent_groups[group_idx].size = (uint32_t)group_size;
    sim->sent_groups[group_idx].packet_count = packet_count;
    sim->sent_groups_count++;

    sim->total_sent_bytes += (uint32_t)group_size;
    sim->group_count++;
    sim->packet_count += packet_count;

    return 0;
}

camel_group_event_t* camel_network_simulator_receive_group(camel_network_simulator_t* sim,
                                                        uint64_t now_ts_us)
{
    static camel_group_event_t event;
    uint32_t capacity_bps;
    uint32_t base_delay_us;
    double loss_rate;
    uint64_t transit_time_us;
    uint32_t recv_size = 0;
    uint32_t lost_size = 0;
    uint32_t recv_packets = 0;
    uint32_t lost_packets = 0;
    uint64_t earliest_send_ts = UINT64_MAX;
    uint64_t latest_send_ts = 0;

    if (sim == NULL || sim->sent_groups_count == 0)
        return NULL;

    memset(&event, 0, sizeof(event));

    capacity_bps = camel_network_simulator_get_current_capacity(sim);
    base_delay_us = camel_network_simulator_get_current_delay(sim);
    loss_rate = camel_network_simulator_get_current_loss(sim);

    (void)capacity_bps;
    transit_time_us = (uint64_t)(sim->buffer_stats.queue_delay_us);
    if (transit_time_us < base_delay_us)
        transit_time_us = base_delay_us;

    uint32_t groups_to_process = sim->sent_groups_count;
    if (groups_to_process == 0)
        return NULL;

    for (uint32_t i = 0; i < groups_to_process && i < sim->sent_groups_capacity; i++) {
        uint32_t group_idx = (sim->sent_groups_count - groups_to_process + i) % sim->sent_groups_capacity;
        camel_sent_group_t* sent = &sim->sent_groups[group_idx];

        if (sent->send_ts_us == 0)
            continue;

        uint64_t expected_recv_ts = sent->send_ts_us + transit_time_us;
        if (expected_recv_ts > now_ts_us)
            continue;

        event.group_id = sent->group_id;
        event.send_ts_us = sent->send_ts_us;
        event.first_send_ts_us = sent->send_ts_us;
        event.group_size = sent->size;
        event.packet_count = sent->packet_count;
        event.first_packet_size = 200;

        for (uint32_t p = 0; p < sent->packet_count; p++) {
            uint32_t pkt_size = (p == 0) ? 200 : (sent->size - 200) / (sent->packet_count - 1);
            if (p == sent->packet_count - 1)
                pkt_size = sent->size - recv_size - lost_size;

            double random_val = (double)rand() / (double)RAND_MAX;
            if (random_val < loss_rate) {
                lost_size += pkt_size;
                lost_packets++;
                uint32_t interval_idx = recv_size / BURST_INTERVAL_BYTES;
                if (interval_idx < BURST_MAX_INTERVALS)
                    event.interval_loss[interval_idx]++;
            } else {
                recv_size += pkt_size;
                recv_packets++;
            }

            uint64_t pkt_send_ts = sent->send_ts_us + p * 1000ULL;
            if (pkt_send_ts < earliest_send_ts)
                earliest_send_ts = pkt_send_ts;
            if (pkt_send_ts > latest_send_ts)
                latest_send_ts = pkt_send_ts;
        }

        event.recv_size = recv_size;
        event.recv_packet_count = recv_packets;
        event.lost_bytes = lost_size;
        event.lost_packet_count = lost_packets;
        event.timestamp_us = now_ts_us;
        event.first_send_ts_us = earliest_send_ts;
        event.queue_delay_us = sim->buffer_stats.queue_delay_us;

        if (sim->buffer_stats.buffer_bytes >= recv_size)
            sim->buffer_stats.buffer_bytes -= recv_size;
        else
            sim->buffer_stats.buffer_bytes = 0;

        sim->total_received_bytes += recv_size;
        sim->total_lost_bytes += lost_size;

        memset(&sim->sent_groups[group_idx], 0, sizeof(camel_sent_group_t));
        break;
    }

    return &event;
}

void camel_network_simulator_get_stats(camel_network_simulator_t* sim,
                                      camel_network_stats_t* stats)
{
    if (sim == NULL || stats == NULL)
        return;

    memset(stats, 0, sizeof(*stats));

    stats->current = sim->current;
    stats->sim_time_us = sim->sim_time_us;
    stats->total_sent_bytes = sim->total_sent_bytes;
    stats->total_received_bytes = sim->total_received_bytes;
    stats->total_lost_bytes = sim->total_lost_bytes;
    stats->group_count = sim->group_count;
    stats->packet_count = sim->packet_count;
    stats->buffer_stats = sim->buffer_stats;
}

void camel_network_simulator_reset_stats(camel_network_simulator_t* sim)
{
    if (sim == NULL)
        return;

    sim->sim_time_us = 0;
    sim->total_sent_bytes = 0;
    sim->total_received_bytes = 0;
    sim->total_lost_bytes = 0;
    sim->group_count = 0;
    sim->packet_count = 0;

    memset(&sim->buffer_stats, 0, sizeof(camel_router_buffer_stats_t));

    sim->queue_size = 0;
    sim->queue_head = 0;
    sim->queue_tail = 0;
    sim->sent_groups_count = 0;
}

int camel_network_simulator_load_trace_file(camel_network_simulator_t* sim,
                                           const char* filename)
{
    (void)sim;
    (void)filename;
    return -1;
}
