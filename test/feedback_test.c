#include "test_framework.h"
#include "common.h"

#include <string.h>

int test_feedback_codec(void)
{
	int failed = 0;
	camel_bin_stream_t strm;
	camel_feedback_msg_t input;
	camel_feedback_msg_t output;

	camel_bin_stream_init(&strm);
	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));
	input.frame_id = 123;
	input.frame_size = 4567;
	input.packet_count = 5;
	input.first_packet_size = 1200;
	input.first_transport_seq = 77;
	input.last_transport_seq = 81;
	input.first_ts = 1000000;
	input.last_ts = 1009000;
	input.interval_count = 3;
	input.interval_received_bytes[0] = 2048;
	input.interval_received_bytes[1] = 2048;
	input.interval_received_bytes[2] = 471;

	camel_feedback_msg_encode(&strm, &input);
	camel_bin_stream_rewind(&strm, 0);
	camel_feedback_msg_decode(&strm, &output);

	FCC_EXPECT_EQ("frame id round trip", output.frame_id, input.frame_id);
	FCC_EXPECT_EQ("frame size round trip", (uint64_t)output.frame_size, (uint64_t)input.frame_size);
	FCC_EXPECT_EQ("packet count round trip", output.packet_count, input.packet_count);
	FCC_EXPECT_EQ("first packet size round trip", output.first_packet_size, input.first_packet_size);
	FCC_EXPECT_EQ("first seq round trip", output.first_transport_seq, input.first_transport_seq);
	FCC_EXPECT_EQ("last seq round trip", output.last_transport_seq, input.last_transport_seq);
	FCC_EXPECT_EQ("first ts round trip", output.first_ts, input.first_ts);
	FCC_EXPECT_EQ("last ts round trip", output.last_ts, input.last_ts);
	FCC_EXPECT_EQ("interval count round trip", output.interval_count, input.interval_count);
	FCC_EXPECT_EQ("interval 0 round trip",
		output.interval_received_bytes[0], input.interval_received_bytes[0]);
	FCC_EXPECT_EQ("interval 1 round trip",
		output.interval_received_bytes[1], input.interval_received_bytes[1]);
	FCC_EXPECT_EQ("interval 2 round trip",
		output.interval_received_bytes[2], input.interval_received_bytes[2]);

	camel_bin_stream_destroy(&strm);
	return failed;
}

