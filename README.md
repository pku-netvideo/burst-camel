# CAMEL-CC

**CAMEL-CC** (Combined Aggregate and Media-level Estimation for Congestion Control) is a congestion control algorithm implementation for real-time media.

## Overview

CAMEL-CC implements a novel congestion control algorithm that:
- Performs aggregate bandwidth estimation from packet arrivals
- Detects congestion using dD/dinflight linear regression
- Dynamically adjusts burst length based on interval loss patterns
- Provides smooth rate adaptation for real-time video transmission

## Features

- **Bandwidth Estimator**: Aggregate bandwidth estimation with BDP computation
- **Congestion Detector**: dD/dinflight based congestion detection with gamma window scaling
- **Burst Controller**: Dynamic burst length adjustment with fallback mode
- **Sender Integration**: Complete sender-side congestion control
- **Receiver Integration**: Packet aggregation and feedback generation
- **Network Simulator**: Built-in network simulation for testing

## Building

```bash
# Build the library and tests
make

# Run all tests
make test

# Clean build artifacts
make clean
```

## Quick Start

### Example Usage

```c
#include "src/include/camel.h"

static void on_bitrate_changed(void* user, uint32_t bitrate, uint8_t fraction_loss, uint32_t rtt) {
  (void)user; (void)fraction_loss; (void)rtt;
  /* Apply bitrate to your encoder. */
}

static void pace_send(void* user, uint32_t packet_id, int retrans, size_t size, int padding) {
  (void)user; (void)packet_id; (void)retrans; (void)size; (void)padding;
  /* Send one packet on the wire. */
}

static void on_group_feedback(void* user, const uint8_t* payload, int payload_size) {
  camel_sender_t* sender = (camel_sender_t*)user;
  camel_sender_on_group_feedback(sender, payload, payload_size);
}

static void on_packet_ack(void* user, const uint8_t* payload, int payload_size) {
  camel_sender_t* sender = (camel_sender_t*)user;
  camel_sender_on_packet_ack(sender, payload, payload_size);
}

/* Create sender/receiver. */
camel_sender_t* sender = camel_sender_create(
    user_data,
    on_bitrate_changed,
    handler,
    pace_send,
    0,
    0,
    app_predict_callback
);

camel_receiver_t* receiver = camel_receiver_create(sender, on_group_feedback, on_packet_ack);

/* Real-time mode: start internal pacing + heartbeat thread. */
camel_sender_start(sender);

/*
 * For each outgoing packet:
 * - assign a monotonically increasing transport_seq
 * - choose a group_id and set is_group_end=1 for the last packet of that group
 */
camel_sender_on_packet_sent(sender, group_id, transport_seq, payload_size, is_group_end);

/* For each incoming packet at the receiver: */
camel_receiver_on_packet_received(receiver, group_id, transport_seq, payload_size, is_group_end);

/* Cleanup */
camel_sender_stop(sender);
camel_receiver_destroy(receiver);
camel_sender_destroy(sender);
```

## End-to-End Pipeline

This implementation is fully packet-level at the public API boundary. A typical integration uses a packet group to represent one video frame (or any application-defined aggregation unit), but the library itself only understands packets and groups.

### 1) Sender side: record each transmitted packet

For every packet you put on the wire, call:

```c
camel_sender_on_packet_sent(sender, group_id, transport_seq, payload_size, is_group_end);
```

This updates:
- packet history (for RTT and inflight tracking)
- per-group send bookkeeping (total group bytes, first packet metadata)
- inflight bytes (used for cwnd gating)

### 2) Receiver side: feed each received packet and emit feedback

For every received packet, call:

```c
camel_receiver_on_packet_received(receiver, group_id, transport_seq, payload_size, is_group_end);
```

The receiver emits two feedback streams via callbacks:
- Packet-level ACK samples (`packet_ack_cb`) as `camel_transport_feedback_msg_t`
- Group-level aggregate feedback (`group_feedback_cb`) as `camel_group_feedback_msg_t` when `is_group_end=1`

### 3) Sender side: consume ACK and aggregate feedback

Feed the callback payloads back into the sender:

```c
camel_sender_on_packet_ack(sender, ack_payload, ack_size);
camel_sender_on_group_feedback(sender, group_payload, group_size);
```

The sender only produces a bandwidth/congestion sample when both are available for the same group:
- First-packet RTT (from ACK) for the group’s first packet
- Aggregate group feedback (receive time span + received bytes per 2KB interval)

