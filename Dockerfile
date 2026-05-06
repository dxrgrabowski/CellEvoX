FROM ubuntu:24.04

ARG USER=defaultuser
RUN useradd -m ${USER}
# Environment variables
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y sudo

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
    libeigen3-dev \
    qt6-base-dev \
    qt6-tools-dev \
    qt6-quick3d-dev \
    qt6-declarative-dev \
    qt6-wayland \
    qml6-module-qtquick \
    qml6-module-qtquick-controls \
    qml6-module-qtquick-window \
    qml6-module-qtquick-dialogs \
    qml6-module-qtquick-layouts \
    qml6-module-qtquick-particles \
    qml6-module-qtqml-workerscript \
    qml6-module-qtquick-templates \
    qml6-module-qtqml \
    qml6-module-qt5compat-graphicaleffects \
    qt6-5compat-dev \
    qml6-module-qt-labs-platform \
    qml6-module-qt-labs-folderlistmodel \
    nlohmann-json3-dev \
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

COPY ./CellEvoX /home/${USER}/CellEvoX

# Set permissions for the user
RUN chown -R ${USER}:${USER} /home/${USER}/CellEvoX

# Switch to default user
USER ${USER}

# Set the working directory to the repository
WORKDIR /home/${USER}/CellEvoX
