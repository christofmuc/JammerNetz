# macOS signing credential setup and renewal

This guide creates or renews the GitHub Actions credentials used to sign and
notarize macOS distributions from a Windows machine. Replace prompted values
with the values shown in the relevant Apple Developer and GitHub accounts.
Never put certificate passwords, app-specific passwords, private keys, `.p12`
files, or Base64 certificate contents in the repository.

## Required repository secrets

Configure these GitHub Actions repository secrets:

| Secret | Required value |
|---|---|
| `APPLE_DEVELOPER_IDENTITY` | Complete certificate common name in the form `Developer ID Application: <CERTIFICATE_NAME> (<TEAM_ID>)`. |
| `TEAM_ID` | Ten-character Team ID from Apple Developer **Membership details**. |
| `APPLE_ID` | Apple Account email used for the developer membership and notarization. |
| `APPLE_APP_SPECIFIC_PASSWORD` | App-specific Apple Account password created for the notarization workflow. |
| `MACOS_CERTIFICATE_BASE64` | Single-line Base64 representation of the Developer ID Application `.p12`. |
| `MACOS_CERTIFICATE_PASSWORD` | Export password that protects the `.p12`. |
| `MACOS_TEMPORARY_KEYCHAIN_PASSWORD` | Independent random password used only for the temporary CI keychain. |

GitHub secret values are write-only. Existing values cannot be read back or
copied between repositories. A signed application exposes the certificate
identity and Team ID, but it does not contain the private key or any password.

## Decide whether to reuse or replace the certificate

Reuse an existing Developer ID Application certificate when all of the
following are available:

- the certificate has not expired or been revoked;
- its matching private key is available;
- both are stored together in an encrypted `.p12` file;
- the `.p12` export password is known.

If any item is missing, create another Developer ID Application certificate.
Do not revoke a still-valid certificate merely to create a new one: previously
distributed software may depend on it. A Developer ID Installer certificate is
not required for a distribution made from application bundles, ZIP archives,
and a DMG rather than a signed installer package.

## Collect the non-secret account values

1. Sign in at <https://developer.apple.com/account/>.
2. Open **Membership details** and record the ten-character Team ID.
3. Record the Apple Account email used to access that membership.
4. The certificate identity will have this form after the certificate is
   issued:

   ```text
   Developer ID Application: <CERTIFICATE_NAME> (<TEAM_ID>)
   ```

## Create an encrypted private key and certificate request on Windows

These commands are compatible with Windows PowerShell 5.1. Open PowerShell and
collect the values without placing them directly in command history:

```powershell
$jnProjectName = Read-Host 'Project name'
$jnRepository = Read-Host 'GitHub repository (OWNER/REPOSITORY)'
$jnAppleId = Read-Host 'Apple Account email'
$jnCertificateName = Read-Host 'Apple Developer certificate name'
$jnTeamId = Read-Host 'Apple Developer Team ID'
$jnCountry = Read-Host 'Two-letter country code'

$jnSigningDir = Join-Path `
    ([Environment]::GetFolderPath('MyDocuments')) `
    "AppleSigning\$jnProjectName"
New-Item -ItemType Directory -Force -Path $jnSigningDir | Out-Null
Set-Location $jnSigningDir

$jnOpenSSL = 'C:\Program Files\OpenSSL-Win64\bin\openssl.exe'
if (-not (Test-Path -LiteralPath $jnOpenSSL)) {
    throw "OpenSSL was not found at $jnOpenSSL"
}
```

Generate a random password for the encrypted private key and `.p12`. This does
not print the password. Save the clipboard value in a password manager before
continuing:

```powershell
$jnPasswordBytes = New-Object byte[] 24
$jnRng = [Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $jnRng.GetBytes($jnPasswordBytes)
}
finally {
    $jnRng.Dispose()
}
$jnP12Password = [Convert]::ToBase64String($jnPasswordBytes)
$jnP12Password | Set-Clipboard
```

Generate an encrypted 2048-bit RSA private key. PowerShell requires the `&`
call operator because the executable path is held in a variable. Paste the
clipboard password when OpenSSL prompts; entered password characters are not
displayed:

```powershell
& $jnOpenSSL genrsa -aes256 -out .\DeveloperIDApplication.key 2048
```

Generate the certificate signing request and paste the same private-key
password when prompted:

```powershell
& $jnOpenSSL req -new -sha256 `
    -key .\DeveloperIDApplication.key `
    -out .\DeveloperIDApplication.certSigningRequest `
    -subj "/emailAddress=$jnAppleId/CN=$jnCertificateName/C=$jnCountry"
