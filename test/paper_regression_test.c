#include "test_framework.h"
#include "sender.h"
#include "receiver.h"
#include "common.h"
#include "pacer.h"
#include "packet_history.h"
#include "bandwidth_estimator.h"
#include "congestion_detector.h"
#include "burst_controller.h"
#include "gcc_fallback.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define TEST_CAMEL_MAX_GROUP_NUM 1024

typedef struct
{
	uint32_t group_id;
	uint32_t sent_size_bytes;
	uint32_t sent_packet_count;
	uint32_t first_packet_size;
	uint16_t first_transport_seq;
	uint8_t ended;
	uint64_t ended_ts_us;
	uint64_t first_send_ts_us;
	uint64_t last_send_ts_us;
	uint32_t acked_bytes;
	uint32_t acked_bytes_excluding_first;
	uint32_t acked_packets;
	uint32_t acked_packets_with_recv_ts;
	uint64_t first_acked_ts_us;
	uint64_t last_acked_ts_us;
	uint8_t recv_ts_domain;
	uint64_t recv_ts_min_us;
	uint64_t recv_ts_max_us;
	uint32_t acked_interval_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
	uint64_t sample_inflight_bytes;
	int valid;
} test_group_send_info_t;

typedef struct
{
	uint32_t group_id;
	uint32_t recv_size_bytes;
	uint32_t recv_packet_count;
	uint64_t first_recv_ts_us;
	uint64_t last_recv_ts_us;
	uint32_t interval_count;
	uint32_t interval_received_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
	int valid;
} test_group_feedback_info_t;

struct camel_sender_t
{
	void* trigger;
	camel_bitrate_changed_func trigger_cb;
	void* handler;
	camel_pace_send_func send_cb;
	camel_app_layer_predict_func app_func;

	uint32_t last_bitrate_bps;
	int64_t notify_ts_ms;

	uint32_t max_bitrate;
	uint32_t min_bitrate;

	camel_bin_stream_t strm;
	camel_packet_history_t packet_history;

	test_group_send_info_t groups[TEST_CAMEL_MAX_GROUP_NUM];
	test_group_feedback_info_t group_feedback[TEST_CAMEL_MAX_GROUP_NUM];
	uint64_t group_first_rtt_us[TEST_CAMEL_MAX_GROUP_NUM];
	uint8_t group_first_rtt_valid[TEST_CAMEL_MAX_GROUP_NUM];
	uint16_t last_cumack_seq;
	uint8_t last_cumack_valid;

	camel_pacer_t* pacer;

	camel_estimator_t estimator;
	camel_congestion_detector_t detector;
	camel_burst_controller_t burst_ctrl;

	size_t congestion_window_bytes;
	uint32_t camel_target_bitrate_bps;
	double camel_gamma;
	double camel_last_slope_us_per_byte;

	uint64_t inflight_bytes;

	camel_gcc_fallback_t gcc_fallback;
	uint32_t gcc_target_bitrate_bps;
	int fallback_enabled;
	int in_fallback;

	int fatal_error;

	pthread_t thread;
	int thread_running;
	int stop_thread;
	pthread_mutex_t mu;
	int mu_inited;

	camel_sender_config_t cfg;
	camel_sender_warning_cb warning_cb;
	void* warning_ctx;
};

typedef struct
{
	uint32_t bitrate;
	int bitrate_calls;
	int app_calls;
	uint8_t group_payload[4096];
	int group_payload_size;
} paper_regression_capture_t;

static void paper_bitrate_changed(void* trigger, uint32_t bitrate, uint8_t fraction_loss, uint32_t rtt)
{
	paper_regression_capture_t* c = (paper_regression_capture_t*)trigger;
	(void)fraction_loss;
	(void)rtt;
	c->bitrate = bitrate;
	c->bitrate_calls++;
}

static void paper_pace_send(void* handler, uint32_t packet_id, int retrans, size_t size, int padding)
{
	(void)handler;
	(void)packet_id;
	(void)retrans;
	(void)size;
	(void)padding;
}

