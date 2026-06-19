FROM ubuntu:22.04

ARG SMATCH_COMMIT=bcc58b9ccf06d28ab6be4f0992bc74f462aa12f8

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    bc \
    bison \
    build-essential \
    ca-certificates \
    curl \
    file \
    flex \
    g++-riscv64-linux-gnu \
    gcc-riscv64-linux-gnu \
    git \
    libelf-dev \
    libncurses-dev \
    libsqlite3-dev \
    libssl-dev \
    make \
    perl \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    rsync \
    sqlite3 \
    unzip \
    universal-ctags \
    xz-utils \
    zstd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/gadet-scanning
COPY . /opt/gadet-scanning

RUN python3 -m pip install --no-cache-dir --upgrade pip wheel \
    && python3 -m pip install --no-cache-dir --index-url https://download.pytorch.org/whl/cpu torch==2.12.1+cpu \
    && python3 -m pip install --no-cache-dir transformers numpy \
    && python3 -m pip install --no-cache-dir --no-deps -e mitigation_diffing

RUN set -eux; \
    cd smatch; \
    git clone https://github.com/error27/smatch.git smatch-src; \
    cd smatch-src; \
    git checkout "$SMATCH_COMMIT"; \
    sed -i 's/struct arg_state args\[slots\] = {};/struct arg_state args[slots]; memset(args, 0, sizeof(args));/' pre-process.c; \
    make clean || true; \
    make -j"$(nproc)"; \
    test -x smatch
RUN cd codeql && ./setup.sh

ENV PATH="/opt/gadet-scanning/codeql/codeql-home/codeql:${PATH}"

CMD ["/bin/bash"]
