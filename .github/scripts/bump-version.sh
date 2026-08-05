#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# bump-version.sh -- bump the version across CMakeLists.txt, vcpkg.json,
# version.h, and CHANGELOG.md.
#
# Usage: echo "y" | ./.github/scripts/bump-version.sh <new_version>
#   where <new_version> is one of: major, minor, patch, or x.y.z

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

NEW_VERSION="${1:-}"
if [ -z "$NEW_VERSION" ]; then
    echo "Usage: $0 <major|minor|patch|x.y.z>"
    exit 1
fi

# Get current version from vcpkg.json
CURRENT_VERSION=$(python3 -c "
import json, sys
with open('$ROOT_DIR/vcpkg.json') as f:
    print(json.load(f)['version'])
")
echo "Current version: $CURRENT_VERSION"

# Parse current
CUR_MAJOR=$(echo "$CURRENT_VERSION" | cut -d. -f1)
CUR_MINOR=$(echo "$CURRENT_VERSION" | cut -d. -f2)
CUR_PATCH=$(echo "$CURRENT_VERSION" | cut -d. -f3)

# Calculate new version
case "$NEW_VERSION" in
    major)
        NEW_MAJOR=$((CUR_MAJOR + 1))
        NEW_MINOR=0
        NEW_PATCH=0
        ;;
    minor)
        NEW_MAJOR=$CUR_MAJOR
        NEW_MINOR=$((CUR_MINOR + 1))
        NEW_PATCH=0
        ;;
    patch)
        NEW_MAJOR=$CUR_MAJOR
        NEW_MINOR=$CUR_MINOR
        NEW_PATCH=$((CUR_PATCH + 1))
        ;;
    *)
        if ! echo "$NEW_VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
            echo "Error: invalid version: $NEW_VERSION"
            exit 1
        fi
        NEW_MAJOR=$(echo "$NEW_VERSION" | cut -d. -f1)
        NEW_MINOR=$(echo "$NEW_VERSION" | cut -d. -f2)
        NEW_PATCH=$(echo "$NEW_VERSION" | cut -d. -f3)
        ;;
esac

VERSION_STRING="${NEW_MAJOR}.${NEW_MINOR}.${NEW_PATCH}"
echo "New version: $VERSION_STRING"

# 1. CMakeLists.txt
sed -i \
    -e "s/^project(otlp-c VERSION [0-9.]*/project(otlp-c VERSION ${VERSION_STRING}/" \
    "$ROOT_DIR/CMakeLists.txt"

# 2. vcpkg.json
python3 -c "
import json
with open('$ROOT_DIR/vcpkg.json', 'r') as f:
    data = json.load(f)
data['version'] = '${VERSION_STRING}'
with open('$ROOT_DIR/vcpkg.json', 'w') as f:
    json.dump(data, f, indent=2)
    f.write('\n')
"

# 3. include/otlp-c/version.h
sed -i \
    -e "s/^#define OTLP_C_VERSION_MAJOR [0-9]*/#define OTLP_C_VERSION_MAJOR ${NEW_MAJOR}/" \
    -e "s/^#define OTLP_C_VERSION_MINOR [0-9]*/#define OTLP_C_VERSION_MINOR ${NEW_MINOR}/" \
    -e "s/^#define OTLP_C_VERSION_PATCH [0-9]*/#define OTLP_C_VERSION_PATCH ${NEW_PATCH}/" \
    "$ROOT_DIR/include/otlp-c/version.h"
sed -i \
    "s|#define OTLP_C_VERSION_STRING .*|#define OTLP_C_VERSION_STRING \"${VERSION_STRING}\"|" \
    "$ROOT_DIR/include/otlp-c/version.h"

# 4. CHANGELOG.md -- prepend a template entry
if [ -f "$ROOT_DIR/CHANGELOG.md" ]; then
    if ! grep -q "## \[${VERSION_STRING}\]" "$ROOT_DIR/CHANGELOG.md"; then
        TODAY=$(date +%Y-%m-%d)
        TMP=$(mktemp)
        {
            head -n 5 "$ROOT_DIR/CHANGELOG.md"
            echo ""
            echo "## [${VERSION_STRING}] - ${TODAY}"
            echo ""
            echo "### Added"
            echo ""
            echo "### Changed"
            echo ""
            echo "### Fixed"
            echo ""
            tail -n +6 "$ROOT_DIR/CHANGELOG.md"
        } > "$TMP"
        mv "$TMP" "$ROOT_DIR/CHANGELOG.md"
    fi
fi

echo "Bumped version to ${VERSION_STRING}"
echo "Changed files: CMakeLists.txt, vcpkg.json, version.h, CHANGELOG.md"
