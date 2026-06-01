# RISC-V Spectre Artifact

Artifact repository accompanying the paper.

## Components

- **`spectre-poc-framework/`** — Unified framework for all 13 Spectre attack configurations (PHT, BTB, RSB, STL) across same-address/cross-address and in-place/out-of-place variants (Section 3).
- **`microarch-measurements/`** — Measurement code for speculation window and RSB depth analysis (Appendix B).
- **`barrier-test/`** — Testing framework for candidate speculation barrier instructions (Section 5.3).
- **`gadet-scanning/`** — Smatch and CodeQL configurations for RISC-V kernel gadget analysis (Section 4.1).
- **`specbuild/`** — Semantic embedding-based tool for cross-architecture mitigation diffing to identify missing RISC-V mitigations (Section 4.1).
- **`branchless-jit-dispatch/`** — Modified uBPF interpreter with branchless dispatch implementation (Section 5.2).
- **`cacheutils.h`** — Cache side-channel library including Flush+Reload, Evict+Reload, Prime+Probe with eviction set construction, and a counter-thread timer for C910 and P550 (Section 3.2). The counter-thread timer is opt-in: build with `-DCACHEUTILS_TIMER_COUNTER` to use it instead of `rdcycle`.
- **`counter-thread-timer/`** — Standalone counter-thread timer plus a benchmark that measures its resolution (~6 cycle on C910), backing the timing claim in Section 3.2.
- **`bpf-spectre-exploit/`** — eBPF bytecode to reliably emit the type confusion gadget used in the end-to-end exploit (Section 4).
- **`kernel-patches/`** — The RISC-V Spectre v1 kernel patches contributed by this work, collected for inspection (Section 6). Each is a standalone `.patch` file; see `kernel-patches/README.md` for subjects and upstream status.

## Not Included

- **Mitigation Benchmarks** — Reproducing SLH and fence-based mitigation benchmarks requires SPEC CPU 2017, which is proprietary and cannot be redistributed (Section 5.2).
- **End-to-End Exploit** — We deliberately do not release a turnkey end-to-end exploit; assembling the individual components requires significant domain expertise.