From these samples the sender updates:
- bandwidth estimator (EWMA bandwidth + min-delay)
- congestion detector (`gamma`)
- congestion window (`cwnd = gamma * BDP`)
- target bitrate (`target = gamma * avg_bandwidth`, unless in fallback)

Updates are pushed into the built-in pacer:
- pacing bitrate
- cwnd
- max burst bytes (from burst controller)

## Packet-Level RTT (First-Packet RTT)

The delay signal uses the RTT of the first packet of each packet group. This requires packet-level ACK feedback:

1. Call `camel_sender_on_packet_sent(...)` for every transmitted packet.
2. Call `camel_receiver_on_packet_received(...)` for every received packet. The receiver emits:
   - per-packet ACK samples via `packet_ack_cb`
   - per-group aggregate feedback via `group_feedback_cb`
3. Feed those callbacks back into the sender via:
   - `camel_sender_on_packet_ack(...)`
   - `camel_sender_on_group_feedback(...)`

### ACK requirements and batch ACK

- Batch ACK is supported: one ACK message may contain multiple `transport_seq` samples (`sample_count > 1`).
- Not every packet must be ACKed for correctness of parsing, but:
  - The first packet of each group must be ACKed, otherwise that group will never produce a delay-based sample (it will be missing first-packet RTT).
  - Inflight accounting only decreases when ACK samples are received; if you ACK too sparsely, inflight may remain artificially high and pacing/cwnd gating can become overly conservative.

### How to packetize video frames

A practical mapping is:
- One encoded video frame (byte buffer) → one packet group (`group_id = frame_index`).
- Split the frame into MTU-sized packets (e.g., 1200-byte payload).
- Set `is_group_end=1` on the last packet of that frame/group.
- Use a monotonically increasing `transport_seq` across all outgoing packets.

## Burst Length

- Read the current burst length (bytes):

```c
size_t burst_bytes = camel_sender_get_burst_bytes(sender);
```

- If you use the built-in pacer, the burst length is applied automatically. If you use your own pacer, cap each burst to `burst_bytes`.

## Pacer

The sender provides a simple built-in pacer (queue + budget + max burst + cwnd):

```c
camel_sender_pacer_insert_packet(sender, packet_id, retrans, size, padding, now_ts_ms);
camel_sender_pacer_try_transmit(sender, now_ts_ms);
```

You can query pacer parameters via:

```c
camel_pacer_stats_t stats;
camel_sender_get_pacer_stats(sender, &stats);
```

## Heartbeat / Scheduling

The sender supports two timing modes:

1. Real-time mode (online): call `camel_sender_start(sender)` and `camel_sender_stop(sender)`.
2. Manual-time mode (simulation/replay): do not start the thread; instead drive:

```c
camel_sender_pacer_try_transmit(sender, now_ms);
camel_sender_heartbeat(sender, now_ms);
```

## Fallback

When the burst controller enters fallback mode, the sender switches to the fallback controller output.

You can enable/disable fallback:

```c
camel_sender_set_fallback_enabled(sender, 1);
```

If fallback is disabled but triggered, the sender enters a fatal state and stops producing target bitrate updates.

See `doc/API.md` for complete API list.

## Architecture

```
src/
├── bandwidth_estimator.c/h   # Bandwidth estimation
├── congestion_detector.c/h   # Congestion detection
├── burst_controller.c/h      # Burst length control
├── sender.c/h                # Sender-side control and pacing
├── receiver.c/h              # Receiver-side aggregation and feedback
├── common.c/h                # Codecs and common definitions
└── include/
    └── camel.h               # Public umbrella header
```

## Testing

```bash
# Run all unit tests
make test

# Run specific test
./test/fcc_unittest
```

See `doc/TESTING.md` for detailed testing documentation.

## Algorithm

CAMEL-CC implements three key algorithms:

1. **Bandwidth Estimation**: `B = sum(S_i) / (t_recv_n - t_recv_1)`
2. **Congestion Detection**: `S(D, inflight) = dD / dinflight`
3. **Burst Control**: `M_t = M_{t-1} ± 2KB` (based on loss rate)

See `doc/ALGORITHM.md` for detailed algorithm documentation.

## Documentation

- [Algorithm Details](doc/ALGORITHM.md)
- [API Reference](doc/API.md)
- [Implementation Notes](doc/IMPLEMENTATION.md)
- [Testing Guide](doc/TESTING.md)
- [Simulation Guide](doc/SIMULATION.md)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Citation

If you use CAMEL-CC in your research, please cite:

```
CAMEL-CC: Frame-level Bandwidth Estimation and Congestion Control for Video Transmission
```
