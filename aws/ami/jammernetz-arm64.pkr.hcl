packer {
  required_version = ">= 1.16.0, < 2.0.0"

  required_plugins {
    amazon = {
      source  = "github.com/hashicorp/amazon"
      version = ">= 1.8.2, < 2.0.0"
    }
  }
}
variable "aws_region" {
  type    = string
  default = "eu-central-1"
}

variable "builder_instance_type" {
  type    = string
  default = "t4g.small"
}

variable "source_commit" {
  type    = string
  default = "unknown"
}

variable "source_commit_short" {
  type    = string
  default = "unknown"
}

variable "source_repository" {
  type    = string
  default = "christofmuc/JammerNetz"
}

variable "workflow_run_id" {
  type    = string
  default = "local"
}

variable "created_at" {
  type    = string
  default = "unknown"
}

variable "build_timestamp" {
  type    = string
  default = "local"
}

locals {
  ami_name = "jammernetz-server-arm64-${var.source_commit_short}-${var.build_timestamp}"
  common_tags = {
    Name             = local.ami_name
    Application      = "JammerNetzServer"
    Architecture     = "arm64"
    OS               = "Ubuntu 24.04 LTS"
    SourceCommit     = var.source_commit
    SourceRepository = var.source_repository
    GitHubRunId      = var.workflow_run_id
    CreatedAt        = var.created_at
    ManagedBy        = "Packer"
  }
}

source "amazon-ebs" "jammernetz_arm64" {
  ami_name      = local.ami_name
  instance_type = var.builder_instance_type
  region        = var.aws_region
  ssh_username  = "ubuntu"
  ssh_timeout   = "15m"

  source_ami_filter {
    filters = {
      architecture        = "arm64"
      name                = "ubuntu/images/hvm-ssd-gp3/ubuntu-noble-24.04-arm64-server-*"
      root-device-type    = "ebs"
      virtualization-type = "hvm"
    }
    most_recent = true
    owners      = ["099720109477"]
  }

  imds_support = "v2.0"

  launch_block_device_mappings {
    device_name           = "/dev/sda1"
    delete_on_termination = true
    encrypted             = true
    volume_size           = 12
    volume_type           = "gp3"
  }

  tags          = local.common_tags
  run_tags      = local.common_tags
  snapshot_tags = local.common_tags
}

build {
  name    = "jammernetz-server-arm64"
  sources = ["source.amazon-ebs.jammernetz_arm64"]

  provisioner "file" {
    source      = "staging/JammerNetzServer"
    destination = "/tmp/JammerNetzServer"
  }

  provisioner "file" {
    source      = "jammernetz-server.service"
    destination = "/tmp/jammernetz-server.service"
  }

  provisioner "shell" {
    script = "scripts/provision.sh"
  }

  provisioner "shell" {
    script = "scripts/smoke-test.sh"
  }

  post-processor "manifest" {
    output     = "packer-manifest.json"
    strip_path = true
    custom_data = {
      architecture      = "arm64"
      created_at        = var.created_at
      os                = "Ubuntu 24.04 LTS"
      source_commit     = var.source_commit
      source_repository = var.source_repository
      workflow_run_id   = var.workflow_run_id
    }
  }
}
