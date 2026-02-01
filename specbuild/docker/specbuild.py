#!/usr/bin/env python3
"""SPEC CPU 2017 Minimal Cross-Compilation Build System

Generates minimal benchmark packages that don't require runcpu on the target.
Uses runcpu --fake on build host to generate run directories with speccmds.cmd,
then packages only what's needed to run benchmarks directly with specinvoke.

Runs inside Docker container. Expects mounts:
  /spec_cpu       - SPEC CPU 2017 installation
  /sysroot        - Target sysroot
  /output         - Output directory
  /configs        - Config files
  /scripts        - External scripts (run.sh, specinvoke_config.h)
  /opt/llvm-riscv - Custom LLVM compiler (optional)
"""

import argparse
import logging
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
    stream=sys.stdout,
)
log = logging.getLogger(__name__)

# Paths inside container
SPEC_CPU = Path("/spec_cpu")
SYSROOT = Path("/sysroot")
OUTPUT = Path("/output")
SCRIPTS = Path("/scripts")

# Architecture to compiler triple mapping
TRIPLES = {
    "riscv64": "riscv64-linux-gnu",
    "aarch64": "aarch64-linux-gnu",
    "loongarch64": "loongarch64-linux-gnu",
}

# Benchmark suites
INTRATE = [
    "500.perlbench_r", "502.gcc_r", "505.mcf_r", "520.omnetpp_r",
    "523.xalancbmk_r", "525.x264_r", "531.deepsjeng_r", "541.leela_r",
    "548.exchange2_r", "557.xz_r"
]

FPRATE = [
    "503.bwaves_r", "507.cactuBSSN_r", "508.namd_r", "510.parest_r",
    "511.povray_r", "519.lbm_r", "521.wrf_r", "526.blender_r",
    "527.cam4_r", "538.imagick_r", "544.nab_r", "549.fotonik3d_r",
    "554.roms_r"
]


def fix_speccmds_paths(cmd_file: Path) -> None:
    """Fix absolute Docker paths in speccmds.cmd/compare.cmd to work on target."""
    if not cmd_file.exists():
        return

    lines = cmd_file.read_text().splitlines()
    fixed = []

    for line in lines:
        if line.startswith("-C /"):
            fixed.append("-C .")
        elif line.startswith("-E PATH "):
            fixed.append("-E PATH /usr/local/bin:/usr/bin:/bin")
        elif line.startswith("-E SPEC /"):
            fixed.append("-E SPEC .")
        elif line.startswith("-E SPECDB_PWD /"):
            fixed.append("-E SPECDB_PWD .")
        elif line.startswith(("-E DEBIAN_FRONTEND ", "-E HOSTNAME ",
                              "-E PYTHONUNBUFFERED ", "-E HOME /root")):
            continue
        elif "/spec_cpu/bin/specperl" in line or "/spec_cpu/bin/harness/specdiff" in line:
            # Fix specdiff: use local specdiff and .ref files
            match = re.search(
                r'-k -o (\S+\.cmp)\s+.*specdiff\s+(.*?)\s+/spec_cpu/.*?/output/(\S+)\s+(\S+)\s*>',
                line
            )
            if match:
                cmp, params, ref, out = match.groups()
                fixed.append(f"-k -o {cmp} specdiff {params} {ref}.ref {out} > {cmp}")
            else:
                # Fallback
                line = line.replace("/spec_cpu/bin/specperl ", "")
                line = line.replace("/spec_cpu/bin/harness/specdiff", "specdiff")
                line = re.sub(r'/spec_cpu/benchspec/CPU/\d+\.\w+_r/data/\w+/output/', '', line)
                fixed.append(line)
        else:
            fixed.append(line)

    cmd_file.write_text("\n".join(fixed) + "\n")


def get_compiler(arch: str) -> tuple[str, str]:
    """Return (CC, sysroot_path) for architecture."""
    triple = TRIPLES[arch]
    if arch == "loongarch64":
        return f"{triple}-gcc", "/opt/cross-tools/target"
    return f"{triple}-gcc", str(SYSROOT)


