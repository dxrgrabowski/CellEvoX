FROM stateoftheartio/qt6:6.8-gcc-aqt

# Dodajemy tylko nasze dodatkowe zależności
RUN sudo apt-get update && sudo apt-get install -y \
    libboost-all-dev \
    libtbb-dev \
    libprotobuf-dev \
    protobuf-compiler \
    librocksdb-dev \
    libeigen3-dev \
    && sudo rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
