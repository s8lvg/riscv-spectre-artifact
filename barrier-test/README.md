# Barrier Test

Tests whether RISC-V barriers stop speculation after faults (`barrier_test`).
Also tests if speculation based on the RSB can be stopped by barriers (`barrier_test_rsb`).

# How it works
The tests sets up a fault or speculation based on the RSB, followed by a barrier, followed by a secret-dependent memory access.
If the barrier stops speculation, the secret-dependent access will not be executed speculatively, and the secret cannot be leaked.

## Build & Run

```
make
./barrier_test
./barrier_test_rsb
```

## Results

Low hit rate = barrier stops speculation. High hit rate = speculation continues past barrier.
