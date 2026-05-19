# API Reference

All public APIs are exported via `src/include/camel.h` (the umbrella header).

## Bandwidth Estimator

- `camel_estimator_init`
- `camel_estimator_add_sample`
- `camel_estimator_get_bandwidth`
- `camel_estimator_get_bdp_bytes`

## Congestion Detector

- `camel_congestion_detector_init`
- `camel_congestion_detector_add_sample`

## Burst Controller

- `camel_burst_controller_init`
- `camel_burst_controller_record_interval(_counts)`
- `camel_burst_controller_maybe_update`

## Receiver (Packet-Level)

The receiver emits feedback payloads via callbacks. Your application is responsible for delivering those payloads to the sender over a feedback channel.

- `camel_receiver_create`
- `camel_receiver_on_packet_received`
- `camel_receiver_on_packet_received_with_offset`
- `camel_receiver_destroy`

Use `camel_receiver_on_packet_received_with_offset` when receiver interval feedback should drive burst control. Third-party compact interval feedback should remain untrusted unless it preserves original frame offsets.

## Sender (Packet-Level)

The sender consumes feedback payloads produced by the receiver (typically received over the network) via:

- `camel_sender_create`
- `camel_sender_set_config`
- `camel_sender_set_warning_cb`
- `camel_sender_end_group`
- `camel_sender_on_packet_sent`
- `camel_sender_on_packet_ack`
- `camel_sender_on_cumulative_ack`
- `camel_sender_on_ack_ranges`
- `camel_sender_on_group_feedback`
- `camel_sender_heartbeat` (Manual-time mode)
- `camel_sender_start` / `camel_sender_stop` (Real-time mode)
- `camel_sender_get_burst_bytes`
- `camel_sender_in_fallback`
- `camel_sender_set_fallback_enabled`
- `camel_sender_pacer_insert_packet`
- `camel_sender_pacer_try_transmit`
- `camel_sender_get_pacer_stats`

## Built-in Pacer (Lower-Level)

The sender wraps a built-in pacer. If you integrate the pacer directly, use:

- `camel_pacer_init` / `camel_pacer_destroy`
- `camel_pacer_set_estimate_bitrate`
- `camel_pacer_set_bitrate_limits`
- `camel_pacer_set_congestion_window`
- `camel_pacer_set_outstanding`
- `camel_pacer_set_max_burst_bytes`
- `camel_pacer_insert_packet`
- `camel_pacer_try_transmit`

## Binary Stream Utilities (Feedback Encoding)

- `camel_bin_stream_init` / `camel_bin_stream_destroy`
- `camel_bin_stream_rewind`
- `camel_mach_uint16_write` / `camel_mach_uint16_read`
- `camel_mach_uint32_write` / `camel_mach_uint32_read`
- `camel_mach_uint64_write` / `camel_mach_uint64_read`

## Feedback Codec

- `camel_group_feedback_msg_encode`
- `camel_group_feedback_msg_decode`
- `camel_feedback_msg_encode`
- `camel_feedback_msg_decode`
- `camel_transport_feedback_msg_encode`
- `camel_transport_feedback_msg_decode`
- `camel_cumack_msg_encode`
- `camel_cumack_msg_decode`
- `camel_ack_ranges_msg_encode`
- `camel_ack_ranges_msg_decode`
