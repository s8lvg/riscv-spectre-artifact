#!/usr/bin/env python3
"""SPEC CPU 2017 Build Wrapper

Reads spec-build.yaml and invokes Docker to cross-compile benchmarks.
Produces minimal packages that run without runcpu on the target.
"""

import argparse
import logging
import subprocess
import sys
import tarfile
from pathlib import Path

import yaml

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger(__name__)

BASE = Path(__file__).parent


def run_docker(spec_cpu: Path, sysroot: Path, output: Path, configs_dir: Path,
               scripts_dir: Path, arch: str, config: str, benchmarks: str,
               size: str = "ref", compiler: Path = None, image: str = "specbuild") -> None:
    """Run Docker build."""
    cmd = [
        "docker", "run", "--rm",
        "-v", f"{spec_cpu}:/spec_cpu",
        "-v", f"{sysroot}:/sysroot",
        "-v", f"{output}:/output",
        "-v", f"{configs_dir}:/configs",
        "-v", f"{scripts_dir}:/scripts",
    ]

    if compiler:
        cmd.extend(["-v", f"{compiler}:/opt/llvm-riscv"])

    cmd.extend([
        image,
        f"--arch={arch}",
        f"--config=/configs/{config}",
        f"--benchmarks={benchmarks}",
        f"--size={size}",
    ])

    log.info("Docker: %s", " ".join(cmd))
    subprocess.run(cmd, check=True)


def create_tarball(output: Path, name: str) -> None:
    """Create compressed tarball."""
    tarball = output.parent / f"{name}.tar.gz"
    log.info("Creating tarball: %s", tarball.name)
    with tarfile.open(tarball, "w:gz") as tar:
        tar.add(output, arcname=name)
    size_mb = tarball.stat().st_size / 1024 / 1024
    log.info("Created %s (%.1fMB)", tarball.name, size_mb)


def build_target(name: str, target: dict, config: dict, args) -> None:
    """Build a single target."""
    spec_cpu = (BASE / config["spec_cpu"]["path"]).resolve()
    sysroot = (BASE / target["sysroot"]).resolve()
    output_dir = (BASE / config["output"]["dir"]).resolve()
    configs_dir = (BASE / "configs").resolve()
    scripts_dir = (BASE / "scripts").resolve()

    if not spec_cpu.exists():
        sys.exit(f"Error: SPEC CPU not found: {spec_cpu}")
    if not sysroot.exists():
        sys.exit(f"Error: Sysroot not found: {sysroot}")

    configs = target.get("configs", [])
    if not configs:
        sys.exit(f"Error: No configs for target {name}")

    compiler = args.compiler.resolve() if args.compiler else None

    for cfg_path in configs:
        cfg_name = Path(cfg_path).name
        label = Path(cfg_path).stem

        output = output_dir / f"{name}-{label}"
        output.mkdir(parents=True, exist_ok=True)

        log.info("Building %s with %s", name, cfg_name)

        run_docker(
            spec_cpu=spec_cpu,
            sysroot=sysroot,
            output=output,
            configs_dir=configs_dir,
            scripts_dir=scripts_dir,
            arch=target["arch"],
            config=cfg_name,
            benchmarks=args.benchmarks,
            size=args.size,
            compiler=compiler,
        )

        if not args.no_tarball:
            create_tarball(output, f"{name}-{label}")

        log.info("Output: %s", output)


def main():
    parser = argparse.ArgumentParser(
        description="SPEC CPU 2017 Cross-Compilation Build",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 build.py -l                              # List targets
  python3 build.py -t lab46 --benchmarks=505.mcf_r # Single benchmark
  python3 build.py -t lab46 --benchmarks=intrate   # Integer benchmarks
  python3 build.py -t lab46                        # All benchmarks (default)
        """,
    )
    parser.add_argument("config_file", nargs="?", default="spec-build.yaml")
    parser.add_argument("-t", "--target", help="Build specific target")
    parser.add_argument("-l", "--list", action="store_true", help="List targets")
    parser.add_argument("--benchmarks", default="intrate fprate")
    parser.add_argument("--size", default="ref", choices=["test", "train", "ref"])
    parser.add_argument("--no-tarball", action="store_true")
    parser.add_argument("--compiler", type=Path, help="Custom compiler path")

    args = parser.parse_args()

    with open(args.config_file, encoding="utf-8") as f:
        config = yaml.safe_load(f)

    if not config.get("targets"):
        sys.exit("Error: No targets in config")

    if args.list:
        print("Available targets:")
        for name, t in config["targets"].items():
            cfgs = ", ".join(Path(c).stem for c in t.get("configs", []))
            print(f"  {name}: {t['arch']} [{cfgs}]")
        return

    if not args.target:
        sys.exit("Error: Specify target with -t (use -l to list)")

    if args.target not in config["targets"]:
        sys.exit(f"Error: Unknown target '{args.target}'")

    build_target(args.target, config["targets"][args.target], config, args)
    log.info("Done")


if __name__ == "__main__":
    main()
