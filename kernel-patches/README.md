# RISC-V Spectre v1 kernel patches

All kernel patches produced by this work, collected for inspection. Each is a
standalone `.patch` file you can read or apply.

| File | Subject |
|------|---------|
| `syscall-table-indexing-25fd7ee7bf58.patch` | riscv: Sanitize syscall table indexing under speculation |
| `kvm-aplic-8565617a8599.patch` | KVM: riscv: Fix Spectre-v1 in APLIC interrupt handling |
| `kvm-one_reg-f9e26fc32541.patch` | KVM: riscv: Fix Spectre-v1 in ONE_REG register access |
| `kvm-aia-csr-ec87a82ca874.patch` | KVM: riscv: Fix Spectre-v1 in AIA CSR access |
| `kvm-fp-reg-8f0c15c4b14f.patch` | KVM: riscv: Fix Spectre-v1 in floating-point register access |
| `kvm-pmu-2dda6a9e09ee.patch` | KVM: riscv: Fix Spectre-v1 in PMU counter access |
| `uaccess-pointer-masking-v1.patch` | riscv: Use pointer masking to limit uaccess speculation |
| `bpf-emit-fence-i-nospec.patch` | riscv, bpf: Emit fence.i for BPF_NOSPEC |
| `futex-mask-user-pointers.patch` | riscv: futex: Mask __user pointers prior to dereference |

Filenames with a hash are the version merged into mainline Linux (torvalds/linux,
that commit).
