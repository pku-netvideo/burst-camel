#include "test_framework.h"
#include "burst_controller.h"
#include <string.h>
#include <stdint.h>

static void record_loss_rate(camel_burst_controller_t* ctrl, uint32_t interval, uint32_t sent, uint32_t lost)
{
    uint32_t i;
    
    for (i = 0; i < sent; i++)
        camel_burst_controller_record_interval(ctrl, interval, i < lost);
}

int test_burst_controller(void)
{
    int failed = 0;
    camel_burst_controller_t ctrl;
    
    printf("Test 1: No excess loss updates\n");
    camel_burst_controller_init(&ctrl, 2048, 4096, 8192);
    record_loss_rate(&ctrl, 0, 100, 1);
    record_loss_rate(&ctrl, 1, 100, 2);
    int result = camel_burst_controller_maybe_update(&ctrl, 5000);
    FCC_EXPECT_EQ("Should return 1", result, 1);
    printf("  Burst after update: %u bytes (expected 6144)\n", (unsigned)ctrl.current_burst_bytes);
    FCC_EXPECT_EQ("Burst increases by 2KB", ctrl.current_burst_bytes, 6144);
    FCC_EXPECT_EQ("No excess loss: last_excess_loss_interval is UINT32_MAX",
        ctrl.last_excess_loss_interval, UINT32_MAX);
    
    printf("\nTest 2: Excess loss updates\n");
    record_loss_rate(&ctrl, 0, 100, 1);
    record_loss_rate(&ctrl, 2, 100, 15);
    result = camel_burst_controller_maybe_update(&ctrl, 10000);
    FCC_EXPECT_EQ("Should return 1", result, 1);
    printf("  Burst after excess loss: %u bytes (expected 4096)\n", (unsigned)ctrl.current_burst_bytes);
    FCC_EXPECT_EQ("Burst decreases by 2KB", ctrl.current_burst_bytes, 4096);
    FCC_EXPECT_EQ("Excess loss interval recorded", ctrl.last_excess_loss_interval, 2U);
    
    printf("\nTest 3: Physical loss updates\n");
    record_loss_rate(&ctrl, 0, 100, 10);
    record_loss_rate(&ctrl, 1, 100, 11);
    result = camel_burst_controller_maybe_update(&ctrl, 15000);
    FCC_EXPECT_EQ("Should return 1", result, 1);
    printf("  Burst after physical loss: %u bytes (expected 6144)\n", (unsigned)ctrl.current_burst_bytes);
    FCC_EXPECT_EQ("Physical loss does not decrease", ctrl.current_burst_bytes, 6144);
    
    printf("\nTest 4: Minimum burst with excess loss\n");
    camel_burst_controller_init(&ctrl, 2048, 2048, 8192);
    record_loss_rate(&ctrl, 0, 100, 1);
    record_loss_rate(&ctrl, 3, 100, 20);
    result = camel_burst_controller_maybe_update(&ctrl, 5000);
    FCC_EXPECT_EQ("Should return 1", result, 1);
    printf("  Fallback mode: %d (expected 1)\n", ctrl.fallback_mode);
    FCC_EXPECT_EQ("Fallback enabled at min burst", ctrl.fallback_mode, 1);
    FCC_EXPECT_EQ("Excess loss interval recorded at min burst", ctrl.last_excess_loss_interval, 3U);
    
    printf("\nTest 5: Max burst clamp\n");
    camel_burst_controller_init(&ctrl, 2048, 8192, 8192);
    record_loss_rate(&ctrl, 0, 100, 0);
    result = camel_burst_controller_maybe_update(&ctrl, 5000);
    FCC_EXPECT_EQ("Should return 1", result, 1);
    printf("  Burst after update: %u bytes (expected 8192)\n", (unsigned)ctrl.current_burst_bytes);
    FCC_EXPECT_EQ("Burst clamped to max", ctrl.current_burst_bytes, 8192);
    
    printf("\nTest 6: Byte-count interval updates\n");
    camel_burst_controller_init(&ctrl, 2048, 4096, 8192);
    camel_burst_controller_record_interval_counts(&ctrl, 0, 2048, 0);
    camel_burst_controller_record_interval_counts(&ctrl, 1, 2048, 1024);
    result = camel_burst_controller_maybe_update(&ctrl, 5000);
    FCC_EXPECT_EQ("Should return 1", result, 1);
    printf("  Burst after byte-count update: %u bytes (expected 2048)\n", (unsigned)ctrl.current_burst_bytes);
    FCC_EXPECT_EQ("Byte-count loss decreases burst", ctrl.current_burst_bytes, 2048);
    
    printf("\n=== Burst Controller Summary: %s ===\n\n",
           failed == 0 ? "ALL PASSED" : "SOME FAILED");
    
    return failed;
}
