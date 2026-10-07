# Install a precompiled Tab5 build

Use this route to install or update M5Stack Tab5 Console OS without compiling
firmware or native games. Current game storage requires microSD. The maintained
target is ESP32-P4 revision 1.x with 16 MiB flash; other hardware requires its
own reviewed workflow.

## Download less source

The [root setup instructions](../README.md#set-up-a-tab5) use
`scripts/clone-lite.sh`: a depth-one, blob-filtered clone with sparse checkout
configured before materializing files. It omits only game asset PNGs and design
PNG/GIF galleries. Converted artwork, native C, game manifests, resource
sidecars, licenses, provenance and repository skills remain available.

At the initial measured source revision, tracked files shrink from 279.20 MiB
to 66.23 MiB; the compressed source-lite archive is 22.30 MiB. These are source
measurements, not timed network benchmarks. Ordinary full clones still contain
the original artwork and history.

To restore authoring media or history in a lite clone:

```sh
git sparse-checkout disable  # Downloads the omitted original artwork
git fetch --unshallow        # Downloads older history only when needed
```

Each CI release also offers matching `source-lite` and `art-source` archives.
The art archive includes notices and authoring context; overlay it only on the
source archive for the same complete commit. The generated `.p4-source.json`
stamp lets the downloader identify a source archive without Git metadata.
Never change that stamp to obtain firmware from a different revision.

## Prepare the precompiled files and USB tools

For an online download, install GitHub CLI (`gh`) alongside Git and Python 3,
and sign in to an account with access to the private
`openai/P4-Game-Console` repository:

```sh
gh auth login --hostname github.com
```

From the repository root:

```sh
make prebuilt
make install-tools
. .tools/install-python/bin/activate
make prepare-game-data
```

`make prebuilt` selects only the complete local source commit. It uses GitHub
CLI authentication to read that exact `tab5-<commit>` release in
`openai/P4-Game-Console`, then downloads `tab5-<commit>.tar.gz` and `SHA256SUMS`
through the authenticated GitHub asset API. Metadata and streamed downloads
have size and time limits; each asset must match its recorded byte count.
The downloader verifies the archive, safely stages an allowlisted file inventory
and verifies every file against the manifest. Source, SDK/component locks, game inventory,
image family, partition layout and app/update pairing must match. A valid
cached package is reused. An invalid existing package is preserved and reported.
Cached packages and offline archives require neither GitHub CLI nor network
access. Authentication stays with GitHub CLI; credentials are excluded from
downloader output.

The verified directory is:

```text
build-host/prebuilt/tab5/<40-character-source-commit>/
  manifest.json
  firmware/bootloader/bootloader.bin
  firmware/partition_table/partition-table.bin
  firmware/ota_data_initial.bin
  firmware/p4_console_os.bin
  firmware/P4UPDATE.P4U
  content/GAMES/*.P4G
  content/GAMES/*.P4R          # Only when a standard game needs a sidecar
```

`make install-tools` creates an isolated `.tools/install-python` environment
with esptool 4.12.0 and pyserial 3.5. It reuses a matching environment and
preserves mismatched existing directories. ESP-IDF, its compiler and component
downloads are unnecessary for this installation path.

The package excludes Doom/Chex WADs, Quake PAKs, factory recovery images and
development games. Default setup obtains the exact Doom shareware separately
with the pinned data helper and preserves its notices. Chex requires explicit
selection and SD storage. Keep all downloaded files Git-ignored.

If the exact release is missing, wait for that main-branch CI build or use its
downloaded CI artifact. For an offline artifact, use the SHA-256 from its
`SHA256SUMS` and retain the matching source checkout:

```sh
python3 scripts/fetch-prebuilt.py \
  --archive /absolute/path/tab5-<commit>.tar.gz \
  --sha256 <archive-sha256-from-SHA256SUMS>
```

An access error or missing release stops the download; the downloader does not
try the public upstream, fall back to `latest`, or compile implicitly. A local
firmware change requires a new build. The existing source-build route remains
`make setup`, `make verify`, `make prepare-game-data`,
`python3 scripts/doom/arena-content.py --fetch`, then
`make console-os-tab5-idf` (use Python 3.10 or newer for the source tools).
SDK setup now uses a shallow pinned clone and parallel shallow submodules,
and reuses a discovered matching SDK and the existing project tools cache.
Exact tag, commit, gitlinks, cleanliness and tool overrides remain enforced.

## Install through the guarded workflow

A downloadable build does not authorize a flash. Establish the exact-unit
binding and follow the [Tab5 installation guide](boards/M5STACK_TAB5.md). Do not
create or refresh a firmware backup as part of flashing, including on a new
unit; backups run only as a separately requested operation and are never a
flashing prerequisite. Preserve existing recovery artifacts; recovery can rebuild
old source. A/B/C select entries in the reviewed authorization. Every unit,
including a new Tab5, needs its own confirmed model and hashed live identity.

Use the same digest-bound local authorization as a source-built install, with
an additional `prebuilt_manifest_sha256` equal to the verified package's
`manifest.json` SHA-256. Keep the existing four canonical artifact path keys
under `apps/console_os/build-tab5/`; their byte lengths and SHA-256 values must
match the corresponding files in the fetched `firmware/` directory. Review
the exact feature flags and any app-only predecessor build-artifact binding.
No firmware backup or backup manifest is required. No release manifest
contains or replaces local device authorization.

```sh
python scripts/flash-console-os-tab5.py \
  --prebuilt build-host/prebuilt/tab5/<commit> \
  --authorization /absolute/path/reviewed-authorization.json \
  --authorization-sha256 <reviewed-authorization-sha256> \
  --unit A
```

This performs local checks only. Add the authorized explicit `--port` and
`--install` for a write. The installer neither reads nor creates firmware
backups, and retains model, identity, security, revision, capacity, artifact and
app-only predecessor/partition/active-slot gates. First-layout installation
requires no old firmware; app-only predecessors use reviewed build artifacts.
Routine writes use device checksum verification; optional full readback remains
available for recovery or diagnostics. Receipts bind the prebuilt manifest too.

After the authorized OS install, or for a compatible existing OS, use one
connection to transfer the standard games and skip identical installed files:

```sh
python scripts/p4-transfer.py push-bundle \
  build-host/prebuilt/tab5/<commit>/content --port <explicit-port>
python scripts/p4-usb-content.py doom --port <explicit-port>
```

Game-only updates do not need an OS reflash. Preserve saves and preferences.
For an OS update through the running console, use the existing reviewed
update workflow with `firmware/P4UPDATE.P4U`; downloading it alone does not
perform an update.

## CI builds and evidence

[The workflow](../.github/workflows/tab5-build.yml) builds the full Tab5 target
from pinned tools and committed component locks, runs the repository verifier,
and exports the standard native bundle. SDK/component and compiler caches are
keyed by the locks and platform. PRs produce downloadable build artifacts;
successful trusted `main` builds publish commit-specific prereleases in the
repository running the workflow, with checksums and paired source archives.
The publisher refuses to replace existing
assets. GitHub's server-enforced release immutability is a separate repository
setting; the workflow does not enable it.

The exporter requires clean committed source and runs the full SDK/ELF verifier.
The portable consumer rechecks binary/package integrity and the recorded build
evidence without shipping a compiler or ELF. These are build candidates with
`hardware_verified=false` and `flash_authorized=false`. Installation receipts,
actual-device gameplay, peripheral behavior and frame cadence remain separate
acceptance evidence.

Generated firmware stays in ignored build directories and release assets.
Committing binaries would make future clones larger. No published history is
rewritten by this cleanup; the lite routes avoid downloading unnecessary blobs.
