FROM ubuntu:24.04

ARG USER=defaultuser

# Environment variables
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y sudo

RUN useradd -m -s /bin/bash ${USER} && \
    echo "${USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Tworzenie folderu /run/user/1000 i przypisanie praw na podstawie zmiennej USER
RUN mkdir -p /run/user/1000 \
    && chown ${USER}:${USER} /run/user/1000 \
    && chmod 700 /run/user/1000

# Dodanie użytkownika do grupy video
RUN usermod -a -G video ${USER}

# COPY .devcontainer/CellEvoX /dxr/.ssh/CellEvoX
# COPY .devcontainer/CellEvoX.pub /dxr/.ssh/CellEvoX.pub

# Update packages and install basic tools
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
    libboost-program-options-dev \
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


ENV XDG_RUNTIME_DIR=/run/user/1000

# Domyślny użytkownik w kontenerze
USER ${USER}
