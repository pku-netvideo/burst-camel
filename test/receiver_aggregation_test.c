#include "test_framework.h"
#include "receiver.h"
#include "common.h"

#include <string.h>

typedef struct
{
	uint8_t payload[512];
	int payload_size;
	int calls;
} receiver_feedback_capture_t;

static void capture_feedback(void* handler, const uint8_t* payload, int payload_size)
{
	receiver_feedback_capture_t* capture = (receiver_feedback_capture_t*)handler;

	capture->calls++;
	capture->payload_size = payload_size;
	if (payload_size > (int)sizeof(capture->payload))
		payload_size = (int)sizeof(capture->payload);
	memcpy(capture->payload, payload, (size_t)payload_size);
}

int test_receiver_aggregation(void)
{
	int failed = 0;
	receiver_feedback_capture_t capture;
	camel_receiver_t* receiver;
	camel_bin_stream_t strm;
	camel_feedback_msg_t msg;

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, capture_feedback);

	camel_receiver_on_received_frame_info(receiver, 1, 1000);
	camel_receiver_on_received_frame_info(receiver, 1, 900);
	camel_receiver_on_received_frame_info(receiver, 1, 800);
	FCC_EXPECT_EQ("no feedback before frame changes", capture.calls, 0);

	camel_receiver_on_received_frame_info(receiver, 2, 700);
	FCC_EXPECT_EQ("feedback emitted on frame transition", capture.calls, 1);
	FCC_EXPECT_TRUE("feedback payload non-empty", capture.payload_size > 0);

	camel_bin_stream_init(&strm);
	if ((size_t)capture.payload_size > strm.size)
		return 1;
	memcpy(strm.data, capture.payload, (size_t)capture.payload_size);
	strm.used = (size_t)capture.payload_size;
	camel_bin_stream_rewind(&strm, 0);
	memset(&msg, 0, sizeof(msg));
	camel_feedback_msg_decode(&strm, &msg);

	FCC_EXPECT_EQ("feedback frame id", msg.frame_id, 1);
	FCC_EXPECT_EQ("feedback frame size", (uint64_t)msg.frame_size, 2700ULL);
	FCC_EXPECT_EQ("feedback packet count", msg.packet_count, 3);
	FCC_EXPECT_EQ("feedback first packet size", msg.first_packet_size, 1000);
	FCC_EXPECT_EQ("feedback interval count", msg.interval_count, 2);
	FCC_EXPECT_EQ("feedback interval 0 bytes", msg.interval_received_bytes[0], 2048);
	FCC_EXPECT_EQ("feedback interval 1 bytes", msg.interval_received_bytes[1], 652);
	FCC_EXPECT_TRUE("feedback timestamps ordered", msg.last_ts >= msg.first_ts);

	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, capture_feedback);

	camel_receiver_on_received_frame_info(receiver, 65536, 500);
	camel_receiver_on_received_frame_info(receiver, 65536, 400);
	FCC_EXPECT_EQ("no feedback for frame 65536 yet", capture.calls, 0);

	camel_receiver_on_received_frame_info(receiver, 65537, 300);
	FCC_EXPECT_EQ("feedback emitted on frame 65537 arrival", capture.calls, 1);

	camel_bin_stream_init(&strm);
	if ((size_t)capture.payload_size <= strm.size) {
		memcpy(strm.data, capture.payload, (size_t)capture.payload_size);
		strm.used = (size_t)capture.payload_size;
		camel_bin_stream_rewind(&strm, 0);
		memset(&msg, 0, sizeof(msg));
		camel_feedback_msg_decode(&strm, &msg);
		FCC_EXPECT_EQ("large frame_id preserved in feedback", msg.frame_id, 65536U);
		FCC_EXPECT_EQ("large frame total size correct", (uint64_t)msg.frame_size, 900ULL);
	}
	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, capture_feedback);

	for (int i = 0; i < 66; i++)
		camel_receiver_on_received_frame_info(receiver, 100, 2048);
	camel_receiver_on_received_frame_info(receiver, 101, 100);
	FCC_EXPECT_EQ("oversized frame triggers feedback", capture.calls, 1);

	camel_bin_stream_init(&strm);
	if ((size_t)capture.payload_size <= strm.size) {
		memcpy(strm.data, capture.payload, (size_t)capture.payload_size);
		strm.used = (size_t)capture.payload_size;
		camel_bin_stream_rewind(&strm, 0);
		memset(&msg, 0, sizeof(msg));
		camel_feedback_msg_decode(&strm, &msg);
		FCC_EXPECT_EQ("oversized frame interval_count does not exceed 64", msg.interval_count, 64U);
		FCC_EXPECT_EQ("oversized frame interval 63 has at most 2048 bytes", msg.interval_received_bytes[63], 2048U);
	}
	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);
	return failed;
}

