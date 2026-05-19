# Implementation Notes

## Layout

- `src/include/`: public headers
- `src/`: module implementations
- `test/`: unit tests and simulation tests

## Goal

This repository is a standalone extraction based on an original implementation from a larger internal testbed repository.
The intent is to preserve algorithm behavior while applying only necessary dependency changes and symbol renames.

## Integration Boundary

- The public API is packet-level: you feed transmitted packets into the sender and received packets into the receiver.
- The receiver emits feedback payloads (packet ACK samples and optional group feedback) via callbacks.
- The library does not implement a feedback transport. Your application must deliver feedback payloads back to the sender (often across processes/machines).
