# SPDX-License-Identifier: Apache-2.0 OR MIT
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential bash bc bison bzip2 ca-certificates cpio curl file flex gawk \
    git libncurses-dev patch perl python3 rsync sed tar unzip wget xz-utils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
