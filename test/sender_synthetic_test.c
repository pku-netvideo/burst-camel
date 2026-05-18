#include "test_framework.h"
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
	camel_sender_capture_t capture;
	camel_sender_t* sender;
	camel_bin_stream_t strm;
	camel_group_feedback_msg_t gfb;

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, test_bitrate_changed, NULL, test_pace_send, 0, 0, test_app_predict);

	camel_bin_stream_init(&strm);
	camel_sender_on_packet_sent(sender, 10, 100, 1000, 0);
	camel_sender_on_packet_sent(sender, 10, 101, 1000, 0);
	camel_sender_on_packet_sent(sender, 10, 102, 1000, 1);
	{
		camel_transport_feedback_msg_t tfb;
		memset(&tfb, 0, sizeof(tfb));
		tfb.sample_count = 3;
		tfb.samples[0].transport_seq = 100;
		tfb.samples[0].recv_ts_us = camel_test_sys_us() + 1000;
		tfb.samples[1].transport_seq = 101;
		tfb.samples[1].recv_ts_us = camel_test_sys_us() + 2000;
		tfb.samples[2].transport_seq = 102;
		tfb.samples[2].recv_ts_us = camel_test_sys_us() + 3000;
		camel_transport_feedback_msg_encode(&strm, &tfb);
		camel_sender_on_packet_ack(sender, strm.data, (int)strm.used);
	}

	memset(&gfb, 0, sizeof(gfb));
	gfb.group_id = 10;
	gfb.group_size_bytes = 3000;
	gfb.packet_count = 3;
	gfb.first_packet_size = 1000;
	gfb.first_recv_ts_us = 1000000;
	gfb.last_recv_ts_us = 1004000;
	gfb.interval_count = 2;
	gfb.interval_received_bytes[0] = 2048;
	gfb.interval_received_bytes[1] = 952;
	camel_group_feedback_msg_encode(&strm, &gfb);
	camel_sender_on_group_feedback(sender, strm.data, (int)strm.used);

	camel_sender_heartbeat(sender, 1000);
	FCC_EXPECT_EQ("heartbeat notifies target", capture.bitrate, 4000000U);
	FCC_EXPECT_EQ("heartbeat notifies once", (uint64_t)capture.bitrate_calls, 1ULL);

	camel_sender_heartbeat(sender, 1050);
	FCC_EXPECT_EQ("heartbeat suppresses early duplicate", (uint64_t)capture.bitrate_calls, 1ULL);

	camel_bin_stream_destroy(&strm);
	camel_sender_destroy(sender);

	memset(&capture, 0, sizeof(capture));
	sender = camel_sender_create(&capture, test_bitrate_changed, NULL, test_pace_send, 0, 0, test_app_predict);
	{
		size_t initial_burst = camel_sender_get_burst_bytes(sender);
		camel_sender_on_packet_sent(sender, 20, 200, 1000, 0);
		camel_sender_on_packet_sent(sender, 20, 201, 2000, 0);
		camel_sender_on_packet_sent(sender, 20, 202, 2000, 1);
		camel_bin_stream_init(&strm);
		memset(&gfb, 0, sizeof(gfb));
		gfb.group_id = 20;
		gfb.group_size_bytes = 3000;
		gfb.packet_count = 3;
		gfb.first_packet_size = 1000;
		gfb.first_recv_ts_us = 0;
		gfb.last_recv_ts_us = 4000;
		gfb.interval_count = 3;
		gfb.interval_received_bytes[0] = 2048;
		gfb.interval_received_bytes[1] = 952;
		gfb.interval_received_bytes[2] = 0;
		camel_group_feedback_msg_encode(&strm, &gfb);
		camel_sender_on_group_feedback(sender, strm.data, (int)strm.used);

		FCC_EXPECT_TRUE("burst decreases when interval 2 has excess loss",
			camel_sender_get_burst_bytes(sender) < initial_burst);

		camel_bin_stream_destroy(&strm);
	}

	camel_sender_destroy(sender);

	return failed;
}
