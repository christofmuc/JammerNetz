## Introduction

JammerNetz is an Open Source system for "networked music performance" (NMP), also known as "jamming over the internet". It consists of a lightweight server software, and a little UI client software that can use your audio devices and stream multiple uncompressed high-quality audio channels to other participants as a common mixdown.

The design choice is that if you have a modern DSL or fibre connection, there is no need to degrade your music experience using CODECs mostly designed for speech transmission as used in many other internet jam solutions. We are happily using JammerNetz since months for our weekly sessions with synths, voice, and electric guitar.

Of course be aware that the main influence on the quality has your internet provider and your choice where you are running the server. If you have high quality fibre and a good and fast ASIO audio interface, you can expect 30ms total latency, which we think is awesome, but if some participants are bound for cable internet and are trying to play together across all of Germany, even running the server in a suitable AWS instance in Frankfurt close to the internet's main hub you will not get much better than 70-80 ms total audio latency (not round trip time), which we feel stll is worth the effort!

## Features

JammerNetz allows you to

  * Host a jam session on a server, allowing a configurable number of clients to participate.
  * Allows for channel configuration to send mono or stereo channels, and also a "send-only" channel type for using microphones without hearing your own voice. This makes for a great communication during the session. 
  * Send multiple audio channels per client to the server, e.g. your synthesist can send the keyboards and his voice separately.
  * Does automatic hard-disk recording of your session to local disk on each client in a lossless compressed FLAC file.
  * Does automatic MIDI recording in case it detects any incoming MIDI notes, thereby logging all keys played into a MIDI file for later revisit ("what did I play? Sounds great!")

### Limitations

It should be noted that due to the design of the system, we have a few limitations or restrictions that other systems might not have. We believe that we have made sensible trade-offs, but your milage may vary:

  1. Only ASIO drivers are currently supported by the JammerNetz Client. As we are aiming for lowest-possible latency, you should really use an Audio device with ASIO drivers. 
  2. All clients need to run on the same sample rate (48000 is set in the source, but you might want to switch to 44100), else mixing the signals together isn't straightforward.
  3. As of today, even the buffer size used by the device must be identical. The file BuffersConfig.h has the constants and is currently set at 128 samples.

## Usage

As of today, the system is still in a built-and-run-yourself state. You will need some experience in compiling a C++ application, getting the required third party binaries and repos, starting an AWS instance and deploying the Linux build of the server there to start it. Depending on the interest in this system, we might be able to provide more help. 

## Building the software

### Prerequisites

