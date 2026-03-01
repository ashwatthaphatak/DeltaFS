FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
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

RUN rm -rf /tmp/deltafs-build \
 && cmake -S /workspace -B /tmp/deltafs-build \
 && cmake --build /tmp/deltafs-build -j$(nproc) \
 && install -m 0755 /tmp/deltafs-build/deltafs_node /usr/local/bin/deltafs_node \
 && install -m 0755 /tmp/deltafs-build/deltafs_ctl /usr/local/bin/deltafs_ctl

EXPOSE 50051
VOLUME ["/var/lib/deltafs"]
ENTRYPOINT ["/usr/local/bin/deltafs_node"]
