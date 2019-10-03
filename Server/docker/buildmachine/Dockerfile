# Copyright (c) 2019 Christof Ruch. All rights reserved.
#
# Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase

FROM ubuntu:latest

RUN apt-get update
RUN apt-get -y install unzip less
RUN apt-get -y install g++ make

# Stuff required to use JUCE in the basic version for the server
RUN apt-get -y install libasound2-dev libcurl4-openssl-dev pkg-config

COPY juce-5.4.3-linux.zip /root
RUN cd /root && unzip /root/juce-5.4.3-linux.zip && rm juce-5.4.3-linux.zip

# Stuff required by my code
RUN apt-get -y install libtbb-dev

RUN echo "cd /src/Server/Builds/LinuxMakefile && make" > /root/run_make.sh
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
