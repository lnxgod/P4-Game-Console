"""Shared selection policy for normal bundles and explicit developer installs."""


def development_only(manifest: dict) -> bool:
    # A WIP folder is never a release opt-in, even if enabled was left true.
    return manifest.get("enabled") is not True or manifest.get("folder") == "GAMES/WIP"
