# SPEC CPU 2017 Cross-Compilation Docker
#
# Build:
#   docker build -t specbuild docker/
#
# Run:
#   docker run --rm \
#       -v /path/to/cpu2017:/spec_cpu \
#       -v /path/to/sysroot:/sysroot \
#       -v $(pwd)/packages/output:/output \
#       -v $(pwd)/configs:/configs \
#       -v $(pwd)/scripts:/scripts \
#       specbuild --arch=riscv64 --config=/configs/riscv64-gcc.cfg --benchmarks=505.mcf_r

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

# Install build dependencies and GCC cross-compilers
RUN apt-get update && apt-get install -y \
    build-essential \
    python3 \
    perl \
    make \
    wget \
    xz-utils \
    # RISC-V
    gcc-riscv64-linux-gnu \
    g++-riscv64-linux-gnu \
    gfortran-riscv64-linux-gnu \
    # ARM64
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    gfortran-aarch64-linux-gnu \
    # Runtime libs for mounted LLVM
    libxml2 zlib1g libtinfo6 libncurses6 \
    && rm -rf /var/lib/apt/lists/*

# LoongArch cross-compiler
ENV LOONGARCH_VERSION="binutils_2.43.1-gcc_14.2.0-glibc_2.40"
RUN cd /tmp && \
    wget -q https://github.com/loongson/build-tools/releases/download/2024.11.01/x86_64-cross-tools-loongarch64-${LOONGARCH_VERSION}.tar.xz && \
    tar -xf x86_64-cross-tools-loongarch64-${LOONGARCH_VERSION}.tar.xz -C /opt && \
    rm x86_64-cross-tools-loongarch64-${LOONGARCH_VERSION}.tar.xz && \
    for tool in gcc g++ gfortran ar as ld nm objcopy objdump ranlib strip; do \
        ln -sf /opt/cross-tools/bin/loongarch64-unknown-linux-gnu-$tool \
               /usr/local/bin/loongarch64-linux-gnu-$tool; \
    done

# Create mount points
RUN mkdir -p /spec_cpu /sysroot /output /configs /scripts /opt/llvm-riscv

# Copy build script
COPY specbuild.py /usr/local/bin/specbuild
RUN chmod +x /usr/local/bin/specbuild

ENTRYPOINT ["specbuild"]
CMD ["--help"]
