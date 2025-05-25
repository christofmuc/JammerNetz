packer {
  required_plugins {
    amazon = {
      version = ">= 1.0.0"
      source  = "github.com/hashicorp/amazon"
    }
  }
}

variable "aws_region" {
  type    = string
  default = "us-east-1"
}

variable "ami_name_prefix" {
  type    = string
  default = "jammernetz-server-arm64"
}

// Ubuntu Server 20.04 LTS (HVM), SSD Volume Type, ARM64
// This AMI ID is for us-east-1. Change if using a different region.
// You can find the latest AMIs here: https://cloud-images.ubuntu.com/locator/ec2/
variable "source_ami_arm64" {
  type    = string
  default = "ami-069aab8917536a518" // Ubuntu 20.04 LTS ARM64 for us-east-1
}

variable "instance_type_arm64" {
  type    = string
  default = "t4g.small" // Choose a cost-effective ARM64 instance type
}

source "amazon-ebs" "jammernetz-arm64" {
  ami_name        = "${var.ami_name_prefix}-{{timestamp}}"
  instance_type   = var.instance_type_arm64
  region          = var.aws_region
  source_ami      = var.source_ami_arm64
  ssh_username    = "ubuntu" // Default username for Ubuntu AMIs
  force_deregister = true    // Deregister any existing AMI with the same name (clean up old builds)
  force_delete_snapshot = true // Delete snapshot with AMI

  tags = {
    Name        = "${var.ami_name_prefix}"
    OS_Version  = "Ubuntu"
    Architecture = "arm64"
    CreatedBy   = "Packer"
    Timestamp   = "{{timestamp}}"
  }
}

build {
  sources = ["source.amazon-ebs.jammernetz-arm64"]

  provisioner "file" {
    source      = "packer_staging/" // This path is relative to workflow execution dir where packer_staging is created
    destination = "/tmp/packer_files/"             // Trailing slash is important for directories
  }

  provisioner "shell" {
    script = "/tmp/packer_files/ami-setup.sh"
    execute_command = "sudo {{.Path}}" // Run the script with sudo
  }
}