We use the magnificent JUCE library to immensly reduce the amount of work we have to do. You can download it for free from [JUCE.com](https://juce.com/).

Recursive checkout with Git submodules is required to retrieve the nice JUCE module [ff_meters](https://github.com/ffAudio/ff_meters) from ffAudio into the correct directory.

In addition, you will need the [Steinberg ASIO SDK](https://www.steinberg.net/en/company/developers.html), which can be downloaded from their webseite. Here is a direct link: https://www.steinberg.net/asiosdk. Unzip the contents into the third_party\ASIOSDK directory.

For the multithreading aspects of the application, we use the [Intel Threading Building Blocks](https://software.intel.com/en-us/tbb) library. Download the matching binary release from github [here](https://github.com/intel/tbb/releases). Unzip the file downloaded into the third_party subdirectory. We have used version tbb2019_20190605oss. If you have a different version, you need to adapt the paths in the Projucer exporter later.

As we don't want to send any unecrpyted audio data through the internet, we use a simple BlowFish encryption scheme to make sure that only authorized people join the jam session. For now, we have simply compiled a shared secret into server and client, more elaborate schemes might follow.

To generate the shared secret, create a file named RandomNumbers.bin in the &lt;JammerNetzDir&gt;\common subdirectory. For example, you can use an external source like (https://www.random.org/bytes/) to generate 72 random bytes, or use a more trustworthy key source as you choose.

### Supported platforms

JUCE is a cross-platform library with support for all major platforms, but we have tested the system at the moment only on Windows 10 using MS Visual Studio 2017, and the server on an up-to-date Ubuntu Linux 18. Other platforms might work as well, but might require some fiddling and fixing.

### Building the server

On Windows using Visual Studio 2017

  1. After downloading the JUCE library and unzipping it, start the provided "Projucer" application. This is the JUCE IDE which you will use to generate the build files for the target.
  2. The Projucer file is preloaded with "Exporters" for MS Visual Studio 2017 and a Linux makefile. Right click the "Visual Studio 2017" and select "Save this exporter". This will generate a Visual Studio 2017 solution file.
  3. Open the generated &lt;JammerNetzDir&gt;\Server\Builds\VisualStudio2017\JammerNetzServer.sln file in Visual Studio. You can now select Debug or Release config and build and run it normally with the IDE.

Other targets:

  1. We create the Linux build of the server using cross-platform compilation from my Windows machine. See below for more instructions on that using Docker.
  2. A native Linux makefile can also be created using the Projucer on Ubuntu, as well as Mac OS support should be theoretically possible.


### Building the client

On Windows using Visual Studio 2017

  1. Start Projucer and open the file &lt;JammerNetzDir&gt;\Client\Client.jucer
  2. Save the exporter and open the generated solution file, or just click the Visual Studio icon which will open it for you.
  3. You can now select Debug or Release config and build and run it normally with the IDE.


## Cross-platform building Linux server on Windows 10 using Docker

Thanks to Docker, it has never been that easy to do cross-platform development on Windows. If you don't have it, get yourself [Docker Desktop](https://www.docker.com/products/docker-desktop) and experience the power!

### Building the Linux version from command line

After installing Docker Desktop, be sure to also download the Linux version of JUCE from [here](https://shop.juce.com/get-juce/download) and place the downloaded zip file (don't unzip this!) into the directory &lt;JammerNetzDir&gt;\server\docker\buildmachine.

First, what we are going to do is to create a docker image for the build machine we are going to use. For that, open a command line, cd into the directory &lt;JammerNetzDir&gt;\Server\docker\buildmachine and run the command

    buildBuildmachine.bat

Make sure to have saved the LinuxMakefile exporter from within Projucer before the next step!

You can build the Linux version from a Windows command line by just running the provided batch file inside the buildmachine directory. You need to specify the JammerNetz top level directory in the funky Docker volume syntax with forward slashes and a double slash for the drive letter. E.g. the Windows directory D:\Development\JammerNetz needs to be specfied as //d/Development/JammerNetz:

    makeLinux.bat //d/Development/JammerNetz

This is so the Linux virtual machine will mount the source directory from the host, and directly place it's build output on the host, so the virtual machine is immediately removed again after the build is finished.

You will find the output of the Build machine in the directory &lt;JammerNetzDir&gt;\Server\Builds\LinuxMakefile\build. The server executable is just called "JammerNetzServer" and is ready to run on a matching Linux machine.

### Automatic Linux Release build

You can also enable the cross-platform build in the Release export configuration of the Projucer. Just remove the "rem" from the line

    rem $(ProjectDir)..\..\docker\buildmachine\makeLinux.bat //d/Development/JammerNetz-OS


and save the Exporter again from within Projucer. 

Now, when you compile a release version of the Windows server, after the Windows build is done it will automatically run the Linux build. Neat!

## Similar systems

We had used the great [Jamulus](http://llcon.sourceforge.net/) system before developing our own system, and JammerNetz certainly has been inspired by this great piece of software. We also made some substantial design and architecture changes over Jamulus, justifying a new development instead of contributing to the Jamulus codebase. Most importantly, while Jamulus is using Qt as a cross-platform library, JammerNetz uses JUCE massively reducing the lines of code required. 

Note that as of today Jamulus is in a much more plug'n'play state than JammerNetz, so if you want to quickly try out the concept of a live internet jam session, we strongly recommend you go over and check out Jamulus - they even host a few test servers so you can get up and running in minutes!

## About the author

Christof is a lifelong software developer having worked in various industries, and can't stop his programming hobby anyway. 
