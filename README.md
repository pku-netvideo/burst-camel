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

## Packet-Level RTT (First-Packet RTT)

The delay signal uses the RTT of the first packet of each packet group. This requires packet-level ACK feedback:

1. Call `camel_sender_on_packet_sent(...)` for every transmitted packet.
2. Call `camel_receiver_on_packet_received(...)` for every received packet. The receiver emits:
   - per-packet ACK samples via `packet_ack_cb`
   - per-group aggregate feedback via `group_feedback_cb`
3. Feed those callbacks back into the sender via:
   - `camel_sender_on_packet_ack(...)`
   - `camel_sender_on_group_feedback(...)`

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
