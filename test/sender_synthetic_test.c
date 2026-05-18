#include "test_framework.h"
#include "bandwidth_estimator.h"
#include "congestion_detector.h"
#include "sender.h"
#include "common.h"

#include <string.h>
#include <time.h>

typedef struct
{
	uint32_t bitrate;
	int bitrate_calls;
} camel_sender_capture_t;

static uint64_t camel_test_sys_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static uint64_t camel_test_sys_ms(void)
{
	return camel_test_sys_us() / 1000ULL;
}

static void test_bitrate_changed(void* trigger, uint32_t bitrate, uint8_t fraction_loss, uint32_t rtt)
{
	camel_sender_capture_t* capture = (camel_sender_capture_t*)trigger;
	(void)fraction_loss;
	(void)rtt;
	capture->bitrate = bitrate;
	capture->bitrate_calls++;
}

static void test_pace_send(void* handler, uint32_t packet_id, int retrans, size_t size, int padding)
{
	(void)handler;
	(void)packet_id;
	(void)retrans;
	(void)size;
	(void)padding;
}

static void test_app_predict(void* trigger, int32_t target_rate, double* ssim, double* pssim,
	double* size_u, double* size_sigma2)
{
	(void)trigger;
	*ssim = 1.0;
	*pssim = 1.0;
	*size_u = (double)target_rate / 30.0 / 8.0;
	*size_sigma2 = 1.0;
}

