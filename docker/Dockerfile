FROM --platform=linux/arm64 debian:trixie
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    make cmake git bc bison flex \
    libssl-dev libelf-dev \
    build-essential \
    nano
WORKDIR /work
