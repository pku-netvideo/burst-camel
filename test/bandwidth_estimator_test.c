#include "test_framework.h"
#include "bandwidth_estimator.h"
#include <string.h>

static int test_basic_bandwidth_calculation(void)
{
    int failed = 0;
    camel_estimator_t est;
    camel_frame_sample_t sample;
    uint64_t bandwidth;

    printf("Test 1: Basic bandwidth calculation\n");

    camel_estimator_init(&est, 0.1);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 1;
    sample.packet_count = 3;
    sample.bytes_excluding_first = 9000;
    sample.first_recv_ts_us = 1000000;
    sample.last_recv_ts_us = 1010000;
    sample.delay_us = 50000;

    int ret = camel_estimator_add_sample(&est, &sample);
    FCC_EXPECT_EQ("Sample added successfully", ret, 0);

    bandwidth = camel_estimator_get_bandwidth(&est);
    printf("  Calculated bandwidth: %llu bps\n", (unsigned long long)bandwidth);
    FCC_EXPECT_GT("Bandwidth calculated", bandwidth, 0);

    FCC_EXPECT_EQ("One valid sample", est.valid_samples, 1);

    return failed;
}

static int test_bdp_calculation(void)
{
    int failed = 0;
    camel_estimator_t est;
    camel_frame_sample_t sample;
    uint64_t bdp;

    printf("Test 2: BDP calculation\n");

    camel_estimator_init(&est, 0.1);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 1;
    sample.packet_count = 3;
    sample.bytes_excluding_first = 9000;
    sample.first_recv_ts_us = 1000000;
    sample.last_recv_ts_us = 1010000;
    sample.delay_us = 50000;

    (void)camel_estimator_add_sample(&est, &sample);

    bdp = camel_estimator_get_bdp_bytes(&est);
    printf("  Calculated BDP: %llu bytes\n", (unsigned long long)bdp);
    FCC_EXPECT_GT("BDP calculated", bdp, 0);

    return failed;
}

static int test_invalid_samples(void)
{
    int failed = 0;
    camel_estimator_t est;
    camel_frame_sample_t sample;

    printf("Test 3: Invalid sample rejection\n");

    camel_estimator_init(&est, 0.1);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 1;
    sample.packet_count = 1;
    sample.bytes_excluding_first = 1000;
    sample.first_recv_ts_us = 1000000;
    sample.last_recv_ts_us = 1005000;
    sample.delay_us = 50000;

    int ret = camel_estimator_add_sample(&est, &sample);
    FCC_EXPECT_EQ("Single packet rejected", ret, -1);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 2;
    sample.packet_count = 3;
    sample.bytes_excluding_first = 1000;
    sample.first_recv_ts_us = 1000000;
    sample.last_recv_ts_us = 1000000;
    sample.delay_us = 50000;

    ret = camel_estimator_add_sample(&est, &sample);
    FCC_EXPECT_EQ("Non-increasing timestamp rejected", ret, -1);

    FCC_EXPECT_EQ("No valid samples", est.valid_samples, 0);

    return failed;
}

static int test_min_delay_maintenance(void)
{
    int failed = 0;
    camel_estimator_t est;
    camel_frame_sample_t sample;

    printf("Test 4: Minimum delay maintenance\n");

    camel_estimator_init(&est, 0.1);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 1;
    sample.packet_count = 3;
    sample.bytes_excluding_first = 9000;
    sample.first_recv_ts_us = 1000000;
    sample.last_recv_ts_us = 1010000;
    sample.delay_us = 50000;

    (void)camel_estimator_add_sample(&est, &sample);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 2;
    sample.packet_count = 3;
    sample.bytes_excluding_first = 9000;
    sample.first_recv_ts_us = 2000000;
    sample.last_recv_ts_us = 2010000;
    sample.delay_us = 60000;

    (void)camel_estimator_add_sample(&est, &sample);

    memset(&sample, 0, sizeof(sample));
    sample.frame_id = 3;
    sample.packet_count = 3;
    sample.bytes_excluding_first = 9000;
    sample.first_recv_ts_us = 3000000;
    sample.last_recv_ts_us = 3010000;
    sample.delay_us = 40000;

    (void)camel_estimator_add_sample(&est, &sample);

    FCC_EXPECT_EQ("Min delay is 40ms", est.min_delay_us, 40000);

    return failed;
}

int test_bandwidth_estimator(void)
{
    int failed = 0;

    printf("\n=== Bandwidth Estimator Tests ===\n\n");

    failed += test_basic_bandwidth_calculation();
    printf("\n");

    failed += test_bdp_calculation();
    printf("\n");

    failed += test_invalid_samples();
    printf("\n");

    failed += test_min_delay_maintenance();
    printf("\n");

    printf("=== Bandwidth Estimator Summary: %s ===\n\n",
           failed == 0 ? "ALL PASSED" : "SOME FAILED");

    return failed;
}
