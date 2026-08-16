# macOS signed distribution

JammerNetz macOS distribution builds target Apple Silicon only. Regular macOS
CI remains unsigned and is intended for development. The
`macOS Signed Distribution` workflow produces the artifacts suitable for
sharing with other users:

- a signed, notarized, and stapled standalone-client DMG;
- a signed, notarized, and stapled VST3 bundle inside a ZIP archive;
- a signed, notarized, and stapled AUv2 component inside a ZIP archive.

The distribution workflow runs only when started manually or for JammerNetz's
semantic release and release-candidate tags. This keeps Developer ID
credentials out of ordinary pull-request
jobs and avoids submitting every development build to Apple's notarization
service.

## Required GitHub Actions secrets

Configure these repository secrets under **Settings > Secrets and variables >
Actions**. They intentionally use the same names as the KnobKraft Orm signing
workflow so the values can be transferred without inventing a second naming
scheme.

| Secret | Value |
|---|---|
| `MACOS_CERTIFICATE_BASE64` | Base64 representation of the exported Developer ID Application `.p12` file. |
| `MACOS_CERTIFICATE_PASSWORD` | Password used when exporting that `.p12`. |
| `MACOS_TEMPORARY_KEYCHAIN_PASSWORD` | A strong arbitrary password used only for the runner's temporary keychain. |
| `APPLE_DEVELOPER_IDENTITY` | Full certificate identity, for example `Developer ID Application: Name (TEAMID)`. |
| `APPLE_ID` | Apple ID belonging to the developer account. |
| `APPLE_APP_SPECIFIC_PASSWORD` | App-specific password created for notarization; never use the normal Apple ID password. |
| `TEAM_ID` | Ten-character Apple Developer team identifier. |

On this Windows development machine, create the certificate secret without
writing its value to the repository:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes('DeveloperIDApplication.p12')) | Set-Clipboard
```

Paste the clipboard contents directly into the GitHub secret and keep the
original `.p12` outside the repository.

## Running a distribution

Open **Actions > macOS Signed Distribution > Run workflow** and select the
branch or tag to build. A release tag such as `2.4.0` or `2.4.0-rc1` also
starts the workflow automatically. Successful runs upload one `JammerNetz-macOS-notarized`
artifact containing the DMG and the two plug-in ZIPs.

The workflow imports the certificate into an ephemeral keychain, stores the
notarization credentials as a temporary `notarytool` keychain profile, signs
nested code before its containing bundle with the hardened runtime enabled,
and removes the keychain even when the job fails. The plug-in build also
copies and rewrites non-system runtime dependencies such as TBB into each
bundle before signing, so the result does not depend on paths from the CI
machine. The workflow never uploads the certificate or keychain.

Before upload, CI verifies the signatures with `codesign`, submits the DMG and
both plug-in ZIPs with `notarytool`, staples and validates the resulting
tickets, and asks Gatekeeper to assess the DMG. Audio plug-in bundles are not
applications, so their final validation uses `codesign` and `stapler` rather
than Gatekeeper's executable-app assessment.
