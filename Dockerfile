FROM ubuntu:24.10

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
    libprotobuf-dev \
    protobuf-compiler \
    librocksdb-dev \
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
    qml6-module-qtquick-effects \
    qml6-module-qtquick-particles \
    qml6-module-qtqml-workerscript \
    qml6-module-qtquick-templates \
    qml6-module-qtqml \
    qml6-module-qt5compat-graphicaleffects \
    qt6-5compat-dev \
    qml6-module-qt-labs-platform \
    qml6-module-qt-labs-folderlistmodel \
    nlohmann-json3-dev \
    python3-matplotlib


ENV XDG_RUNTIME_DIR=/run/user/1000

# Domyślny użytkownik w kontenerze
USER ${USER}
