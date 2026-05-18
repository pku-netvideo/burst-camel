/*-
* Copyright (c) 2017-2018 Razor, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/
#ifndef __camel_common_h_
#define __camel_common_h_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMEL_FEEDBACK_MAX_INTERVALS 64
#define CAMEL_TRANSPORT_FEEDBACK_MAX_SAMPLES 128

typedef struct
{
	uint8_t*	data;
	size_t		size;
	size_t		used;
	size_t		rpos;
} camel_bin_stream_t;

int		camel_bin_stream_init(camel_bin_stream_t* strm);
void	camel_bin_stream_destroy(camel_bin_stream_t* strm);
void	camel_bin_stream_rewind(camel_bin_stream_t* strm, int reset);

void	camel_mach_uint16_write(camel_bin_stream_t* strm, uint16_t val);
void	camel_mach_uint16_read(camel_bin_stream_t* strm, uint16_t* val);
void	camel_mach_uint32_write(camel_bin_stream_t* strm, uint32_t val);
void	camel_mach_uint32_read(camel_bin_stream_t* strm, uint32_t* val);
void	camel_mach_uint64_write(camel_bin_stream_t* strm, uint64_t val);
void	camel_mach_uint64_read(camel_bin_stream_t* strm, uint64_t* val);

typedef struct
{
	uint32_t group_id;
	uint32_t group_size_bytes;
	uint32_t packet_count;
	uint32_t first_packet_size;
	uint64_t first_recv_ts_us;
	uint64_t last_recv_ts_us;
	uint32_t interval_count;
	uint32_t interval_received_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
} camel_group_feedback_msg_t;

void camel_group_feedback_msg_encode(camel_bin_stream_t* strm, camel_group_feedback_msg_t* msg);
void camel_group_feedback_msg_decode(camel_bin_stream_t* strm, camel_group_feedback_msg_t* msg);

typedef struct
{
	uint32_t frame_id;
	size_t frame_size;
	uint32_t packet_count;
	uint32_t first_packet_size;
	uint16_t first_transport_seq;
	uint16_t last_transport_seq;
	uint64_t first_ts;
	uint64_t last_ts;
	uint64_t feedback_send_ts_us;
	uint32_t interval_count;
	uint32_t interval_received_bytes[CAMEL_FEEDBACK_MAX_INTERVALS];
} camel_feedback_msg_t;

void camel_feedback_msg_encode(camel_bin_stream_t* strm, camel_feedback_msg_t* msg);
void camel_feedback_msg_decode(camel_bin_stream_t* strm, camel_feedback_msg_t* msg);

typedef struct
{
	uint16_t transport_seq;
	uint64_t recv_ts_us;
} camel_transport_feedback_sample_t;

typedef struct
{
	uint16_t sample_count;
	camel_transport_feedback_sample_t samples[CAMEL_TRANSPORT_FEEDBACK_MAX_SAMPLES];
} camel_transport_feedback_msg_t;

void camel_transport_feedback_msg_encode(camel_bin_stream_t* strm, camel_transport_feedback_msg_t* msg);
void camel_transport_feedback_msg_decode(camel_bin_stream_t* strm, camel_transport_feedback_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif
