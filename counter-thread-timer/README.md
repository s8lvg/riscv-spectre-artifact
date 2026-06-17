# Counter-thread timer

High-resolution timing source for RISC-V that does not need the `rdcycle`
CSR (recent kernels disable it in userspace). A background thread increments
a shared, cache-line aligned counter in a tight loop; the main thread reads
it as a monotonic clock.

- `counter_timer.h`: the timer (`counter_timer_start`, `ctr_read`, `counter_timer_stop`).
- `bench.c`: measures resolution, reports ticks and CPU cycles.

## Build and run

```
make
COUNTER_CORE=1 taskset -c 0 ./bench
```

Pin the counter thread (`COUNTER_CORE`) and the main thread (`taskset`) to
different physical cores, otherwise they contend on the same core and the
counter stalls.

## What it measures

1. Calibrates cycles-per-tick from `rdcycle`.
2. Reads the counter twice back-to-back many times; the minimum nonzero
   delta is the resolution in ticks, converted to cycles via the calibration.

On the T-Head C910 this yields ~6 cycle resolution, sufficient for the
Flush+Reload and Prime+Probe channels used in the paper.

`rdcycle` is used only for calibration. On P550 with recent kernels it must
be enabled by the kernel; otherwise build with `make NO_RDCYCLE=1` to report
ticks only without replacing the Makefile's required platform flags.
