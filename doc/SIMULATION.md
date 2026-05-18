# Simulation

`test/network_simulator.[ch]` provides a synthetic network simulator for end-to-end replay/simulation tests:

- Configurable: capacity, base delay, loss rate, router buffer size (bytes)
- Output: per-group event statistics (received/lost/queue delay, etc.)

## Trace Hook

`camel_network_simulator_load_trace_file()` is a placeholder entry point for loading scenarios from real traces.
