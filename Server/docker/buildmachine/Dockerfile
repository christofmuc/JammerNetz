# Copyright (c) 2019 Christof Ruch. All rights reserved.
#
# Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase

FROM ubuntu:latest

RUN apt-get update && apt-get -y install unzip less g++ make

# Stuff required to use JUCE in the basic version for the server
RUN apt-get -y install libasound2-dev libcurl4-openssl-dev pkg-config

# Add much more stuff to be used by the Client
RUN apt-get -y install libfreetype6-dev libjack-dev libx11-dev mesa-common-dev libgl1-mesa-dev libglew-dev libgtk-3-dev webkit2gtk-4.0

# Stuff required by my code
RUN apt-get -y install libtbb-dev libssl-dev

COPY curl-7.67.0.tar.gz /root
RUN cd /root && tar -xzf curl-7.67.0.tar.gz && cd curl-7.67.0 && ./configure --with-ssl && make -j4 && make install && ldconfig

COPY cmake-3.15.5.tar.gz /root
RUN cd /root && tar -xzf cmake-3.15.5.tar.gz && cd cmake-3.15.5 && ./bootstrap && make -j4 && make install

RUN apt-get install -y tofrodos
COPY run_make.sh /root
RUN fromdos /root/run_make.sh
RUN chmod +x /root/run_make.sh

ENTRYPOINT /root/run_make.sh

# Build the container via
#
# docker build .
#
# and then run it with
#
# docker run --volume=//d/development/jammernetz:/src -it -p 7777:7777/udp --entrypoint=/bin/bash --rm <containerID>
#
