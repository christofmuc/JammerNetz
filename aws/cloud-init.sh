#!/bin/bash
apt-get -y update
apt-get -y install unzip g++ make libasound2-dev libcurl4-openssl-dev pkg-config libtbb-dev
wget https://github.com/Kitware/CMake/releases/download/v3.16.5/cmake-3.16.5-Linux-x86_64.sh
chmod +x cmake-3.16.5-Linux-x86_64.sh
./cmake-3.16.5-Linux-x86_64.sh --skip-license --prefix=/usr

git clone --recurse-submodules -j8 https://github.com/christofmuc/JammerNetz
cd JammerNetz
# Configure
cmake -S . -B builds -G "Unix Makefiles"

# Build Flatbuffers library
cd third_party/flatbuffers
cmake -S . -B LinuxBuilds -G "Unix Makefiles"
cmake --build LinuxBuilds

# Now build the server
cd ../..
cmake --build builds/Server/

# Install server
cp ./builds/Server/JammerNetzServer /bin

# Install service with systemd
echo "[Unit]
Description=JammerNetz Server
After=network.target
StartLimitIntervalSec=0
[Service]
Type=simple
Restart=always
RestartSec=1
User=ubuntu
ExecStart=/bin/JammerNetzServer

[Install]
WantedBy=multi-user.target" > /etc/systemd/system/JammerNetz.service

# Enable service to launch on start
systemctl enable JammerNetz

# Launch now (for first start)
systemctl start JammerNetz
