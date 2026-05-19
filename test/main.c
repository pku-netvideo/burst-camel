#include "test_framework.h"

int test_bandwidth_estimator(void);
int test_congestion_detector(void);
int test_burst_controller(void);
int test_feedback_codec(void);
int test_receiver_aggregation(void);
int test_sender_synthetic(void);
int test_paper_regressions(void);
int test_fcc_replay_simulation(void);

int run_test(const char* name, int (*test_fn)(void)) {
    printf("\n=== %s ===\n", name);
    int failed = test_fn();
    if (failed == 0) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s (%d assertions failed)\n", name, failed);
    }
    return failed;
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
    failed += run_test("paper_regressions", test_paper_regressions);
    failed += run_test("replay_simulation", test_fcc_replay_simulation);
    /* TODO: Add remaining tests when modules are ready */
    printf("\n[Remaining tests pending implementation]\n");
    
    printf("\n====================\n");
    if (failed == 0) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("%d test(s) FAILED!\n", failed);
        return 1;
    }
}