```

Keep the encrypted private key secure. It is required to create the `.p12` and
may be needed again for recovery.

## Issue the Developer ID Application certificate

1. Open <https://developer.apple.com/account/resources/certificates/add>.
2. Under **Software**, select **Developer ID** and continue.
3. Select **Developer ID Application**, not Developer ID Installer.
4. Upload `DeveloperIDApplication.certSigningRequest`.
5. Download the issued `.cer` certificate.

Convert the downloaded DER certificate to PEM. Enter its full path when
prompted:

```powershell
$jnCerPath = Read-Host 'Full path to the downloaded .cer certificate'
& $jnOpenSSL x509 -inform DER `
    -in $jnCerPath `
    -out .\DeveloperIDApplication.pem
```

Inspect the public certificate before packaging it:

```powershell
& $jnOpenSSL x509 `
    -in .\DeveloperIDApplication.pem `
    -noout -subject -serial -dates
```

Confirm that the subject name and Team ID match the intended developer
membership and that the validity dates are current.

## Export the certificate and private key as `.p12`

Construct the exact signing identity and export the `.p12`:

```powershell
$jnIdentity = "Developer ID Application: $jnCertificateName ($jnTeamId)"

& $jnOpenSSL pkcs12 -export `
    -inkey .\DeveloperIDApplication.key `
    -in .\DeveloperIDApplication.pem `
    -out .\DeveloperIDApplication.p12 `
    -name $jnIdentity
```

OpenSSL first requests the private-key password and then the `.p12` export
password twice. Paste the saved password for all prompts. The export password
must exactly match `MACOS_CERTIFICATE_PASSWORD`.

## Configure the certificate and identity secrets

Set the account and identity values:

```powershell
$jnAppleId | gh secret set APPLE_ID --repo $jnRepository
$jnTeamId | gh secret set TEAM_ID --repo $jnRepository
$jnIdentity | gh secret set APPLE_DEVELOPER_IDENTITY --repo $jnRepository
```

Set the `.p12` password without printing it:

```powershell
$jnP12Password |
    gh secret set MACOS_CERTIFICATE_PASSWORD --repo $jnRepository
```

Encode the `.p12` directly into the repository secret without creating a
Base64 text file:

```powershell
[Convert]::ToBase64String(
    [IO.File]::ReadAllBytes(
        (Resolve-Path .\DeveloperIDApplication.p12)
    )
) | gh secret set MACOS_CERTIFICATE_BASE64 --repo $jnRepository
```

Generate an unrelated password for the runner's temporary keychain and send it
directly to GitHub:

```powershell
$jnKeychainBytes = New-Object byte[] 32
$jnRng = [Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $jnRng.GetBytes($jnKeychainBytes)
}
finally {
    $jnRng.Dispose()
}

[Convert]::ToBase64String($jnKeychainBytes) |
    gh secret set MACOS_TEMPORARY_KEYCHAIN_PASSWORD --repo $jnRepository
```

## Create the notarization password

1. Sign in at <https://account.apple.com/>.
2. Open **Sign-In and Security**.
3. Select **App-Specific Passwords**.
4. Generate a password labelled for the project's GitHub notarization job.
5. Copy the generated password exactly, including hyphens. Do not use the
   normal Apple Account password.
6. Immediately store the clipboard value:

   ```powershell
   Get-Clipboard |
       gh secret set APPLE_APP_SPECIFIC_PASSWORD --repo $jnRepository
   Set-Clipboard -Value ''
   ```

Resetting the primary Apple Account password revokes existing app-specific
passwords. Generate and store a replacement before the next distribution run
if that occurs.

## Verify and test

Confirm that all seven secret names exist. GitHub will not display their
values:

```powershell
gh secret list --repo $jnRepository
```

Run the signed-distribution workflow and check that it completes all of these
stages:

1. import the `.p12` into the temporary keychain;
2. find the exact Developer ID Application identity;
3. store the `notarytool` credentials;
4. sign and verify every application and plug-in bundle;
5. notarize the DMG and plug-in ZIP archives;
6. staple and validate the notarization tickets;
7. pass Gatekeeper assessment for the DMG and signature/ticket validation for
   the non-application plug-in bundles;
8. upload the notarized artifacts.

After a successful run, download and test the artifacts on an Apple Silicon
Mac. Keep encrypted backups of the `.key` and `.p12` plus the `.p12` password.
Do not keep unencrypted private keys, certificate Base64 files, or credentials
inside a source checkout.

## Renewal checklist

When the certificate approaches expiration:

1. keep the previous certificate and its encrypted backup;
2. create a new private key and Developer ID Application certificate;
3. export a new encrypted `.p12`;
4. replace `MACOS_CERTIFICATE_BASE64` and
   `MACOS_CERTIFICATE_PASSWORD` together;
5. update `APPLE_DEVELOPER_IDENTITY` only if its common name changes;
6. verify the Apple Account, Team ID, and app-specific password;
7. run a complete signed-distribution build before the old certificate
   expires;
8. archive the new encrypted materials and record the certificate expiration
   date in the password manager.
