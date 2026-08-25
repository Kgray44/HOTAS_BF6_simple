# Curated release history

The JSON files in this directory are the authoritative source for historical GitHub Release notes and release-status warnings.

## Release status policy

| Release | Status | Guidance |
| --- | --- | --- |
| v1.9.3 | Current | Recommended published release |
| v1.9.2 | **Deprecated** | **Do not use for new setup; upgrade to v1.9.3+** |
| v1.9.1 | **Deprecated** | **Do not use Automatic Repair; upgrade to v1.9.3+** |
| v1.9.0 | **Deprecated** | **Do not use; upgrade to v1.9.3+** |
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
| v1.5 | Historical source release | Retroactive notes only; no installer asset |
| v1.4 | Historical source release | Retroactive notes only; no installer asset |
| v1.3 | Historical source release | Retroactive notes only; no installer asset |
| v1.2 | Historical source release | Retroactive tag/Release at verified v1.2 baseline; no installer asset |
| v1.1 | Historical source release | Retroactive notes only; no installer asset |
| v1.0 | Pre-repository milestone | No exact Git source snapshot exists; do not fabricate a tag/Release |

## Rules

- GitHub Release titles are **always exactly the version tag**, for example `v1.9.2`.
- Existing annotated Git tags and release assets are preserved; the synchronizer updates only GitHub Release title/body metadata.
- Missing Releases for verified historical tags may be created retroactively with curated notes, but **no installer/binary asset is fabricated**.
- v1.2 is the one missing historical tag that can be reconstructed safely because commit `c020e6b350ed43addf5a6090087f70c9701750f1` is the exact v1.2 baseline immediately preceding the v1.3 version bump.
- v1.0 remains documentation-only because the repository begins at v1.1; creating a v1.0 Git tag would invent a source snapshot that Git does not contain.
- `deprecated` means **do not install or use** and generates a red caution block with the replacement version.
- `superseded` identifies a real known issue without pretending the entire release is unusable for every historical purpose.
- `historical` and `historical-stable` remain documented without being mislabeled as pre-releases.
- `current` identifies the recommended published release.
- The synchronizer never rewrites existing tag targets and never touches attached release assets.

The `Release Notes Sync` workflow validates these records, creates missing historical Releases only where explicitly authorized, and synchronizes all curated GitHub Release titles/bodies whenever this directory changes on `main`.
