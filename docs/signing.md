# Release signing

HOTAS BF6 stable-release signing is designed around Azure Artifact Signing and
GitHub OpenID Connect (OIDC). GitHub does not store a PFX, private key, or
Azure client secret for this path.

## Current state

The `release-signing` GitHub environment contains the Azure identifiers and
Artifact Signing configuration. Its `ARTIFACT_SIGNING_ENABLED` variable is
currently `false` because the `HOTASBF6Public` Public Trust certificate profile
cannot be created until Microsoft's Individual/Public identity validation
finishes.

While it is disabled, a manual `workflow_dispatch` release run from `main` is
an **UNSIGNED DRY RUN**. It still builds, tests, benchmarks, stages the Qt
application, compiles and smoke-tests the installer, generates metadata, and
uploads the artifacts. Azure login, signing, and signature-validity checks are
skipped. A pushed stable `v*` tag fails before build rather than publishing an
unsigned public release.

No artifact should be described as Microsoft-signed or publicly trusted until a
real enabled Azure signing run has completed successfully.

## Signed release ordering

When `ARTIFACT_SIGNING_ENABLED=true`, the release workflow uses GitHub OIDC to
authenticate to Azure and follows this immutable-artifact order:

```text
build and test
→ stage HOTAS BF6.exe and HOTAS BF6 Launcher.exe
→ sign and verify those project executables
→ compile the installer from that stage
→ smoke-test the installer
→ sign and verify the final installer
→ generate SHA256SUMS and update-manifest.json
→ upload and publish the exact signed installer
```

Only the project-owned mapper executable, launcher executable, and final
installer are signing targets. The installer is not rebuilt or otherwise
mutated after its signature is applied; manifests and checksums are separate
metadata files derived from its signed bytes.

## Local development signing

Development self-signing is separate from Azure public signing and is optional
for normal local builds. It creates or reuses a non-exportable CurrentUser
certificate named `CN=HOTAS BF6 Development`; it never exports a private key.

```powershell
.\scripts\setup-development-signing.ps1 -TrustForCurrentUser
.\scripts\sign-development-build.ps1 -Path 'C:\path\to\HOTAS BF6.exe', 'C:\path\to\HOTAS BF6 Launcher.exe'
```

`-TrustForCurrentUser` adds only the development certificate's public portion
to the current user's `TrustedPublisher` and `Root` stores. It requires neither
administrator privileges nor any LocalMachine trust change. This certificate
must never be distributed to users or used for a stable public release.

## Enable trusted signing after Microsoft validation

After the individual/public identity is verified:

1. In Azure Artifact Signing, create a **Public Trust** certificate profile.
2. Name it exactly `HOTASBF6Public` and select the completed verified identity.
3. Keep Program Type appropriate for ordinary Win32 signing (currently `None`).
4. Confirm the existing GitHub signing principal still has signer access.
5. In GitHub's `release-signing` environment, change
   `ARTIFACT_SIGNING_ENABLED` from `false` to `true`.
6. Run a manual release dry run from `main`.
7. Confirm GitHub OIDC login succeeds and the mapper, launcher, and installer
   signatures all verify as valid.
8. Confirm the installer, SHA256SUMS, and update manifest describe the same
   signed installer bytes.
9. Only then create the next stable `vX.Y.Z` tag.

The Azure subscription, Microsoft Entra application, federated credential,
GitHub environment, and Artifact Signing account are existing external
configuration; this repository does not create or modify them.
