# mitigation-diffing

Find RISC-V equivalents of x86 Spectre mitigation sites using semantic embeddings.

## Install

```bash
pip install -e .
```

## Usage

```bash
mitigation-diffing /path/to/kernel
mitigation-diffing /path/to/kernel -m my_mitigations.csv -k 5
```

## Options

- `-m, --mitigations`: CSV file with mitigation sites (default: mitigations.csv)
- `-k, --top-k`: Number of matches to show (default: 3)
