#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPOSITORY_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
VERSION="5.4.8"
ARCHIVE="lua-$VERSION.tar.gz"
ARCHIVE_URL="https://www.lua.org/ftp/$ARCHIVE"
ARCHIVE_SHA256="4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae"
DESTINATION="$REPOSITORY_ROOT/third_party/lua-$VERSION"
MANIFEST="$REPOSITORY_ROOT/third_party/lua-$VERSION.manifest"

if [[ -e "$DESTINATION" || -e "$MANIFEST" ]]; then
    echo "refusing to overwrite existing Lua vendor content" >&2
    exit 2
fi

TEMPORARY="$(mktemp -d)"
trap 'rm -rf "$TEMPORARY"' EXIT

curl --fail --location "$ARCHIVE_URL" --output "$TEMPORARY/$ARCHIVE"
echo "$ARCHIVE_SHA256  $TEMPORARY/$ARCHIVE" | shasum -a 256 -c -
tar -xzf "$TEMPORARY/$ARCHIVE" -C "$TEMPORARY"

STAGING="$TEMPORARY/vendor"
mkdir -p "$STAGING/doc"
cp "$TEMPORARY/lua-$VERSION/README" "$STAGING/README"
cp "$TEMPORARY/lua-$VERSION/doc/readme.html" "$STAGING/doc/readme.html"
cp -R "$TEMPORARY/lua-$VERSION/src" "$STAGING/src"

(
    cd "$STAGING"
    find . -type f -print | LC_ALL=C sort | while IFS= read -r path; do
        digest="$(shasum -a 256 "$path" | awk '{print $1}')"
        printf '%s  %s\n' "$digest" "${path#./}"
    done
) > "$TEMPORARY/lua-$VERSION.manifest"

mv "$STAGING" "$DESTINATION"
mv "$TEMPORARY/lua-$VERSION.manifest" "$MANIFEST"
echo "vendored Lua $VERSION into $DESTINATION"
