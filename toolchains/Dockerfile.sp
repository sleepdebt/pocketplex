# toolchains/Dockerfile.sp.
# Build image for the RG35XX SP / Knulli (aarch64, glibc 2.40).
# Host-arch independent: gcc-aarch64-linux-gnu cross-compiles to arm64 on any host.
# Base glibc is bookworm's 2.36 (<= device's 2.40 => forward compatible).
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture arm64 \
 && apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      build-essential \
      pkg-config \
      gcc-aarch64-linux-gnu \
      libc6-dev-arm64-cross \
      crossbuild-essential-arm64 \
      libsdl2-dev:arm64 \
      libsdl2-ttf-dev:arm64 \
      libcurl4-openssl-dev:arm64 \
      libcjson-dev:arm64 \
      zip \
    && rm -rf /var/lib/apt/lists/*

# Sanity: arm64 cross toolchain + arm64 pkg-config find SDL2/cURL/cJSON.
RUN aarch64-linux-gnu-gcc --version | head -1 \
 && PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
    pkg-config --modversion sdl2 SDL2_ttf libcurl
