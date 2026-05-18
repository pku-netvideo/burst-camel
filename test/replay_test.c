#include "test_framework.h"
#include "bandwidth_estimator.h"
#include "congestion_detector.h"
#include "burst_controller.h"
#include "network_simulator.h"

#include <string.h>
#include <math.h>

static int test_constant_1mbps_undershoot(void)
{
    int failed = 0;
    camel_network_simulator_t net;
    camel_estimator_t est;
    camel_congestion_detector_t det;
    uint32_t frame_id = 0;
    uint64_t now_us = 0;
    uint32_t interval_us = 33333;

    camel_network_simulator_init(&net);
    camel_network_simulator_add_synthetic_scenario(&net, 1000000, 50, 0.0, 64 * 1024);

    camel_estimator_init(&est, 0.1);
    camel_congestion_detector_init(&det, 8, 0.5, 0.2);

    for (uint32_t i = 0; i < 60; i++) {
        uint32_t frame_size = 4000;
        uint32_t packet_count = 3;

        camel_network_simulator_send_frame(&net, frame_id, frame_size, packet_count, now_us);

        now_us += interval_us;
        camel_network_simulator_update_time(&net, now_us);

        camel_frame_event_t* evt = camel_network_simulator_receive_frame(&net, now_us);
        if (evt != NULL && evt->recv_size > 0) {
            camel_frame_sample_t sample;
            memset(&sample, 0, sizeof(sample));
            sample.frame_id = evt->frame_id;
            sample.packet_count = evt->recv_packet_count;
            sample.bytes_excluding_first = evt->recv_size - evt->first_packet_size;
            sample.first_recv_ts_us = evt->first_send_ts_us + 50000;
            sample.last_recv_ts_us = evt->timestamp_us;
            sample.delay_us = 50000;

            if (camel_estimator_add_sample(&est, &sample) == 0) {
                (void)camel_congestion_detector_add_sample(&det, evt->recv_size, sample.delay_us);
            }
        }
        frame_id++;
    }

    double avg_bandwidth_bps = (double)camel_estimator_get_bandwidth(&est);

    FCC_EXPECT_TRUE("Estimate near capacity range",
        avg_bandwidth_bps > 500000 && avg_bandwidth_bps < 3000000);
    FCC_EXPECT_TRUE("gamma remains 1.0 (no congestion in stable network)",
        fabs(det.gamma - 1.0) < 0.01);

    camel_network_simulator_destroy(&net);
    return failed;
}

static int test_physical_random_loss(void)
{
    int failed = 0;
    camel_network_simulator_t net;
    camel_burst_controller_t burst;
    uint32_t frame_id = 0;
    uint64_t now_us = 0;
    uint32_t interval_us = 33333;
    camel_network_simulator_init(&net);
    camel_network_simulator_add_synthetic_scenario(&net, 2000000, 50, 0.05, 64 * 1024);

    camel_burst_controller_init(&burst, 2048, 12 * 1024, 64 * 1024);

    for (uint32_t i = 0; i < 180; i++) {
        uint32_t frame_size = 6000;
        uint32_t packet_count = 4;

        camel_network_simulator_send_frame(&net, frame_id, frame_size, packet_count, now_us);

        now_us += interval_us;
        camel_network_simulator_update_time(&net, now_us);

        camel_frame_event_t* evt = camel_network_simulator_receive_frame(&net, now_us);
        if (evt != NULL) {
            for (uint32_t j = 0; j < CAMEL_NETWORK_MAX_INTERVALS; j++) {
                if (evt->interval_loss[j] > 0) {
                    camel_burst_controller_record_interval(&burst, j, 1);
                }
            }
            if (i % 10 == 0) {
                (void)camel_burst_controller_maybe_update(&burst, (uint64_t)(now_us / 1000));
            }
        }
        frame_id++;
    }

    FCC_EXPECT_TRUE("burst stays within bounds", burst.current_burst_bytes >= 2048 && burst.current_burst_bytes <= 65536);
    FCC_EXPECT_TRUE("burst does not collapse under physical loss", burst.current_burst_bytes >= 2048);
    FCC_EXPECT_EQ("no fallback mode under uniform physical loss", burst.fallback_mode, 0);

    camel_network_simulator_destroy(&net);
    return failed;
}

int test_fcc_replay_simulation(void)
{
    int failed = 0;

    failed += test_constant_1mbps_undershoot();
    failed += test_physical_random_loss();

    return failed;
}