def build_specinvoke(arch: str, output_bin: Path) -> bool:
    """Cross-compile specinvoke for target architecture."""
    src = SPEC_CPU / "tools" / "src" / "specinvoke"
    if not src.exists():
        log.warning("specinvoke source not found at %s", src)
        return False

    cc, sysroot = get_compiler(arch)

    # Copy config.h from mounted scripts
    config_h = src / "config.h"
    if not config_h.exists():
        src_config = SCRIPTS / "specinvoke_config.h"
        if src_config.exists():
            shutil.copy2(src_config, config_h)
        else:
            log.error("specinvoke_config.h not found in /scripts")
            return False

    cmd = [
        cc, f"--sysroot={sysroot}", "-static", "-Os",
        f"-L{sysroot}/usr/lib", f"-L{sysroot}/usr/lib64",
        f"-L{sysroot}/lib", f"-L{sysroot}/lib64",
        "-o", str(output_bin / "specinvoke"),
        str(src / "specinvoke.c"), str(src / "unix.c"), str(src / "getopt.c"),
    ]

    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode == 0:
        log.info("Built specinvoke for %s", arch)
        return True
    log.error("Failed to build specinvoke: %s", result.stderr)
    return False


def extract_label(config: Path) -> str:
    """Extract label from SPEC config file."""
    content = config.read_text()
    for pattern in [r'^%define\s+label\s+(\S+)', r'^label\s*=\s*(\S+)']:
        match = re.search(pattern, content, re.MULTILINE)
        if match:
            return match.group(1)
    return config.stem


def get_benchmarks(spec: str) -> list[str]:
    """Expand benchmark specification to list."""
    cpu_dir = SPEC_CPU / "benchspec" / "CPU"
    result = []

    for s in spec.split():
        if s == "intrate":
            result.extend(INTRATE)
        elif s == "fprate":
            result.extend(FPRATE)
        elif s == "all":
            result.extend(INTRATE + FPRATE)
        else:
            # Single benchmark
            for d in cpu_dir.iterdir():
                if d.is_dir() and d.name.startswith(s.split(".")[0] + "."):
                    result.append(d.name)
                    break
            else:
                if (cpu_dir / s).exists():
                    result.append(s)

    return [b for b in result if (cpu_dir / b).exists()]


def run_runcpu(config: Path, benchmarks: list[str], action: str, size: str = "ref") -> bool:
    """Run runcpu with specified action."""
    env = os.environ.copy()
    env["SPEC"] = str(SPEC_CPU)
    env["PATH"] = f"{SPEC_CPU}/bin:{env['PATH']}"

    cmd = [
        str(SPEC_CPU / "bin" / "runcpu"),
        f"--config={config}",
        f"--size={size}",
        "--tune=base",
    ]

    if action == "fake":
        cmd.extend(["--fake", "--loose", "--iterations=1"])
    elif action == "build":
        cmd.extend(["--action=build", "--rebuild", f"--define=build_ncpus={os.cpu_count()}"])

    cmd.extend(benchmarks)
    log.info("runcpu %s: %s", action, " ".join(benchmarks))

    result = subprocess.run(cmd, cwd=SPEC_CPU, env=env,
                           capture_output=(action == "fake"), check=False)
    if result.returncode != 0:
        log.error("runcpu --%s failed", action)
        return False
    return True


def copy_ref_outputs(bench: str, run_dir: Path, size: str) -> None:
    """Copy reference output files for validation."""
    data = SPEC_CPU / "benchspec" / "CPU" / bench / "data"
    for variant in [f"{size}rate", f"{size}speed", size]:
        output_dir = data / variant / "output"
        if output_dir.exists():
            for f in output_dir.iterdir():
                if f.is_file():
                    shutil.copy2(f, run_dir / f"{f.name}.ref")
            break


