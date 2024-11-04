FROM ubuntu:24.10

# Environment variables
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    gnupg2 

    # Create vscode user with sudo access
RUN useradd -m -s /bin/bash vscode && \
echo "vscode ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

COPY .devcontainer/CellEvoX /vscode/.ssh/CellEvoX
COPY .devcontainer/CellEvoX.pub /vscode/.ssh/CellEvoX.pub

# Update packages and install basic tools
RUN apt-get update && apt-get install -y \
    libxcb1 \
    x11-apps \
    libxcb-cursor0 \
    openssh-server \
    zsh \
    libspdlog-dev \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    gnupg2 \
    libboost-all-dev \
    libtbb-dev \
    libprotobuf-dev \
    protobuf-compiler \
    librocksdb-dev \
    libeigen3-dev \
    qt6-base-dev \
    qt6-tools-dev \
    qt6-quick3d-dev \
    qt6-declarative-dev \
    qt6-wayland \
    qt6-svg-dev \
    qt6-webengine-dev \
    qt6-multimedia-dev \
    qt6-networkauth-dev \
    qt6-lottie-dev 

    

RUN mkdir -p /run/user/1000 \
    && chown vscode:vscode /run/user/1000 \
    && chmod 700 /run/user/1000

ENV XDG_RUNTIME_DIR=/run/user/1000

WORKDIR /workspace

COPY . /workspace

RUN chmod -R 777 /workspace

RUN chmod 600 /vscode/.ssh/CellEvoX && \
chmod 644 /vscode/.ssh/CellEvoX.pub 
RUN usermod -a -G video vscode

# Set default user
USER vscode
