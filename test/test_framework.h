#ifndef __fcc_test_h_
#define __fcc_test_h_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FCC_EXPECT_TRUE(msg, cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        failed++; \
    } \
} while(0)

#define FCC_EXPECT_EQ(msg, a, b) FCC_EXPECT_TRUE(msg, (a) == (b))
#define FCC_EXPECT_GT(msg, a, b) FCC_EXPECT_TRUE(msg, (a) > (b))
#define FCC_EXPECT_LT(msg, a, b) FCC_EXPECT_TRUE(msg, (a) < (b))
#define FCC_EXPECT_GE(msg, a, b) FCC_EXPECT_TRUE(msg, (a) >= (b))
#define FCC_EXPECT_LE(msg, a, b) FCC_EXPECT_TRUE(msg, (a) <= (b))
#define FCC_EXPECT_DOUBLE_EQ(msg, a, b, eps) FCC_EXPECT_TRUE(msg, ((a)-(b) < eps) && ((b)-(a) < eps))

int test_bandwidth_estimator(void);
int test_congestion_detector(void);
int test_burst_controller(void);
int test_feedback_codec(void);
int test_receiver_aggregation(void);
int test_sender_synthetic(void);
int test_fcc_replay_simulation(void);

#ifdef __cplusplus
}
#endif

#endif
