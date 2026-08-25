# Curated release history

The JSON files in this directory are the authoritative source for historical GitHub Release notes and release-status warnings.

## Release status policy

| Release | Status | Guidance |
| --- | --- | --- |
| v1.9.1 | Current | Recommended published release |
| v1.9.0 | **Deprecated** | **Do not use; upgrade to v1.9.1+** |
| v1.8.5 | Historical stable | Stable v1.8 baseline; newer release recommended |
| v1.8.4 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.3 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.2 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.1 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.0 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.7.0 | Historical stable | Usable historical release |
| v1.6.3 | Historical stable | Stable v1.6 baseline |
| v1.6.2 | Superseded / known issue | Native POV parsing issue; use v1.6.3+ |
| v1.6.1 | Historical | Usable historical release |
| v1.6.0 | Historical | Usable historical release |

Earlier v1.0-v1.5 entries are documented as source/tag milestones and are **not** retroactively turned into packaged GitHub Releases.

## Rules

- GitHub Release titles are **always exactly the version tag**, for example `v1.9.1`.
- Existing annotated Git tags and release assets are preserved; the synchronizer updates only the GitHub Release title/body.
- `deprecated` means **do not install or use** and generates a red caution block with the replacement version.
- `superseded` identifies a real known issue without pretending the entire release is unusable for every historical purpose.
- `historical` and `historical-stable` remain documented without being mislabeled as pre-releases.
- `current` identifies the recommended published release.
- `syncToGitHubRelease: false` preserves early source/tag milestones without inventing retroactive Releases.

The `Release Notes Sync` workflow validates these records and updates every existing GitHub Release from v1.6.0 onward whenever this directory changes on `main`.
