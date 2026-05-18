# CAMEL-CC

**CAMEL-CC** (Combined Aggregate and Media-level Estimation for Congestion Control) is a frame-level bandwidth estimation and congestion control algorithm for video transmission.

## Overview

CAMEL-CC implements a novel congestion control algorithm that:
- Performs frame-level bandwidth estimation
- Detects congestion using dD/dinflight linear regression
- Dynamically adjusts burst length based on interval loss patterns
- Provides smooth rate adaptation for real-time video transmission

## Features

- **Bandwidth Estimator**: Frame-level bandwidth calculation with BDP computation
- **Congestion Detector**: dD/dinflight based congestion detection with gamma window scaling
- **Burst Controller**: Dynamic burst length adjustment with fallback mode
- **Sender Integration**: Complete sender-side congestion control
- **Receiver Integration**: Frame aggregation and feedback generation
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
#include "include/camel.h"

// Initialize sender
camel_sender_t* sender = camel_sender_create(
    user_data,
    send_packet_callback,
    bitrate_changed_callback
);

// Send frame
camel_sender_send_frame(sender, frame_id, frame_size);

// Process feedback
camel_sender_on_feedback(sender, feedback_data, feedback_size);

// Cleanup
camel_sender_destroy(sender);
```

See `example/` directory for more examples.

## Architecture

```
src/
├── bandwidth_estimator.c/h   # Frame-level bandwidth estimation
├── congestion_detector.c/h   # Congestion detection
├── burst_controller.c/h     # Burst length control
├── sender.c/h               # Sender integration
├── receiver.c/h             # Receiver integration
├── common.c/h               # Common definitions
└── include/
    └── camel.h               # Main header
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
