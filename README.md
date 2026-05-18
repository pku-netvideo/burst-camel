# burst-camel (Official CAMEL-CC Reference Implementation)

[![language](https://img.shields.io/badge/language-C-blue.svg)](./)
[![license](https://img.shields.io/badge/license-MIT-green.svg)](./LICENSE)

**burst-camel** is the **official reference implementation** of **CAMEL-CC**, a congestion control algorithm for low-latency live streaming (LLS). It includes a sender-side controller, a reference receiver for feedback generation, a simple pacer, and a small simulation/test harness.

**Paper**: *Camel: Frame-Level Bandwidth Estimation for Low-Latency Live Streaming under Video Bitrate Undershooting* (WWW ’26). DOI: https://doi.org/10.1145/3774904.3792535

## Abstract (from the paper)

> Low-latency live streaming (LLS) has emerged as a popular web application, with many platforms adopting real-time protocols such as WebRTC to minimize end-to-end latency. However, we observe a counter-intuitive phenomenon: even when the actual encoded bitrate does not fully utilize the available bandwidth, stalling events remain frequent. This insufficient bandwidth utilization arises from the intrinsic temporal variations of real-time video encoding, which cause conventional packet-level congestion control algorithms to misestimate available bandwidth. When a high-bitrate frame is suddenly produced, sending at the wrong rate can either trigger packet loss or increase queueing delay, resulting in playback stalls. To address these issues, we present Camel, a novel frame-level congestion control algorithm (CCA) tailored for LLS. Our insight is to use frame-level network feedback to capture the true network capacity, immune to the irregular sending pattern caused by encoding. Camel comprises three key modules: the Bandwidth and Delay Estimator and the Congestion Detector, which jointly determine the average sending rate, and the Bursting Length Controller, which governs the emission pattern to prevent packet loss. We evaluate Camel on both large-scale real-world deployments and controlled simulations. In the real-world platform with 250M users and 2B sessions across 150+ countries, Camel achieves up to a 70.8% increase in 1080P resolution ratio, a 14.4% increase in media bitrate, and up to a 14.1% reduction in stalling ratio. In simulations under undershooting, shallow buffers, and network jitter, Camel outperforms existing congestion control algorithms, with up to 19.8% higher bitrate, 93.0% lower stalling ratio, and 23.9% improvement in bandwidth estimation accuracy.

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

The sender is designed to work with a wide range of receivers:
- If the receiver can provide full aggregate group feedback (including per-2KB interval received bytes), CAMEL runs in its strongest mode.
- If the receiver only provides ACKs, the sender can synthesize a group sample and approximate interval loss shape from ACK coverage (degraded mode).

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

The built-in receiver emits two feedback streams via callbacks:
- Packet-level ACK samples (`packet_ack_cb`) as `camel_transport_feedback_msg_t`
- Group-level aggregate feedback (`group_feedback_cb`) as `camel_group_feedback_msg_t` when `is_group_end=1`

If you use a different receiver implementation, you may provide:
- ACK only (any of the supported ACK formats), and skip group feedback entirely
- ACK + group feedback (best)

### 3) Sender side: consume ACK and aggregate feedback

Feed the callback payloads back into the sender:

```c
camel_sender_on_packet_ack(sender, ack_payload, ack_size);
camel_sender_on_group_feedback(sender, group_payload, group_size);
```

The sender can produce a bandwidth/congestion sample in two ways:
- Strong mode (recommended): first-packet RTT + group feedback
- Degraded mode: group ended + sufficient ACK coverage (synthetic group feedback)

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

Batch ACK is supported in multiple formats:
- Sample list (`camel_transport_feedback_msg_t`): one message may ACK multiple packets (`sample_count > 1`).
- Cumulative ACK (`camel_cumack_msg_t`): ACKs all packets up to `largest_acked_seq` (within the sender’s history window).
- ACK ranges (`camel_ack_ranges_msg_t`): ACKs multiple `[start_seq, end_seq]` ranges.

ACK coverage recommendations:
- The first packet of each group should be ACKed to produce the cleanest first-packet RTT signal.
- If the first packet is not ACKed, the sender falls back to a degraded delay estimate (first ACKed packet in the group), and emits a warning by default.
- Inflight accounting only decreases when ACKs are received; if you ACK too sparsely, inflight may remain artificially high and pacing/cwnd gating can become overly conservative.

To feed non-sample-list ACK formats into the sender:

```c
camel_sender_on_cumulative_ack(sender, payload, size);
camel_sender_on_ack_ranges(sender, payload, size);
```

### How to packetize video frames

A practical mapping is:
- One encoded video frame (byte buffer) → one packet group (`group_id = frame_index`).
- Split the frame into MTU-sized packets (e.g., 1200-byte payload).
- Set `is_group_end=1` on the last packet of that frame/group.
- Use a monotonically increasing `transport_seq` across all outgoing packets.

If you cannot mark the last packet, you can end the group explicitly from the sender side:

```c
camel_sender_end_group(sender, group_id);
```

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

## Warnings (Optional)

The sender can emit warnings when it enters degraded/compatibility paths (for example: synthesizing group feedback or missing first-packet ACK).

```c
camel_sender_set_warning_cb(sender, warning_cb, warning_ctx);
```

Warnings are enabled by default and can be disabled via:

```c
camel_sender_config_t cfg = {0};
cfg.enable_warnings = 0;
cfg.enable_synthetic_group_feedback = 1;
cfg.enable_synthetic_interval_shape = 1;
cfg.congestion_window_by_samples = 0;
cfg.congestion_window_value = 5000;
cfg.min_delay_window_by_samples = 0;
cfg.min_delay_window_value = 5000;
camel_sender_set_config(sender, &cfg);
```

## Window Configuration (Congestion + RTprop)

The implementation supports two window modes for both:
- congestion detection (regression over `(inflight_bytes, delay_us)`)
- minimum delay tracking (RTprop approximation in `BDP = avg(B) * min(D)`)

Parameters (in `camel_sender_config_t`):
- `congestion_window_by_samples`:
  - `0`: time window (default)
  - `1`: sample-count window
- `congestion_window_value`:
  - when time window: milliseconds (default `5000`)
  - when sample-count: number of samples
- `min_delay_window_by_samples` / `min_delay_window_value`: same semantics as above (default `5000ms`)

## Clock Semantics

- Bandwidth reconstruction uses receiver-side timestamps carried in packet ACK samples (`recv_ts_us`), but only as a relative span `(t_last - t_first)`.
- Delay (first-packet RTT) and fallback controller delay signals use sender-local time. No cross-machine clock synchronization is required.

## Thread Model

- Real-time mode starts an internal sender thread (`camel_sender_start`) which drives pacing and heartbeat.
- The sender protects shared state with a mutex. The bitrate-change callback is invoked without holding the sender mutex.
- The pacer protects its internal queue/budget with a mutex and does not hold the pacer mutex while invoking `send_cb`.

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

If you use this codebase in academic work, please cite:

```bibtex
@inproceedings{burst-camel,
  author = {Liu, Liming and Jia, Zhidong and Jiang, Li and Zhang, Wei and Xie, Lan and Qian, Feng and Yan, Leju and Yan, Bing and Ma, Qiang and Sha, Zhou and Yang, Wei and Ban, Yixuan and Zhang, Xinggong},
  title = {Camel: Frame-Level Bandwidth Estimation for Low-Latency Live Streaming under Video Bitrate Undershooting},
  year = {2026},
  isbn = {9798400723070},
  publisher = {Association for Computing Machinery},
  address = {New York, NY, USA},
  url = {https://doi.org/10.1145/3774904.3792535},
  doi = {10.1145/3774904.3792535},
  booktitle = {Proceedings of the ACM Web Conference 2026},
  pages = {5557--5567},
  numpages = {11},
  keywords = {low-latency live streaming, congestion control, frame-level control, large-scale deployment},
  location = {United Arab Emirates},
  series = {WWW '26}
}
```