int test_sender_synthetic(void)
{
	int failed = 0;
	camel_estimator_t est;
	camel_congestion_detector_t det;
	camel_frame_sample_t sample;
	camel_congestion_result_t result;
	uint64_t target_bitrate_bps;
	uint64_t cwnd_bytes;
	camel_sender_capture_t capture;
	camel_sender_t* sender;
	camel_bin_stream_t strm;
	camel_feedback_msg_t msg;

	camel_estimator_init(&est, 1.0);
	camel_congestion_detector_init(&det, 3, 0.5, 0.2);

	memset(&sample, 0, sizeof(sample));
	sample.frame_id = 1;
	sample.packet_count = 3;
	sample.bytes_excluding_first = 1000;
	sample.first_recv_ts_us = 0;
	sample.last_recv_ts_us = 4000;
	sample.delay_us = 50000;
	FCC_EXPECT_EQ("synthetic sample 1 accepted", camel_estimator_add_sample(&est, &sample), 0);
	(void)camel_congestion_detector_add_sample(&det, 10 * 1024, 50000);

	sample.frame_id = 2;
	sample.first_recv_ts_us = 10000;
	sample.last_recv_ts_us = 14000;
	sample.delay_us = 50000;
	FCC_EXPECT_EQ("synthetic sample 2 accepted", camel_estimator_add_sample(&est, &sample), 0);
	(void)camel_congestion_detector_add_sample(&det, 20 * 1024, 50000);

	sample.frame_id = 3;
	sample.first_recv_ts_us = 20000;
	sample.last_recv_ts_us = 24000;
	sample.delay_us = 50000;
	FCC_EXPECT_EQ("synthetic sample 3 accepted", camel_estimator_add_sample(&est, &sample), 0);
	result = camel_congestion_detector_add_sample(&det, 30 * 1024, 50000);

	target_bitrate_bps = (uint64_t)((double)camel_estimator_get_bandwidth(&est) * result.gamma + 0.5);
	cwnd_bytes = (uint64_t)((double)camel_estimator_get_bdp_bytes(&est) * result.gamma + 0.5);
	FCC_EXPECT_EQ("stable target is 2 Mbps", target_bitrate_bps, 2000000ULL);
	FCC_EXPECT_EQ("stable cwnd is BDP", cwnd_bytes, 12500ULL);

	result = camel_congestion_detector_add_sample(&det, 40 * 1024, 70000);
	result = camel_congestion_detector_add_sample(&det, 50 * 1024, 95000);
	target_bitrate_bps = (uint64_t)((double)camel_estimator_get_bandwidth(&est) * result.gamma + 0.5);
	FCC_EXPECT_TRUE("congestion reduces gamma", result.gamma < 1.0);
	FCC_EXPECT_TRUE("congestion reduces target", target_bitrate_bps < 2000000ULL);

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, test_bitrate_changed, NULL, test_pace_send, 0, 0, test_app_predict);
	camel_sender_send_frame(sender, 10, 3000);

	FCC_EXPECT_EQ("inflight increases after send_frame", sender->inflight_bytes, 3000ULL);

	camel_bin_stream_init(&strm);
	memset(&msg, 0, sizeof(msg));
	msg.frame_id = 10;
	msg.frame_size = 3000;
	msg.packet_count = 3;
	msg.first_packet_size = 1000;
	msg.first_ts = camel_test_sys_us() + 1000;
	msg.last_ts = msg.first_ts + 4000;
	camel_feedback_msg_encode(&strm, &msg);
	camel_sender_on_feedback(sender, strm.data, (int)strm.used);

	FCC_EXPECT_EQ("inflight decreases after feedback", sender->inflight_bytes, 0ULL);
	FCC_EXPECT_TRUE("sender estimator received sample", sender->estimator.valid_samples == 1);
	FCC_EXPECT_EQ("sender camel target is 4 Mbps",
		(uint64_t)sender->camel_target_bitrate_bps, 4000000ULL);
	FCC_EXPECT_TRUE("sender min delay recorded (RTT > 0)", sender->estimator.min_delay_us > 0);
	FCC_EXPECT_DOUBLE_EQ("sender camel gamma stored", sender->camel_gamma, 1.0, 0.000001);

	camel_sender_heartbeat(sender, (int64_t)camel_test_sys_ms() + 1000);
	FCC_EXPECT_EQ("heartbeat notifies camel target", capture.bitrate, 4000000U);
	FCC_EXPECT_EQ("heartbeat notifies once", (uint64_t)capture.bitrate_calls, 1ULL);

	camel_sender_heartbeat(sender, (int64_t)camel_test_sys_ms() + 1050);
	FCC_EXPECT_EQ("heartbeat suppresses early duplicate",
		(uint64_t)capture.bitrate_calls, 1ULL);

	camel_bin_stream_destroy(&strm);
	camel_sender_destroy(sender);

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, test_bitrate_changed, NULL, test_pace_send, 0, 0, test_app_predict);
	{
		size_t initial_burst = sender->burst_ctrl.current_burst_bytes;
		camel_sender_send_frame(sender, 20, 5000);

		camel_bin_stream_init(&strm);
		memset(&msg, 0, sizeof(msg));
		msg.frame_id = 20;
		msg.frame_size = 3000;
		msg.packet_count = 3;
		msg.first_packet_size = 1000;
		msg.first_ts = 0;
		msg.last_ts = 4000;
		msg.interval_count = 3;
		msg.interval_received_bytes[0] = 2048;
		msg.interval_received_bytes[1] = 952;
		msg.interval_received_bytes[2] = 0;
		camel_feedback_msg_encode(&strm, &msg);
		camel_sender_on_feedback(sender, strm.data, (int)strm.used);

		FCC_EXPECT_TRUE("burst decreases when interval 2 lost using correct sender frame_size 5000",
			sender->burst_ctrl.current_burst_bytes < initial_burst);

		camel_bin_stream_destroy(&strm);
	}

	camel_sender_destroy(sender);

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, test_bitrate_changed, NULL, test_pace_send, 0, 0, test_app_predict);

	camel_sender_send_frame(sender, 1, 2000);
	camel_sender_send_frame(sender, 301, 3000);
	FCC_EXPECT_EQ("frame_id 1 slot not overwritten by frame_id 301 with MAX_FRAME_NUM=1024",
		sender->fit.frame_id[1 % 1024], 1U);
	FCC_EXPECT_EQ("frame_id 301 stored at its own slot",
		sender->fit.frame_id[301 % 1024], 301U);
	FCC_EXPECT_EQ("frame 1 size recorded correctly", sender->fit.frame_size[1 % 1024], (size_t)2000);
	FCC_EXPECT_EQ("frame 301 size recorded correctly", sender->fit.frame_size[301 % 1024], (size_t)3000);

	camel_sender_destroy(sender);

	return failed;
}

