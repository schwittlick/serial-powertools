FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        git \
        libgl-dev \
        libglfw3-dev \
        libserialport-dev \
        lsof \
        meson \
        ninja-build \
        pkg-config \
        procps \
        python3 \
        socat \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
