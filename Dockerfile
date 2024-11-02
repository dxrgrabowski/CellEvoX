FROM ubuntu:24.10

RUN sudo apt-get update && sudo apt-get install -y \
    libboost-all-dev \
    libtbb-dev \
    libprotobuf-dev \
    protobuf-compiler \
    librocksdb-dev \
    libeigen3-dev \
    && sudo rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace
