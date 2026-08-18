# JammerNetz ARM64 EC2 AMI

The manual `Publish ARM64 EC2 AMI` workflow builds `JammerNetzServer` on GitHub's native Ubuntu 24.04 ARM64 runner, transfers only the resulting artifact to a separate x64 job, and uses Packer to create a private AMI in the selected AWS region.

The AMI is based on the most recent official Canonical Ubuntu 24.04 ARM64 server image. It contains the server binary, its runtime libraries, a dedicated unprivileged `jammernetz` account, and an enabled `jammernetz-server.service` unit. It never contains a production session key.

## One-time GitHub and AWS setup

1. Configure GitHub's OIDC provider in the AWS account with provider URL `https://token.actions.githubusercontent.com` and audience `sts.amazonaws.com`.
2. Create a dedicated IAM role using `iam/github-oidc-trust-policy.json` as its trust policy and `iam/packer-policy.json` as its permissions policy. Replace every placeholder before applying either document.
3. Create a GitHub environment named `ami-production`.
4. Restrict that environment to the `master` branch and configure a required reviewer if the repository plan supports deployment approvals.
5. Add these environment variables (not long-lived secrets):
   - `AWS_ROLE_ARN`: ARN of the dedicated publishing role.
   - `AWS_ACCOUNT_ID`: expected AWS account ID; the credentials action rejects credentials for any other account.

The publishing role can create only the temporary EC2 resources required by Packer and the resulting AMI. It deliberately has no access to SSM Parameter Store, Secrets Manager, or JammerNetz session keys. Some EC2 creation and cleanup APIs do not support useful resource-level restrictions, so the example policy uses `Resource: "*"` for that action-level permission set. Keep this role dedicated to the AMI workflow and review its CloudTrail activity after the first build.

The policy allows Packer to set only the `ImdsSupport=v2.0` image attribute on
AMIs tagged for JammerNetz. It also permits deregistering those tagged AMIs and
deleting their tagged snapshots when `packer build -on-error=cleanup` needs to
roll back a failed publication. If the policy is managed separately in AWS,
update the attached role policy whenever this checked-in policy changes.

The Packer builder uses the account's default VPC. If the target account has no default VPC, add explicit VPC and subnet variables before running the workflow rather than granting Packer permission to create permanent networking resources.

## Publish an AMI

After this workflow is merged to `master`:

1. Open **Actions > Publish ARM64 EC2 AMI > Run workflow**.
2. Select `master` in **Use workflow from**. The protected publishing workflow
   must always run from this branch.
3. Set **Source ref** to the numeric release tag to publish, such as `2.4.1`,
   or leave it as `master` for an unreleased build. Select the target AWS region
   and an ARM64 builder instance type such as `t4g.small`.
4. Approve the `ami-production` environment deployment when prompted.

The workflow definition and AWS permissions always come from protected
`master`, while the server binary and AMI contents come from **Source ref**.
Starting the workflow itself from a tag is rejected with an explicit error so
the publication job cannot be silently skipped.

The workflow summary reports the AMI ID, region, source commit, architecture, and base OS. The Packer manifest and normal (non-debug) Packer log are retained as a workflow artifact for 30 days. Ordinary pushes and pull requests validate the configuration but never authenticate to AWS or create resources.

The generated AMI remains private to the AWS account. Sharing or making an AMI public requires a separate security and licensing review and is not performed by this workflow.

## Provision the session key

The installed service uses this exact command:

```text
/usr/local/bin/JammerNetzServer -k /etc/jammernetz/secret.key
```

`ConditionPathExists` prevents the enabled unit from starting until that file is present. The required ownership and permissions are:

```text
root:jammernetz 0640 /etc/jammernetz/secret.key
```

For manual testing, stream an existing 72-byte key directly from the operator machine into the protected destination:

```bash
ssh ubuntu@INSTANCE_ADDRESS \
  'sudo install -o root -g jammernetz -m 0640 /dev/stdin /etc/jammernetz/secret.key' \
  < JammerNetz-secret.bin
ssh ubuntu@INSTANCE_ADDRESS 'sudo systemctl start jammernetz-server.service'
```

Do not pass the key itself as a command-line argument or place it in EC2 user data.

For automated deployment, store the base64-encoded key as an SSM SecureString, attach a runtime instance role derived from `iam/runtime-ssm-policy.json`, and adapt `examples/cloud-init-ssm.yaml` with the region and parameter name. The cloud-init example retrieves the value using the instance's temporary role credentials, validates that it decodes to 72 bytes, installs it with restrictive permissions, and starts the service. The publishing role and runtime role must remain separate.

## Verification and troubleshooting

Packer performs these checks before creating the AMI:

- the artifact is an ARM64 executable;
- `ldd` reports no missing runtime libraries;
- the systemd unit starts successfully with an ephemeral key held under `/run`;
- the server opens UDP port 7777;
- the ephemeral key and its `/etc/jammernetz/secret.key` symlink are removed before imaging.

On a launched instance, verify the deployment with:

```bash
sudo systemctl status jammernetz-server.service
sudo journalctl -u jammernetz-server.service
sudo ss -lunp | grep ':7777'
```

The EC2 security group must allow UDP port 7777 from the intended clients. SSH access and its source CIDRs are deployment concerns and are intentionally not configured in the AMI.
