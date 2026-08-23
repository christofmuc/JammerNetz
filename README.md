[![release](https://img.shields.io/github/v/release/christofmuc/jammernetz?style=plastic)](https://github.com/christofmuc/jammernetz/releases)
[![Build Status](https://dev.azure.com/christof0759/JammerNetz/_apis/build/status/christofmuc.JammerNetz?branchName=master)](https://dev.azure.com/christof0759/JammerNetz/_build/latest?definitionId=1&branchName=master)


# Introduction

JammerNetz is an Open Source system for "networked music performance" (NMP), also known as "jamming over the internet". It consists of a lightweight server software, and a little UI client software that can use your audio devices and stream multiple uncompressed high-quality audio channels to other participants as a common mixdown.

The design choice is that if you have a modern DSL or fibre connection there is no need to degrade your music experience using CODECs mostly designed for speech transmission as used in some other internet jam solutions. We are happily using JammerNetz since over two years for our weekly sessions with synths, voice, and electric guitar.

Of course be aware that the main influence on the quality has your internet provider and your choice where you are running the server. If you have high quality fibre and a good and fast ASIO-capable audio interface, you can expect 50ms total air-to-air latency, which we think is awesome. Of course, if some participants are bound for cable internet and are trying to play together across all of Germany, even running the server in a suitable AWS instance in Frankfurt close to the internet's main hub will not get you much better than 70-80 ms total audio latency (including AD and DA conversion in your interface), which we feel is still worth the effort! Do not try to use WiFi.

## Features

JammerNetz is quite feature rich, the following are the main items:

  * It allows you to host a jam session on a server, allowing a configurable number of clients to participate.
  * It allows for channel configuration to send mono or stereo channels, and also a "send-only" channel type for using microphones without hearing your own voice. This makes for great communication during the session.
  * Send multiple audio channels per client to the server, e.g. your synthesist can send the keyboards and his voice separately.
  * Does automatic hard-disk recording of your session to local disk on each client in a lossless compressed FLAC file. After the session, everybody has a record to revisit.
  * Does automatic MIDI recording in case it detects any incoming MIDI notes, thereby logging all keys played into a MIDI file for later revisit ("what did I play? Sounds great!")
  * Features a built-in instrument tuner display showing you the detected note and cents for each channel, so it is easy and quick to get everybody on the same A.
  * Shows the final master mix as an FFT/waterfall with optional circle-of-fifths pitch colours and tracked-note annotations. Fast, Balanced, and Stable presets plus the concert-A reference can be changed directly in the spectrum panel.
  * Authenticated XChaCha20-Poly1305 encryption for every UDP datagram, using a fresh nonce and replay protection while keeping protocol metadata opaque on the wire.

## Screenshot

Here is a screenshot

[![](client-screenshot.PNG)](https://raw.githubusercontent.com/christofmuc/JammerNetz/master/client-screenshot.PNG)

### Limitations

It should be noted that due to the design of the system, we have a few limitations or restrictions that other systems might not have. We believe that we have made sensible trade-offs, but your milage may vary:

  1. All clients need to run on the same sample rate (48000 is set in the source, but you might want to switch to 44100 in file BuffersConfig.h).
  2. The network packets are set to 128 buffer size, which we feel is the best trade off between number of packets per second, MTU, and latency. The Audio device can run in different / biffer buffer sizes now, but be aware that this might increase latency and require bigger jitter buffers on the server (command line parameter on the server!).
  3. As we are aiming for lowest-possible latency, you should really use an audio device with ASIO drivers on Windows, even if Windows Audio in different modes is offered. Stay away from DirectSound. Mac CoreAudio works as well very nicely, as does Jack on Linux.

## Usage

We provide installers for Windows and Mac client to download here in the release section. The Windows installer includes the server executable for local testing and offers the JammerNetz VST3 plug-in as a default-selected task. For real-life applications you'd need to run the server somewhere in the cloud.

you will need some experience in compiling a C++ application and starting an AWS (or Azure or self-hosted or...) instance and deploying the Linux build of the server there to run it. Depending on the interest in this system, we might be able to provide more help.

# Building the software

### Supported platforms

We use JUCE, a cross-platform library with support for all major platforms, but we have tested the client software at the moment only on Windows 10 using MS Visual Studio 2019 and macOS 10.15, and the server on an up-to-date Ubuntu Linux 20.04 LTS. Other platforms might work as well, but might require some fiddling and fixing.

## Downloading

Clone with submodules from github

    git clone --recurse-submodules -j8 https://github.com/christofmuc/JammerNetz

The recursive clone with  submodules is required to retrieve the following additional modules already into the right spot inside the source tree:

1. We use the magnificent [JUCE library](https://juce.com/) to immensly reduce the amount of work we have to do.
2. the nice JUCE module [ff_meters](https://github.com/ffAudio/ff_meters) from ffAudio to display the level meters for each channel.
3. [Q](https://github.com/cycfi/Q), a highly interesting modern C++ DSP library we use for the instrument tuning/pitch detection. Go check it out, it's really cool!
4. [Infra](https://github.com/cycfi/infra), a little helper library required by Q.
5. [Flatbuffers](https://google.github.io/flatbuffers/), a C++ serialization library we use for parts of the network protocol.

JammerNetz encrypts and authenticates every UDP datagram with XChaCha20-Poly1305. A valid session key is mandatory for clients, plug-ins, and servers.

## Installing more dependencies with Conan

Dependency versions are pinned in `conanfile.py` and installed with a current Conan 2 release. From the JammerNetz top-level directory, run:

    conan profile detect
    conan install . -of conan-deps -s build_type=RelWithDebInfo --build=missing

In case you do not have conan installed, have a look at their documentation and [download page](https://conan.io/downloads.html). It is a great tool!

## Build mode (development vs release)

`JAMMERNETZ_ENABLE_LTO` controls link-time optimization:

- Development default: `OFF` (faster local rebuild and link times).
- CI/release artifacts: use `-DCMAKE_BUILD_TYPE=RelWithDebInfo` and set `-DJAMMERNETZ_ENABLE_LTO=ON` explicitly.

CI/release-with-symbols configuration example:

    cmake -S . -B builds -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJAMMERNETZ_ENABLE_LTO=ON
    cmake --build builds --parallel

If you are using CMake 4.x with the current vendored dependencies (notably googletest), add:

    -DCMAKE_POLICY_VERSION_MINIMUM=3.5

to your configure command.

## Building on Windows

We use modern [CMake 3.14](https://cmake.org/) and Visual Studio 2022 Build Tools for C++. The default generator in this repository is Ninja, so make sure `ninja` is installed and you build from a Developer Command Prompt / Developer PowerShell so MSVC is available.

Optionally, if you want to produce a Windows-style installer for your band members: We always recommend the [InnoSetup](http://www.jrsoftware.org/isinfo.php) tool, really one of these golden tools that despite its age shines on and on. Download it and install it, it will automatically be picked up and used by the CMake build process. The legacy `Client/Client.jucer` project instead reads the compiler location from `JAMMERNETZ_ISCC_EXE`; set that local environment variable to the full path of `iscc.exe` before building its Release configuration.

Using CMake and building JammerNetz client and server is a multi-step build:

    cd third_party\flatbuffers
    cmake -S . -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build Builds --parallel
    cd ..\..
    cmake -S . -B Builds\Windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build Builds\Windows --parallel

For CI/release builds with symbols, explicitly enable LTO:

    cmake -S . -B Builds\Windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJAMMERNETZ_ENABLE_LTO=ON
    cmake --build Builds\Windows --parallel

The build will take a few minutes and produce optimized Client, Server, and VST3 binaries with debug symbols, as well as a client installer if Inno Setup was installed before configuring CMake. The installer offers the complete VST3 bundle as a default-selected task and is created as `<JammerNetzDir>\Builds\Windows\Client\jammernetz_setup_x.x.x.exe`.

To test it, you can launch the server on your local machine with

    <JammerNetzDir>\Builds\Server\Release\JammerNetzServer.exe -k [nameOfSecretsFile]

Simply use the "connect to local server" checkbox of the client.

## Building on macOS

We tested on macOS Mojave 10.15:

First install your prerequisites with brew:

    brew install cmake gtk+3 glew ninja

Then run

    cd third_party/flatbuffers
    cmake -S . -B LinuxBuilds -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build LinuxBuilds --parallel
    cd ../..
    cmake -S . -B Builds/macOS -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build Builds/macOS --parallel


## Building the server for Linux on Windows

Most likely, you're not going to run your server on a Windows machine but prefer a Linux cloud machine located at some strategic position in the Internet.

### Cross-platform building Linux server on Windows 10 using Docker

Thanks to Docker, it has never been easier to do cross-platform development on Windows. If you don't have it, get yourself [Docker Desktop](https://www.docker.com/products/docker-desktop) and experience the power!

### Building the Linux version from command line

After installing Docker Desktop, we're ready to do a cross-platform build for Linux.

First, what we are going to do is to create a docker image for the build machine we are going to use. For that, open a command line, cd into the directory &lt;JammerNetzDir&gt;\Server\docker\buildmachine and run the command

    buildBuildmachine.bat

You can build the Linux version from a Windows command line by just running the provided batch file inside the buildmachine directory. You need to specify the JammerNetz top level directory in the funky Docker volume syntax with forward slashes and a double slash for the drive letter. E.g. the Windows directory D:\Development\JammerNetz needs to be specfied as //d/Development/JammerNetz:

    makeLinux.bat //d/Development/JammerNetz

This is so the Linux virtual machine will mount the source directory from the host, and directly place it's build output on the host, so the virtual machine is immediately removed again after the build is finished.

You will find the output of the Build machine in the directory &lt;JammerNetzDir&gt;\Server\Builds\LinuxMakefile\build. The server executable is just called "JammerNetzServer" and is ready to run on a matching Linux machine.

### Deploying the Linux build to a real Ubuntu server

The build above produced a Linux executable. If you have an Ubuntu server running you want to use, e.g. an Amazon EC2 instance, you can copy the executable to the server e.g. with scp, then dial into the machine with ssh and launch it. For example (from within the `<JammerNetz>` directory, use the IP of your server of course):

For repeatable ARM64 EC2 deployments without compiling on the instance, the repository also contains a manually triggered Packer workflow that publishes a private Ubuntu 24.04 AMI with `JammerNetzServer` already installed. See [aws/ami/README.md](aws/ami/README.md) for the AWS OIDC setup, publication procedure, and deployment-time session-key provisioning.

    scp Builds\LinuxBuilds\Server\JammerNetzServer ubuntu@192.168.172.1:.

Then dial into your server, using ssh or putty, and make sure to have all runtime prequisites installed. With that done, you can just launch the server:

    apt-get install -y libtbb-dev libasound2-dev libjack-dev
    ./JammerNetzServer -k [nameOfSecretsFile]

The server should start up and announce its presensence with a happy

    Server listening on port 7777
    Starting JammerNetz server, using CTRL-C to stop

All clients should be able to connect to the server via its IP address.

## Native Linux builds

If you are working on Linux, make sure to have all development prerequisites installed!

The JUCE documentation has this somewhat hidden at https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md.

### Debian/Ubuntu

On a fresh Debian 9 machine, that would e.g. require the following installs:

    sudo apt-get -y install g++ libasound2-dev libcurl4-openssl-dev libfreetype6-dev libncurses-dev libjack-dev libx11-dev libglew-dev mesa-common-dev webkit2gtk-4.1 libwebkit2gtk-4.1-dev cmake webkit2gtk3-devel.x86_64

	sudo apt update
	# This is what we need extra:
	sudo apt install g++ cmake libncurses-dev libglew-dev
	# This is what the JUCE documentation specifies:
	sudo apt install libasound2-dev libjack-jackd2-dev \
		ladspa-sdk \
		libcurl4-openssl-dev  \
		libfreetype-dev libfontconfig1-dev \
		libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
		libwebkit2gtk-4.1-dev \
		libglu1-mesa-dev mesa-common-dev

### Fedora

For a Fedora-based distribution like Amazon Linux 2, you would use `yum` to install the dependencies:

	# TODO - this needs updating for JUCE8
    sudo yum install alsa-lib-devel libcurl-devel freetype-devel ncurses-devel jack-audio-connection-kit-devel libX11-devel  mesa-libGL-devel webkitgtk4-devel glew-devel


### Compilation

With those installs and the recursive git clone from above, cd into the cloned directory and run cmake with the following commands:

    cmake -S . -B builds -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=conan-deps/conan_toolchain.cmake
    cd third_party/flatbuffers
    cmake -S . -B LinuxBuilds -G Ninja -D FLATBUFFERS_CXX_FLAGS="-Wno-error"
    cmake --build LinuxBuilds --parallel
    cd ../..
    cmake --build builds --parallel

This should have created a server binary as `builds/Server/JammerNetzServer` and a client binary as `builds/Client/JammerNetzClient`.

To launch the server, just type

    ./builds/Server/JammerNetzServer -k session.jnzkey

and it shall listen on port 7777. The server fails closed if `-k` is omitted or the file is invalid.

The gcc version seems to matter, I am testing with a vanilla Ubuntu 18.04 LTS installation which comes with gcc 7.5.0 out of the box.

## Session keys

Generate a fresh key for every jam session with the server command. Existing files are not overwritten unless `--force` is explicitly supplied:

    JammerNetzServer --generate-session-key session.jnzkey

Distribute this file independently to the intended participants, select it as the client or plug-in session key, and delete it from active systems after the session. The file contains a versioned 16-byte session ID and 32-byte master key; JammerNetz derives separate client-to-server and server-to-client traffic keys and never sends the file contents over the network. Fingerprints shown by the applications are short BLAKE2b values for human comparison only.

Do not reuse a key after restarting a security session: replay state is process-local, so a new session requires a new key file and session ID.

## Similar systems

We had used the great [Jamulus](https://github.com/jamulussoftware/jamulus) system before developing our own system, and JammerNetz certainly has been inspired by this great piece of software. Please note that JammerNetz was created in pre-pandemic times, and also before Jamulus got its extra surge in community activity.

We also made some substantial design and architecture changes over Jamulus, justifying a new development instead of contributing to the Jamulus codebase. Most importantly, while Jamulus is using Qt as a cross-platform library, JammerNetz uses JUCE, massively reducing the lines of code required.

## Licensing

As some substantial work has gone into the development of this, I decided to offer a dual license - AGPL, see the LICENSE.md file for the details, for everybody interested in how this works and willing to spend some time her- or himself on this, and a commercial MIT license available from me on request. Thus I can help the OpenSource community without blocking possible commercial applications.

## Contributing

All pull requests and issues welcome, I will try to get back to you as soon as I can. Due to the dual licensing please be aware that I will need to request transfer of copyright on accepting a PR.

## Special thanks

Special thanks go to our contributors, namely Viktor for helping in creating the first build for macOS!

## About the author

Christof is a lifelong software developer having worked in various industries, and can't stop his programming hobby anyway.
