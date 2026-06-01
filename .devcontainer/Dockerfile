FROM ubuntu:24.04

ARG USER=defaultuser

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y sudo

RUN useradd -m -s /bin/bash ${USER} && \
    echo "${USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

RUN apt-get update && apt-get install -y \
    openssh-server \
    openssh-client \
    zsh \
    libspdlog-dev \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    gnupg2 \
    libtbb-dev \
    libeigen3-dev \
    nlohmann-json3-dev \
    gdb \
    python3-dev \
    python3-pip \
    python3-venv \
    python3-numpy \
    python3-matplotlib \
    python3-pandas \
    python3-networkx \
    python3-scipy \
    python3-pil \
    pipx \
    ffmpeg \
    ccache

ENV PIPX_HOME=/workspaces/CellEvoX/.pipx \
    PIPX_BIN_DIR=/workspaces/CellEvoX/.pipx/bin \
    PIPX_LOG_DIR=/workspaces/CellEvoX/.pipx/logs \
    MPLCONFIGDIR=/tmp/matplotlib \
    PATH=/workspaces/CellEvoX/.pipx/bin:${PATH}

USER ${USER}
