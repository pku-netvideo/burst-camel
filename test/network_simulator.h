#ifndef __camel_network_simulator_h_
#define __camel_network_simulator_h_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMEL_NETWORK_MAX_INTERVALS 64

typedef enum {
    CAMEL_TRACE_SOURCE_SYNTHETIC = 0,
    CAMEL_TRACE_SOURCE_FILE = 1,
    CAMEL_TRACE_SOURCE_LIVE = 2
} camel_trace_source_t;

typedef struct {
    uint32_t    capacity_bps;
    uint32_t    base_delay_ms;
    double      loss_rate;
    uint32_t    buffer_bytes;
    double      buffer_occupancy;
} camel_network_state_t;

typedef struct {
    uint64_t            timestamp_us;
    uint32_t            group_id;
    size_t              group_size;
    uint32_t            packet_count;
    uint32_t            first_packet_size;
    uint64_t            send_ts_us;
    uint64_t            first_send_ts_us;
    uint32_t            inflight_bytes;
    uint32_t            queue_delay_us;
    uint32_t            recv_size;
    uint32_t            recv_packet_count;
    uint32_t            lost_bytes;
    uint32_t            lost_packet_count;
    uint32_t            interval_loss[CAMEL_NETWORK_MAX_INTERVALS];
} camel_group_event_t;

typedef struct {
    uint32_t            group_id;
    uint64_t            send_ts_us;
    size_t              size;
    uint32_t            packet_count;
} camel_sent_group_t;

typedef struct {
    uint64_t            packet_send_ts_us;
    size_t              size;
    uint32_t            interval_index;
} camel_queued_packet_t;

typedef struct {
    uint32_t            buffer_bytes;
    uint32_t            max_buffer_bytes;
    uint32_t            queue_delay_us;
    uint32_t            overflow_count;
    uint32_t            total_arrivals;
    uint32_t            total_departures;
    uint32_t            total_drops;
    double              avg_occupancy;
} camel_router_buffer_stats_t;

typedef struct {
    camel_network_state_t     current;
    uint64_t                sim_time_us;
    uint32_t                total_sent_bytes;
    uint32_t                total_received_bytes;
    uint32_t                total_lost_bytes;
    uint32_t                group_count;
    uint32_t                packet_count;
    camel_router_buffer_stats_t buffer_stats;
} camel_network_stats_t;

typedef struct {
    camel_trace_source_t      source;
    uint32_t                current_trace_index;
    uint32_t                trace_count;
    camel_network_state_t*    trace_states;
} camel_network_scenario_t;

typedef struct {
    camel_network_scenario_t      scenario;
    camel_network_state_t         current;
    camel_queued_packet_t*        packet_queue;
    uint32_t                    queue_size;
    uint32_t                    queue_capacity;
    uint32_t                    queue_head;
    uint32_t                    queue_tail;
    camel_sent_group_t*           sent_groups;
    uint32_t                    sent_groups_count;
    uint32_t                    sent_groups_capacity;
    uint64_t                    sim_time_us;
    uint32_t                    total_sent_bytes;
    uint32_t                    total_received_bytes;
    uint32_t                    total_lost_bytes;
    uint32_t                    group_count;
    uint32_t                    packet_count;
    camel_router_buffer_stats_t   buffer_stats;
} camel_network_simulator_t;

void camel_network_simulator_init(camel_network_simulator_t* sim);
void camel_network_simulator_destroy(camel_network_simulator_t* sim);

int camel_network_simulator_load_scenario(camel_network_simulator_t* sim,
                                        camel_network_state_t* states,
                                        uint32_t state_count);
int camel_network_simulator_add_synthetic_scenario(camel_network_simulator_t* sim,
                                                  uint32_t capacity_bps,
                                                  uint32_t delay_ms,
                                                  double loss_rate,
                                                  uint32_t buffer_bytes);
void camel_network_simulator_update_time(camel_network_simulator_t* sim, uint64_t now_us);

int camel_network_simulator_send_group(camel_network_simulator_t* sim,
                                      uint32_t group_id,
                                      size_t group_size,
                                      uint32_t packet_count,
                                      uint64_t send_ts_us);

camel_group_event_t* camel_network_simulator_receive_group(camel_network_simulator_t* sim,
                                                        uint64_t now_ts_us);

void camel_network_simulator_get_stats(camel_network_simulator_t* sim,
                                      camel_network_stats_t* stats);

void camel_network_simulator_reset_stats(camel_network_simulator_t* sim);

int camel_network_simulator_load_trace_file(camel_network_simulator_t* sim,
                                           const char* filename);

#ifdef __cplusplus
}
#endif

#endif
