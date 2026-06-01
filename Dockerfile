FROM ubuntu:24.04

ARG USER=defaultuser
RUN useradd -m ${USER}
# Environment variables
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y sudo

# Update packages and install basic tools
RUN apt-get update && apt-get install -y \
    openssh-server \
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

ENV PIPX_HOME=/home/${USER}/.pipx \
    PIPX_BIN_DIR=/home/${USER}/.pipx/bin \
    PIPX_LOG_DIR=/home/${USER}/.pipx/logs \
    MPLCONFIGDIR=/tmp/matplotlib \
    PATH=/home/${USER}/.pipx/bin:${PATH}

COPY . /home/${USER}/CellEvoX

# Set permissions for the user
RUN chown -R ${USER}:${USER} /home/${USER}/CellEvoX

# Switch to default user
USER ${USER}

# Set the working directory to the repository
WORKDIR /home/${USER}/CellEvoX