def package_benchmark(bench: str, label: str, output: Path, size: str) -> bool:
    """Package a single benchmark."""
    cpu_src = SPEC_CPU / "benchspec" / "CPU" / bench
    cpu_dst = output / "benchspec" / "CPU" / bench

    if not cpu_src.exists():
        return False

    # Create structure
    (cpu_dst / "exe").mkdir(parents=True, exist_ok=True)
    (cpu_dst / "run").mkdir(parents=True, exist_ok=True)

    # Copy executable
    exe_copied = False
    for exe in (cpu_src / "exe").iterdir():
        if exe.is_file() and label in exe.name:
            shutil.copy2(exe, cpu_dst / "exe" / exe.name)
            exe_copied = True

    if not exe_copied:
        log.warning("No executable for %s with label %s", bench, label)
        return False

    # Copy run directory
    run_src = cpu_src / "run"
    for run_dir in sorted(run_src.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True):
        if run_dir.is_dir() and run_dir.name.startswith("run_base") and label in run_dir.name:
            if not (run_dir / "speccmds.cmd").exists():
                continue

            dst_run = cpu_dst / "run" / run_dir.name
            shutil.copytree(run_dir, dst_run, dirs_exist_ok=True)

            # Fix paths
            fix_speccmds_paths(dst_run / "speccmds.cmd")
            fix_speccmds_paths(dst_run / "compare.cmd")

            # Copy executable into run dir
            for exe in (cpu_dst / "exe").iterdir():
                if exe.is_file() and not (dst_run / exe.name).exists():
                    shutil.copy2(exe, dst_run / exe.name)

            # Copy reference outputs and specdiff for validation
            copy_ref_outputs(bench, dst_run, size)
            specdiff = SPEC_CPU / "bin" / "harness" / "specdiff"
            if specdiff.exists():
                shutil.copy2(specdiff, dst_run / "specdiff")
                (dst_run / "specdiff").chmod(0o755)

            log.info("Packaged %s", bench)
            return True

    log.warning("No run directory for %s", bench)
    return False


def create_package(arch: str, config: Path, benchmarks: list[str],
                   output: Path, size: str) -> None:
    """Create minimal package."""
    label = extract_label(config)
    log.info("Creating package with label: %s", label)

    # Build specinvoke
    bin_dir = output / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    if not build_specinvoke(arch, bin_dir):
        sys.exit(1)

    # Generate run directories and build
    if not run_runcpu(config, benchmarks, "fake", size):
        sys.exit(1)
    if not run_runcpu(config, benchmarks, "build", size):
        sys.exit(1)

    # Package benchmarks
    packaged = sum(1 for b in benchmarks if package_benchmark(b, label, output, size))

    # Copy run script
    run_script = SCRIPTS / "run.sh"
    if run_script.exists():
        shutil.copy2(run_script, output / "run.sh")
        (output / "run.sh").chmod(0o755)
    else:
        log.warning("run.sh not found in /scripts")

    subprocess.run(["chmod", "-Rf", "a+rX", str(output)], check=False, stderr=subprocess.DEVNULL)
    log.info("Package created: %d benchmarks", packaged)


def main():
    parser = argparse.ArgumentParser(description="SPEC CPU 2017 Minimal Cross-Compilation")
    parser.add_argument("--arch", required=True, choices=TRIPLES.keys())
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--benchmarks", default="intrate")
    parser.add_argument("--size", default="ref", choices=["test", "train", "ref"])
    args = parser.parse_args()

    # Validate
    if not SPEC_CPU.exists() or not (SPEC_CPU / "shrc").exists():
        sys.exit("Error: SPEC CPU not mounted at /spec_cpu")
    if not SYSROOT.exists():
        sys.exit("Error: Sysroot not mounted at /sysroot")
    if not args.config.exists():
        sys.exit(f"Error: Config not found: {args.config}")

    OUTPUT.mkdir(parents=True, exist_ok=True)

    benchmarks = get_benchmarks(args.benchmarks)
    if not benchmarks:
        sys.exit(f"Error: No benchmarks for: {args.benchmarks}")

    log.info("SPEC CPU 2017 Minimal Build")
    log.info("  Arch: %s, Config: %s", args.arch, args.config.name)
    log.info("  Benchmarks: %s", " ".join(benchmarks))

    create_package(args.arch, args.config, benchmarks, OUTPUT, args.size)
    log.info("Done. Output: %s", OUTPUT)


if __name__ == "__main__":
    main()
