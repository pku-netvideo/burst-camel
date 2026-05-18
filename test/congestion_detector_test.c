#include "test_framework.h"
#include "congestion_detector.h"
#include <string.h>

static int test_basic_congestion_detection(void)
{
    int failed = 0;
    camel_congestion_detector_t det;
    camel_congestion_result_t result;
    
    printf("Test 1: Basic congestion detection\n");
    
    camel_congestion_detector_init(&det, 8, 0.5, 0.2);
    
    result = camel_congestion_detector_add_sample(&det, 10000, 50000);
    FCC_EXPECT_EQ("No congestion initially", result.congested, 0);
    FCC_EXPECT_DOUBLE_EQ("Gamma = 1.0 initially", result.gamma, 1.0, 0.001);
    
    for (int i = 0; i < 7; i++) {
        result = camel_congestion_detector_add_sample(&det, 10000 + i * 5000, 50000 + i * 30000);
    }
    
    printf("  Final congestion: %d, gamma: %.4f, slope: %.6f\n", 
           result.congested, result.gamma, result.slope_us_per_byte);
    
    FCC_EXPECT_EQ("Should detect congestion with increasing pattern", result.congested, 1);
    FCC_EXPECT_LT("Gamma should decrease under congestion", result.gamma, 1.0);
    
    return failed;
}

static int test_gamma_window_scaling(void)
{
    int failed = 0;
    camel_congestion_detector_t det;
    camel_congestion_result_t result;
    
    printf("Test 2: Gamma window scaling\n");
    
    camel_congestion_detector_init(&det, 8, 0.5, 0.2);
    
    result = camel_congestion_detector_add_sample(&det, 10000, 50000);
    FCC_EXPECT_DOUBLE_EQ("Initial gamma = 1.0", result.gamma, 1.0, 0.001);
    
    for (int i = 0; i < 10; i++) {
        result = camel_congestion_detector_add_sample(&det, 10000 + i * 2000, 50000 + i * 20000);
    }
    
    printf("  Final gamma: %.4f\n", result.gamma);
    FCC_EXPECT_LT("Gamma should be < 1.0 after congestion", result.gamma, 1.0);
    FCC_EXPECT_GE("Gamma should be >= min_gamma", result.gamma, 0.2);
    
    return failed;
}

static int test_no_congestion_recovery(void)
{
    int failed = 0;
    camel_congestion_detector_t det;
    camel_congestion_result_t result;
    
    printf("Test 3: Recovery when no congestion\n");
    
    camel_congestion_detector_init(&det, 8, 0.5, 0.2);
    
    for (int i = 0; i < 10; i++) {
        result = camel_congestion_detector_add_sample(&det, 10000, 50000);
    }
    
    FCC_EXPECT_EQ("Should not be congested with stable delay", result.congested, 0);
    FCC_EXPECT_DOUBLE_EQ("Gamma should recover to 1.0", result.gamma, 1.0, 0.001);
    
    return failed;
}

int test_congestion_detector(void)
{
    int failed = 0;
    
    printf("\n=== Congestion Detector Tests ===\n\n");
    
    failed += test_basic_congestion_detection();
    printf("\n");
    
    failed += test_gamma_window_scaling();
    printf("\n");
    
    failed += test_no_congestion_recovery();
    printf("\n");
    
    printf("=== Congestion Detector Summary: %s ===\n\n",
           failed == 0 ? "ALL PASSED" : "SOME FAILED");
    
    return failed;
}
