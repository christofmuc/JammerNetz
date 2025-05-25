## ARM64 Server AMI Build Process

This project includes an automated workflow for building an ARM64 Amazon Machine Image (AMI) for the JammerNetz Server. This AMI can then be used to deploy the server on ARM64-based EC2 instances.

### Overview

The build process is orchestrated by a GitHub Actions workflow defined in `.github/workflows/arm64_ami.yaml`. The workflow performs the following key steps:

1.  **Sets up an ARM64 Build Environment**: Utilizes QEMU to enable ARM64 emulation on an x86_64 GitHub Actions runner.
2.  **Installs Cross-Compilation Tools**: Installs `g++-aarch64-linux-gnu` and other necessary dependencies for cross-compiling for ARM64.
3.  **Builds Third-Party Libraries**: Cross-compiles essential third-party libraries (e.g., `flatbuffers`, `sentry-native`) for ARM64 using the `arm64-linux.toolchain.cmake` toolchain file.
4.  **Builds JammerNetz Server**: Cross-compiles the JammerNetz Server application for ARM64.
5.  **Prepares Files for Packaging**: Stages the compiled server binary, a systemd service file (`JammerNetz.service`), and a setup script (`ami-setup.sh`) in a `packer_staging` directory.
6.  **Packages AMI using Packer**: Uses HashiCorp Packer and the `jammernetz-arm64.pkr.hcl` template to:
    *   Launch a source ARM64 EC2 instance (typically an Ubuntu 20.04 LTS ARM64 AMI).
    *   Copy the staged files from `packer_staging/` to the instance.
    *   Execute the `ami-setup.sh` script on the instance. This script installs runtime dependencies, copies the server binary and systemd service to their final locations, and enables the service.
    *   Create a new AMI from the provisioned instance.

### Prerequisites

To successfully run the ARM64 AMI build workflow, the following GitHub secrets must be configured in your repository:

*   `AWS_ACCESS_KEY_ID`: Your AWS Access Key ID.
*   `AWS_SECRET_ACCESS_KEY`: Your AWS Secret Access Key.
*   `AWS_REGION` (optional): The AWS region where the AMI will be built and stored. The workflow defaults to `us-east-1`. If you intend to use a different region, ensure this secret is set, and the Packer template (`jammernetz-arm64.pkr.hcl`) variable `aws_region` is updated accordingly if it doesn't use this secret.

### Resulting AMI

The workflow generates an AMI with a name following the pattern: `jammernetz-server-arm64-{{timestamp}}`, where `{{timestamp}}` is a UTC timestamp in the format `YYYYMMDDHHMMSS` (e.g., `jammernetz-server-arm64-20231027143000`).

The JammerNetz Server within the AMI is configured to start automatically on boot using a systemd service named `JammerNetz.service`.

### Key Files

The main files involved in this build process are:

*   `.github/workflows/arm64_ami.yaml`: The GitHub Actions workflow definition.
*   `jammernetz-arm64.pkr.hcl`: The Packer template used to define and build the AMI.
*   `arm64-linux.toolchain.cmake`: The CMake toolchain file used for cross-compiling the server and its dependencies for ARM64.
*   `packer_staging/ami-setup.sh`: The shell script executed by Packer on the EC2 instance during AMI creation to set up the server environment and service.

This documentation should provide a good overview for developers looking to understand or use the ARM64 AMI build process.
