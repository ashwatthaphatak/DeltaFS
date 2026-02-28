FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libgrpc++-dev \
    libz-dev \
    uuid-dev \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace

RUN cmake -S . -B build \
 && cmake --build build -j$(nproc) \
 && install -m 0755 build/deltafs_node /usr/local/bin/deltafs_node \
 && install -m 0755 build/deltafs_ctl /usr/local/bin/deltafs_ctl

EXPOSE 50051
VOLUME ["/var/lib/deltafs"]
ENTRYPOINT ["/usr/local/bin/deltafs_node"]