static void paper_app_predict(void* trigger, int32_t target_rate, double* ssim, double* pssim,
	double* size_u, double* size_sigma2)
{
	paper_regression_capture_t* c = (paper_regression_capture_t*)trigger;
	(void)target_rate;
	c->app_calls++;
	if (ssim)
		*ssim = 1.0;
	if (pssim)
		*pssim = 1.0;
	if (size_u)
		*size_u = 1.0;
	if (size_sigma2)
		*size_sigma2 = 1.0;
}

static void paper_group_feedback_cb(void* handler, const uint8_t* payload, int payload_size)
{
	paper_regression_capture_t* c = (paper_regression_capture_t*)handler;
	if (payload_size > (int)sizeof(c->group_payload))
		payload_size = (int)sizeof(c->group_payload);
	memcpy(c->group_payload, payload, (size_t)payload_size);
	c->group_payload_size = payload_size;
}

static void paper_ack_cb(void* handler, const uint8_t* payload, int payload_size)
{
	(void)handler;
	(void)payload;
	(void)payload_size;
}

static void encode_and_deliver_packet_acks(camel_sender_t* sender, camel_bin_stream_t* strm)
{
	camel_transport_feedback_msg_t ack;
	memset(&ack, 0, sizeof(ack));
	ack.sample_count = 3;
	ack.samples[0].transport_seq = 1000;
	ack.samples[0].recv_ts_us = 1000000;
	ack.samples[1].transport_seq = 1001;
	ack.samples[1].recv_ts_us = 1001000;
	ack.samples[2].transport_seq = 1002;
	ack.samples[2].recv_ts_us = 1002000;
	camel_transport_feedback_msg_encode(strm, &ack);
	camel_sender_on_packet_ack(sender, strm->data, (int)strm->used);
}

static void encode_and_deliver_group_feedback(camel_sender_t* sender, camel_bin_stream_t* strm)
{
	camel_group_feedback_msg_t gfb;
	memset(&gfb, 0, sizeof(gfb));
	gfb.group_id = 77;
	gfb.group_size_bytes = 3000;
	gfb.packet_count = 3;
	gfb.first_packet_size = 1000;
	gfb.first_recv_ts_us = 1000000;
	gfb.last_recv_ts_us = 1002000;
	gfb.interval_count = 2;
	gfb.interval_received_bytes[0] = 2048;
	gfb.interval_received_bytes[1] = 952;
	camel_group_feedback_msg_encode(strm, &gfb);
	camel_sender_on_group_feedback(sender, strm->data, (int)strm->used);
}

static int test_inflight_delay_detector_records_frame_sample(void)
{
	int failed = 0;
	paper_regression_capture_t capture;
	camel_sender_t* sender;
	camel_bin_stream_t strm;

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, paper_bitrate_changed, NULL, paper_pace_send, 0, 0, paper_app_predict);
	camel_bin_stream_init(&strm);

	camel_sender_on_packet_sent(sender, 77, 1000, 1000, 0);
	camel_sender_on_packet_sent(sender, 77, 1001, 1000, 0);
	camel_sender_on_packet_sent(sender, 77, 1002, 1000, 1);
	encode_and_deliver_packet_acks(sender, &strm);
	encode_and_deliver_group_feedback(sender, &strm);

	FCC_EXPECT_EQ("frame bandwidth sample is produced", sender->estimator.valid_samples, 1U);
	FCC_EXPECT_GT("frame sample is also recorded by inflight-delay detector", sender->detector.count, 0U);

	camel_bin_stream_destroy(&strm);
	camel_sender_destroy(sender);
	return failed;
}

static int test_receiver_preserves_interval_holes_for_loss(void)
{
	int failed = 0;
	paper_regression_capture_t capture;
	camel_receiver_t* receiver;
	camel_bin_stream_t strm;
	camel_group_feedback_msg_t msg;

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, paper_group_feedback_cb, paper_ack_cb);
	camel_bin_stream_init(&strm);

	camel_receiver_on_packet_received_with_offset(receiver, 55, 2001, 2048, 2048, 6144, 0);
	camel_receiver_on_packet_received_with_offset(receiver, 55, 2002, 2048, 4096, 6144, 1);

	memcpy(strm.data, capture.group_payload, (size_t)capture.group_payload_size);
	strm.used = (size_t)capture.group_payload_size;
	camel_bin_stream_rewind(&strm, 0);
	camel_group_feedback_msg_decode(&strm, &msg);

	FCC_EXPECT_EQ("feedback reports original frame size including lost interval", msg.group_size_bytes, 6144U);
	FCC_EXPECT_EQ("feedback includes empty leading interval", msg.interval_count, 3U);
	FCC_EXPECT_EQ("lost leading interval reports zero received bytes", msg.interval_received_bytes[0], 0U);
	FCC_EXPECT_EQ("second interval preserves original offset", msg.interval_received_bytes[1], 2048U);
	FCC_EXPECT_EQ("third interval preserves original offset", msg.interval_received_bytes[2], 2048U);

	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);
	return failed;
}

static int test_receiver_loss_feedback_decreases_sender_burst(void)
{
	int failed = 0;
	paper_regression_capture_t capture;
	camel_receiver_t* receiver;
	camel_sender_t* sender;
	size_t initial_burst;

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, paper_group_feedback_cb, paper_ack_cb);
	sender = camel_sender_create(&capture, paper_bitrate_changed, NULL, paper_pace_send, 0, 0, paper_app_predict);
	camel_sender_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.enable_warnings = 1;
	cfg.enable_synthetic_group_feedback = 1;
	cfg.enable_synthetic_interval_shape = 1;
	cfg.group_idle_timeout_ms = 50;
	cfg.congestion_window_by_samples = 0;
	cfg.congestion_window_value = 5000;
	cfg.min_delay_window_by_samples = 0;
	cfg.min_delay_window_value = 5000;
	cfg.trust_remote_interval_feedback = 1;
	camel_sender_set_config(sender, &cfg);

	initial_burst = camel_sender_get_burst_bytes(sender);
	camel_sender_on_packet_sent(sender, 55, 2000, 2048, 0);
	camel_sender_on_packet_sent(sender, 55, 2001, 2048, 0);
	camel_sender_on_packet_sent(sender, 55, 2002, 2048, 1);

	camel_receiver_on_packet_received_with_offset(receiver, 55, 2001, 2048, 2048, 6144, 0);
	camel_receiver_on_packet_received_with_offset(receiver, 55, 2002, 2048, 4096, 6144, 1);
	camel_sender_on_group_feedback(sender, capture.group_payload, capture.group_payload_size);

	FCC_EXPECT_LT("sender burst decreases after receiver reports interval overflow loss",
		camel_sender_get_burst_bytes(sender), initial_burst);

	camel_sender_destroy(sender);
	camel_receiver_destroy(receiver);
	return failed;
}

static int test_sender_invokes_application_prediction_callback(void)
{
	int failed = 0;
	paper_regression_capture_t capture;
	camel_sender_t* sender;
	camel_bin_stream_t strm;

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, paper_bitrate_changed, NULL, paper_pace_send, 0, 0, paper_app_predict);
	camel_bin_stream_init(&strm);

	camel_sender_on_packet_sent(sender, 77, 1000, 1000, 0);
	camel_sender_on_packet_sent(sender, 77, 1001, 1000, 0);
	camel_sender_on_packet_sent(sender, 77, 1002, 1000, 1);
	encode_and_deliver_packet_acks(sender, &strm);
	encode_and_deliver_group_feedback(sender, &strm);
	camel_sender_heartbeat(sender, 10000);

	FCC_EXPECT_GT("sender notifies bitrate target", capture.bitrate_calls, 0);
	FCC_EXPECT_GT("sender invokes application prediction callback", capture.app_calls, 0);

	camel_bin_stream_destroy(&strm);
	camel_sender_destroy(sender);
	return failed;
}

int test_paper_regressions(void)
{
	int failed = 0;
	printf("Test 1: inflight-delay detector receives frame samples\n");
	failed += test_inflight_delay_detector_records_frame_sample();
	printf("Test 2: receiver preserves frame interval holes under loss\n");
	failed += test_receiver_preserves_interval_holes_for_loss();
	printf("Test 3: receiver loss feedback reduces sender burst length\n");
	failed += test_receiver_loss_feedback_decreases_sender_burst();
	printf("Test 4: sender uses application prediction callback\n");
	failed += test_sender_invokes_application_prediction_callback();
	return failed;
}
