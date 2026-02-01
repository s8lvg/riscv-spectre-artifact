# Spectre Exploit Variants 

Transient execution attacks on RISC-V processors using unified framework.

## Quick Start

```bash
# Run all variants
python3 run_all_tests.py --group all --runs 5

# Export results to CSV
python3 run_all_tests.py --group pht --runs 10 --csv results.csv

# Debug mode (shows cache hits per iteration)
python3 run_all_tests.py --variants PHT/sa_ip --runs 1 --debug

# Manual threshold and core pinning
python3 run_all_tests.py --group btb --threshold 150 --core 2
```

## Test Groups

`--group all` runs 13 standard variants: PHT, BTB, RSB (each with sa_ip, sa_oop, ca_ip, ca_oop) plus STL. Use `--group misc` for additional experiments.

## Documentation

- [Framework API](docs/FRAMEWORK_API.md): Hook system and context fields
- [Adding Variants](docs/ADDING_VARIANTS.md): How to implement new attacks
