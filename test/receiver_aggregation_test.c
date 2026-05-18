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
	camel_group_feedback_msg_t msg;

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, capture_feedback, NULL);

	camel_receiver_on_packet_received(receiver, 1, 10, 1000, 0);
	camel_receiver_on_packet_received(receiver, 1, 11, 900, 0);
	camel_receiver_on_packet_received(receiver, 1, 12, 800, 1);
	FCC_EXPECT_EQ("feedback emitted on group end", capture.calls, 1);
	FCC_EXPECT_TRUE("feedback payload non-empty", capture.payload_size > 0);

	camel_bin_stream_init(&strm);
	if ((size_t)capture.payload_size > strm.size)
		return 1;
	memcpy(strm.data, capture.payload, (size_t)capture.payload_size);
	strm.used = (size_t)capture.payload_size;
	camel_bin_stream_rewind(&strm, 0);
	memset(&msg, 0, sizeof(msg));
	camel_group_feedback_msg_decode(&strm, &msg);

	FCC_EXPECT_EQ("feedback group id", msg.group_id, 1);
	FCC_EXPECT_EQ("feedback group size", (uint64_t)msg.group_size_bytes, 2700ULL);
	FCC_EXPECT_EQ("feedback packet count", msg.packet_count, 3);
	FCC_EXPECT_EQ("feedback first packet size", msg.first_packet_size, 1000);
	FCC_EXPECT_EQ("feedback interval count", msg.interval_count, 2);
	FCC_EXPECT_EQ("feedback interval 0 bytes", msg.interval_received_bytes[0], 2048);
	FCC_EXPECT_EQ("feedback interval 1 bytes", msg.interval_received_bytes[1], 652);
	FCC_EXPECT_TRUE("feedback timestamps ordered", msg.last_recv_ts_us >= msg.first_recv_ts_us);

	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, capture_feedback, NULL);

	camel_receiver_on_packet_received(receiver, 65536, 100, 500, 0);
	camel_receiver_on_packet_received(receiver, 65536, 101, 400, 1);
	FCC_EXPECT_EQ("feedback emitted on group end (large id)", capture.calls, 1);

	camel_bin_stream_init(&strm);
	if ((size_t)capture.payload_size <= strm.size) {
		memcpy(strm.data, capture.payload, (size_t)capture.payload_size);
		strm.used = (size_t)capture.payload_size;
		camel_bin_stream_rewind(&strm, 0);
		memset(&msg, 0, sizeof(msg));
		camel_group_feedback_msg_decode(&strm, &msg);
		FCC_EXPECT_EQ("large group id preserved in feedback", msg.group_id, 65536U);
		FCC_EXPECT_EQ("large group total size correct", (uint64_t)msg.group_size_bytes, 900ULL);
	}
	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);

	memset(&capture, 0, sizeof(capture));
	receiver = camel_receiver_create(&capture, capture_feedback, NULL);

	for (int i = 0; i < 65; i++)
		camel_receiver_on_packet_received(receiver, 100, (uint16_t)(200 + i), 2048, 0);
	camel_receiver_on_packet_received(receiver, 100, (uint16_t)(200 + 65), 2048, 1);
	FCC_EXPECT_EQ("oversized group triggers feedback", capture.calls, 1);

	camel_bin_stream_init(&strm);
	if ((size_t)capture.payload_size <= strm.size) {
		memcpy(strm.data, capture.payload, (size_t)capture.payload_size);
		strm.used = (size_t)capture.payload_size;
		camel_bin_stream_rewind(&strm, 0);
		memset(&msg, 0, sizeof(msg));
		camel_group_feedback_msg_decode(&strm, &msg);
		FCC_EXPECT_EQ("oversized group interval_count does not exceed 64", msg.interval_count, 64U);
		FCC_EXPECT_EQ("oversized group interval 63 has at most 2048 bytes", msg.interval_received_bytes[63], 2048U);
	}
	camel_bin_stream_destroy(&strm);
	camel_receiver_destroy(receiver);
	return failed;
}
