#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed = 0;

int run_test(const char* name, int (*test_fn)(void)) {
    printf("\n=== %s ===\n", name);
    failed = 0;
    int result = test_fn();
    if (failed == 0) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s (%d assertions failed)\n", name, failed);
    }
    return result;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    int failed = 0;
    
    printf("CAMEL-CC Test Suite\n");
    printf("====================\n");
    
    failed += run_test("bandwidth_estimator", test_bandwidth_estimator);
    failed += run_test("congestion_detector", test_congestion_detector);
    failed += run_test("burst_controller", test_burst_controller);
    failed += run_test("feedback_codec", test_feedback_codec);
    failed += run_test("receiver_aggregation", test_receiver_aggregation);
    failed += run_test("sender_synthetic", test_sender_synthetic);
    failed += run_test("replay_simulation", test_fcc_replay_simulation);
    
    printf("\n====================\n");
    if (failed == 0) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("%d test(s) FAILED!\n", failed);
        return 1;
    }
}
