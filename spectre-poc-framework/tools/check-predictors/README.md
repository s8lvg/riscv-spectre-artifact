# C910 Predictor Check

Kernel module to inspect T-Head C910 MHCR register via userspace interface.

## Quick Start

```bash
# Build
make

# Load module (creates /proc/c910_predictors)
sudo make install

# Read predictor status from userspace
cat /proc/c910_predictors

# Unload when done
sudo make remove
```

## Usage

Once loaded, the module creates `/proc/c910_predictors` which can be read anytime:

```bash
# Check predictor status (no sudo needed after loading)
cat /proc/c910_predictors

# Use in scripts
if grep -q "^btb:.*1$" /proc/c910_predictors; then
    echo "BTB is enabled"
fi

# Parse specific values
grep "^branch_prediction:" /proc/c910_predictors
awk '/^return_stack:/ {print $2}' /proc/c910_predictors
```

The file shows enabled/disabled status for:
- **BPE**: Branch prediction enable
- **BTB**: Branch target buffer
- **IBPE**: Indirect branch prediction
- **RS**: Return stack
- **L0BTB**: Level-0 BTB
- **Cache config**: I-Cache, D-Cache, write allocate, etc.

## Example Output

```
$ cat /proc/c910_predictors
mhcr:			0x00000000000011ff
icache_enable:		1
dcache_enable:		1
write_allocate:		1
writeback:		1
write_burst:		1
branch_prediction:	1
btb:			1
indirect_branch_pred:	1
return_stack:		1
l0btb:			1
clock_ratio:		1:1
```

Values are `1` (enabled) or `0` (disabled).

## Technical Details

- Creates `/proc/c910_predictors` file using procfs
- Reads SHCR register (0x5C1) which mirrors MHCR in S-mode
- Live readout - values are read from CSR on each `cat`
- Based on C910 User Manual section 16.1.7.2
- **C910 only** - P550 has different CSR layout

## Makefile Targets

```bash
make          # Build module
make install  # Load module (insmod)
make remove   # Unload module (rmmod)
make check    # Show last 50 dmesg lines
make clean    # Clean build artifacts
```
