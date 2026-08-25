# Release signing

HOTAS BF6 supports optional Azure Artifact Signing through GitHub OpenID Connect (OIDC).
GitHub does not store a PFX, private key, or Azure client secret for this path.

Signing is an optional trust/reputation enhancement. It is **not** a prerequisite for
building, tagging, or publishing a stable HOTAS BF6 release.

## Current state

The `release-signing` GitHub environment contains the Azure identifiers and
Artifact Signing configuration. Its `ARTIFACT_SIGNING_ENABLED` variable is
currently `false` because the `HOTASBF6Public` Public Trust certificate profile
cannot be created until Microsoft's Individual/Public identity validation
finishes.

While signing is disabled, both manual release dry runs and matching stable `v*`
tag builds continue normally without Azure login, signing, or signature-validity
checks. Stable tag builds still build, test, benchmark, stage the Qt application,
compile and smoke-test the installer, generate manifests and checksums, upload
artifacts, and publish the GitHub Release.

Unsigned builds may trigger Windows reputation, Smart App Control, or other
security warnings. Users should not weaken unrelated Windows security controls to
work around those protections.

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

If signing is disabled, the same release pipeline runs without the signing and
signature-verification steps.

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
must never be distributed to users or represented as a public-trust signature.

## Enable trusted signing later

If Microsoft identity validation is completed and public signing is desired:

1. In Azure Artifact Signing, create a **Public Trust** certificate profile.
2. Name it exactly `HOTASBF6Public` and select the completed verified identity.
3. Keep Program Type appropriate for ordinary Win32 signing.
4. Confirm the existing GitHub signing principal still has signer access.
5. In GitHub's `release-signing` environment, change
   `ARTIFACT_SIGNING_ENABLED` from `false` to `true`.
6. Run a manual release dry run from `main`.
7. Confirm GitHub OIDC login succeeds and the mapper, launcher, and installer
   signatures all verify as valid.
8. Confirm the installer, SHA256SUMS, and update manifest describe the same
   signed installer bytes.

After that, future stable releases will be signed automatically while the flag
remains enabled. If signing is later disabled again, stable releases remain
publishable as unsigned builds.

The Azure subscription, Microsoft Entra application, federated credential,
GitHub environment, and Artifact Signing account are external configuration;
this repository does not create or modify them.
