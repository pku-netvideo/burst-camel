#include "test_framework.h"
#include "common.h"

#include <string.h>

int test_feedback_codec(void)
{
	int failed = 0;
	camel_bin_stream_t strm;
	camel_group_feedback_msg_t input;
	camel_group_feedback_msg_t output;
	camel_transport_feedback_msg_t ack_in;
	camel_transport_feedback_msg_t ack_out;

	camel_bin_stream_init(&strm);
	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));
	memset(&ack_in, 0, sizeof(ack_in));
	memset(&ack_out, 0, sizeof(ack_out));

	input.group_id = 123;
	input.group_size_bytes = 4567;
	input.packet_count = 5;
	input.first_packet_size = 1200;
	input.first_recv_ts_us = 1000000;
	input.last_recv_ts_us = 1009000;
	input.interval_count = 3;
	input.interval_received_bytes[0] = 2048;
	input.interval_received_bytes[1] = 2048;
	input.interval_received_bytes[2] = 471;

	camel_group_feedback_msg_encode(&strm, &input);
	camel_bin_stream_rewind(&strm, 0);
	camel_group_feedback_msg_decode(&strm, &output);

	FCC_EXPECT_EQ("group id round trip", output.group_id, input.group_id);
	FCC_EXPECT_EQ("group size round trip", (uint64_t)output.group_size_bytes, (uint64_t)input.group_size_bytes);
	FCC_EXPECT_EQ("packet count round trip", output.packet_count, input.packet_count);
	FCC_EXPECT_EQ("first packet size round trip", output.first_packet_size, input.first_packet_size);
	FCC_EXPECT_EQ("first recv ts round trip", output.first_recv_ts_us, input.first_recv_ts_us);
	FCC_EXPECT_EQ("last recv ts round trip", output.last_recv_ts_us, input.last_recv_ts_us);
	FCC_EXPECT_EQ("interval count round trip", output.interval_count, input.interval_count);
	FCC_EXPECT_EQ("interval 0 round trip",
		output.interval_received_bytes[0], input.interval_received_bytes[0]);
	FCC_EXPECT_EQ("interval 1 round trip",
		output.interval_received_bytes[1], input.interval_received_bytes[1]);
	FCC_EXPECT_EQ("interval 2 round trip",
		output.interval_received_bytes[2], input.interval_received_bytes[2]);

	camel_bin_stream_rewind(&strm, 1);
	ack_in.sample_count = 2;
	ack_in.samples[0].transport_seq = 100;
	ack_in.samples[0].recv_ts_us = 1111111;
	ack_in.samples[1].transport_seq = 101;
	ack_in.samples[1].recv_ts_us = 2222222;
	camel_transport_feedback_msg_encode(&strm, &ack_in);
	camel_bin_stream_rewind(&strm, 0);
	camel_transport_feedback_msg_decode(&strm, &ack_out);
	FCC_EXPECT_EQ("ack sample count round trip", (uint64_t)ack_out.sample_count, (uint64_t)ack_in.sample_count);
	FCC_EXPECT_EQ("ack seq 0 round trip", ack_out.samples[0].transport_seq, ack_in.samples[0].transport_seq);
	FCC_EXPECT_EQ("ack ts 0 round trip", ack_out.samples[0].recv_ts_us, ack_in.samples[0].recv_ts_us);
	FCC_EXPECT_EQ("ack seq 1 round trip", ack_out.samples[1].transport_seq, ack_in.samples[1].transport_seq);
	FCC_EXPECT_EQ("ack ts 1 round trip", ack_out.samples[1].recv_ts_us, ack_in.samples[1].recv_ts_us);

	camel_bin_stream_destroy(&strm);
	return failed;
}
