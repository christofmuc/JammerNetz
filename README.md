## Introduction

JammerNetz is an Open Source system for "networked music performance" (NMP), also known as "jamming over the internet". It consists of a lightweight server software, and a little UI client software that can use your audio devices and stream multiple uncompressed high-quality audio channels to other participants as a common mixdown.

The design choice is that if you have a modern DSL or fibre connection, there is no need to degrade your music experience using CODECs mostly designed for speech transmission as used in many other internet jam solutions. We are happily using JammerNetz since months for our weekly sessions with synths, voice, and electric guitar.

Of course be aware that the main influence on the quality has your internet provider and your choice where you are running the server. If you have high quality fibre and a good and fast ASIO audio interface, you can expect 30ms total latency, which we think is awesome, but if some participants are bound for cable internet and are trying to play together across all of Germany, even running the server in a suitable AWS instance in Frankfurt close to the internet's main hub you will not get much better than 70-80 ms total audio latency (not round trip time), which we feel stll is worth the effort!

## Usage

As of today, the system is still in a built-and-run-yourself state. You will need some experience in compiling a C++ application, getting the required third party binaries and repos, starting an AWS instance and deploying the Linux build of the server there to start it. Depending on the interest in this system, we might be able to provide more help. 

### Prerequisites

We use the magnificent JUCE library to immensly reduce the amount of work we have to do. You can download it for free from [JUCE.com](https://juce.com/).

In addition, you will need the [Steinberg ASIO SDK](https://www.steinberg.net/en/company/developers.html), which can be downloaded from their webseite. Here is a direct link: (https://www.steinberg.net/asiosdk). Unzip the contents into the third_party\ASIOSDK directory.

For the multithreading aspects of the application, we use the [Intel Threading Building Blocks](https://software.intel.com/en-us/tbb) library. Download the matching binary release from github [here](https://github.com/intel/tbb/releases). Unzip the file downloaded into the third_party subdirectory. We have used version tbb2019_20190605oss. If you have a different version, you need to adapt the paths in the Projucer exporter later.

As we don't want to send any unecrpyted audio data through the internet, we use a simple BlowFish encryption scheme to make sure that only authorized people join the jam session. For now, we have simply compiled a shared secret into server and client, more elaborate schemes might follow.

To generate the shared secret, create a file named RandomNumbers.bin in the <JammerNetzDir>\common subdirectory. For example, you can use an external source like (https://www.random.org/bytes/) to generate 72 random bytes, or use a more trustworthy key source as you choose.

### Supported platforms

JUCE is a cross-platform library with support for all major platforms, but we have tested the system at the moment only on Windows 10 using MS Visual Studio 2017, and the server on an up-to-date Ubuntu Linux 18. Other platforms might work as well, but might require some fiddling and fixing.

### Building the server

On Windows using Visual Studio 2017

    1. After downloading the JUCE library and unzipping it, start the provided "Projucer" application. This is the JUCE IDE which you will use to generate the build files for the target.
    2. The Projucer file is preloaded with "Exporters" for MS Visual Studio 2017 and a Linux makefile. Right click the "Visual Studio 2017" and select "Save this exporter". This will generate a Visual Studio 2017 solution file.
    3. Open the generated Server\Builds\VisualStudio2017\JammerNetzServer.sln file in Visual Studio. You can now select Debug or Release config and build and run it normally with the IDE.

Other targets:

    1. We create the Linux build of the server using cross-platform compilation from my Windows machine. See below for more instructions on that using Docker.
    2. A native Linux makefile can also be created using the Projucer on Ubuntu, as well as Mac OS support should be theoretically possible.

## Cross-platform building Linux server on Windows 10 using Docker

Thanks to Docker, it has never been that easy to do cross-platform development on Windows. If you don't have it, get yourself [Docker Desktop](https://www.docker.com/products/docker-desktop) and experience the power!

### Building the Linux version from command line

After installing Docker Desktop, be sure to also download the Linux version of JUCE from [here](https://shop.juce.com/get-juce/download) and place the downloaded zip file (don't unzip this!) into the directory <JammerNetzDirectory>\server\docker\buildmachine.

First, what we are going to do is to create a docker image for the build machine we are going to use. For that, open a command line, cd into the directory <JammerNetzDirectory>\Server\docker\buildmachine and run the command

    buildBuildmachine.bat

Make sure to have saved the LinuxMakefile exporter from within Projucer before the next step!

You can build the Linux version from a Windows command line by just running the provided batch file inside the buildmachine directory. You need to specify the JammerNetz top level directory in the funky Docker volume syntax with forward slashes and a double slash for the drive letter. E.g. the Windows directory D:\Development\JammerNetz needs to be specfied as //d/Development/JammerNetz:

    makeLinux.bat //d/Development/JammerNetz

This is so the Linux virtual machine will mount the source directory from the host, and directly place it's build output on the host, so the virtual machine is immediately removed again after the build is finished.

You will find the output of the Build machine in the directory <JammerNetzDir>\Server\Builds\LinuxMakefile\build. The server executable is just called "JammerNetzServer" and is ready to run on a matching Linux machine.

### Automatic Linux Release build

You can also enable the cross-platform build in the Release export configuration of the Projucer. Just remove the "rem" from the line

    rem $(ProjectDir)..\..\docker\buildmachine\makeLinux.bat //d/Development/JammerNetz-OS


and save the Exporter again from within Projucer. 

Now, when you compile a release version of the Windows server, after the Windows build is done it will automatically run the Linux build. Neat!