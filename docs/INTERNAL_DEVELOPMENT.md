# Private development copy

This private repository continues the owner's public
[P4 Game Console project](https://github.com/lnxgod/P4-Game-Console).
It retains upstream Git history through
`51aefe10080498c3e95e9b78a8b0ecd553e00b5f`, the prepared skill and installation
changes, and a captured snapshot of the active Tab5 firmware work. The original
public repository and its active local checkout remain separate.

Preserve upstream authorship, component licenses and game-data notices. This
import does not change ownership or third-party licensing. Generated firmware,
SDKs, local game data and factory recovery images remain outside Git. The
existing owner-authorized Pure Hades pack retains its original notices.

## Clone and install

OpenAI colleagues have Write access through the `all-openai.com` GitHub team.
The repository remains private and requires sign-in with an OpenAI GitHub
account. Team members can push contribution branches and open pull requests;
this access also covers its CI releases and download assets.

Sign in to GitHub CLI with an account that can read this private repository,
then follow [prebuilt installation](INSTALL_PREBUILT.md). The lightweight clone
keeps source, runtime assets, licenses and repository skills while deferring
large art-authoring images and full history. Use the optional art-source archive
or `git sparse-checkout disable` when artwork development needs those inputs.

The Tab5 workflow builds the pinned SDK and standard native game bundle, then
publishes a candidate tied to the exact successful `main` commit. Release assets
stay in this private repository. Pull-request builds upload candidates without
publishing releases. The downloader requires access to the private repository
and never substitutes an upstream or older image for a missing build.

Prebuilt firmware avoids local compilation. It does not supply Doom/Chex data
or authorize writing a device. Current Tab5 content still requires microSD;
preserve existing recovery artifacts and guarded installation checks. Do not
create or refresh firmware backups as part of flashing; backups run only on
explicit user request and are never a flashing prerequisite. Recovery can
rebuild old source.

## Validation limits

Imported hardware records retain their original artifact and unit bindings.
They do not qualify a newly compiled successor. Keep local host tests, firmware
builds, completed GitHub CI and physical gameplay acceptance distinct. A
configured workflow is not a completed release until its run succeeds.
