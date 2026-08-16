# SPDX-License-Identifier: LGPL-2.1-or-later

# Stage 1: Build yamlfmt
FROM golang:1 AS yaml-builder
# defined from build kit
# DOCKER_BUILDKIT=1 docker build . -t ...
ARG TARGETARCH

WORKDIR /yamlfmt
RUN go install github.com/google/yamlfmt/cmd/yamlfmt@v0.16.0 && \
    strip $(which yamlfmt) && \
    yamlfmt --version

# Stage 2: GMAN development container
#
# Carries every toolchain CI uses, so all five build legs and all three
# workflows can be reproduced locally before pushing. Both compilers are
# present deliberately: macOS clang does not diagnose what gcc and libstdc++
# do, and every phase of this project so far has had at least one defect that
# only the Linux legs could see.
FROM debian:stable-slim

# Consumed by the LABEL block below; build.sh passes the short commit SHA.
ARG VERSION=dev

RUN export DEBIAN_FRONTEND=noninteractive && \
    apt update && \
    apt install -y -q --no-install-recommends \
    sudo ca-certificates curl git gnupg2 \
    build-essential clang lld cmake ninja-build \
    gdb python3 clang-format clang-tidy \
    valgrind \
    libtiff-dev libpng-dev libjpeg-dev zlib1g-dev \
    nodejs npm \
    && \
    apt clean && \
    rm -rf /var/lib/apt/lists/*

# commitlint, so the conventional-commit gate can be checked before pushing
RUN npm install -g @commitlint/cli @commitlint/config-conventional && \
    npm cache clean --force

RUN useradd --create-home -s /bin/bash gman
RUN usermod -a -G sudo gman
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

ENV USER=gman
ENV PATH=${PATH}:/go/bin
COPY --chown=${USER}:${USER} --from=yaml-builder /go/bin/yamlfmt /go/bin/yamlfmt

# git refuses to operate on a bind-mounted tree owned by another uid
RUN git config --system --add safe.directory '*'

USER gman
WORKDIR /workspaces/gman

RUN g++ --version && clang++ --version && cmake --version

LABEL \
    org.label-schema.name="gman" \
    org.label-schema.description="GMAN Development Container" \
    org.label-schema.url="https://github.com/jac18281828/gman" \
    org.label-schema.vcs-url="git@github.com:jac18281828/gman.git" \
    org.label-schema.vendor="John Cairns" \
    org.label-schema.version=$VERSION \
    org.label-schema.schema-version="1.0" \
    org.opencontainers.image.description="GMAN Development Container"
