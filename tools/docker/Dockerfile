# SPDX-License-Identifier: MPL-2.0
# The JieLi toolchain is Linux x86-64: Lorcha builds in this image, the toolchain and the SDK mounted read-only.
FROM amd64/debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends python3 patch && rm -rf /var/lib/apt/lists/*
