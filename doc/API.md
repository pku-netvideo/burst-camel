# API Reference

All public APIs are exported via [camel.h](file:///home/pic/documents/burst-camel/src/include/camel.h).

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

- `camel_receiver_create`
- `camel_receiver_on_packet_received`
- `camel_receiver_destroy`

## Sender (Packet-Level)

- `camel_sender_create`
- `camel_sender_on_packet_sent`
- `camel_sender_on_packet_ack`
- `camel_sender_on_group_feedback`
- `camel_sender_heartbeat` (Manual-time mode)
- `camel_sender_start` / `camel_sender_stop` (Real-time mode)
- `camel_sender_get_burst_bytes`
- `camel_sender_in_fallback`
- `camel_sender_set_fallback_enabled`
- `camel_sender_pacer_insert_packet`
- `camel_sender_pacer_try_transmit`
- `camel_sender_get_pacer_stats`

## Feedback Codec

- `camel_group_feedback_msg_encode`
- `camel_group_feedback_msg_decode`
- `camel_transport_feedback_msg_encode`
- `camel_transport_feedback_msg_decode`
