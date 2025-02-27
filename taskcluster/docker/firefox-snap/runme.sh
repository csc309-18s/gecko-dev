#!/bin/bash

set -xe

# Required env variables
test "$VERSION"
test "$BUILD_NUMBER"
test "$CANDIDATES_DIR"
test "$L10N_CHANGESETS"

# Optional env variables
: WORKSPACE                     "${WORKSPACE:=/home/worker/workspace}"
: ARTIFACTS_DIR                 "${ARTIFACTS_DIR:=/home/worker/artifacts}"
: PUSH_TO_CHANNEL               ""

SCRIPT_DIRECTORY="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

TARGET="firefox-${VERSION}.snap"
TARGET_FULL_PATH="$ARTIFACTS_DIR/$TARGET"
SOURCE_DEST="${WORKSPACE}/source"

mkdir -p "$ARTIFACTS_DIR"
rm -rf "$SOURCE_DEST" && mkdir -p "$SOURCE_DEST"

CURL="curl --location --retry 10 --retry-delay 10"

# Download and extract en-US linux64 binary
$CURL -o "${WORKSPACE}/firefox.tar.bz2" \
    "${CANDIDATES_DIR}/${VERSION}-candidates/build${BUILD_NUMBER}/linux-x86_64/en-US/firefox-${VERSION}.tar.bz2"
tar -C "$SOURCE_DEST" -xf "${WORKSPACE}/firefox.tar.bz2" --strip-components=1

# Get Ubuntu configuration
PARTNER_CONFIG_DIR="$WORKSPACE/partner_config"
git clone https://github.com/mozilla-partners/canonical.git "$PARTNER_CONFIG_DIR"

DISTRIBUTION_DIR="$SOURCE_DEST/distribution"
mv "$PARTNER_CONFIG_DIR/desktop/ubuntu/distribution" "$DISTRIBUTION_DIR"
cp -v "$SCRIPT_DIRECTORY/firefox.desktop" "$DISTRIBUTION_DIR"

# Use list of locales to fetch L10N XPIs
$CURL -o "${WORKSPACE}/l10n_changesets.json" "$L10N_CHANGESETS"
locales=$(python3 "$SCRIPT_DIRECTORY/extract_locales_from_l10n_json.py" "${WORKSPACE}/l10n_changesets.json")

mkdir -p "$DISTRIBUTION_DIR/extensions"
for locale in $locales; do
    $CURL -o "$SOURCE_DEST/distribution/extensions/langpack-${locale}@firefox.mozilla.org.xpi" \
        "$CANDIDATES_DIR/${VERSION}-candidates/build${BUILD_NUMBER}/linux-x86_64/xpi/${locale}.xpi"
done

# Extract gtk30.mo from Ubuntu language packs
apt download language-pack-gnome-*-base
for i in *.deb; do
    # shellcheck disable=SC2086
    dpkg-deb --fsys-tarfile $i | tar xv -C "$SOURCE_DEST" --wildcards "./usr/share/locale-langpack/*/LC_MESSAGES/gtk30.mo" || true
done

# Generate snapcraft manifest
sed -e "s/@VERSION@/${VERSION}/g" -e "s/@BUILD_NUMBER@/${BUILD_NUMBER}/g" snapcraft.yaml.in > "${WORKSPACE}/snapcraft.yaml"
cp -v "$SCRIPT_DIRECTORY/mimeapps.list" "$WORKSPACE"
cd "${WORKSPACE}"
snapcraft

mv -- *.snap "$TARGET_FULL_PATH"

cd "$ARTIFACTS_DIR"

# Generate checksums file
size=$(stat --printf="%s" "$TARGET_FULL_PATH")
sha=$(sha512sum "$TARGET_FULL_PATH" | awk '{print $1}')
echo "$sha sha512 $size $TARGET" > "$TARGET.checksums"

echo "Generating signing manifest"
hash=$(sha512sum "$TARGET.checksums" | awk '{print $1}')

cat << EOF > signing_manifest.json
[{"file_to_sign": "$TARGET.checksums", "hash": "$hash"}]
EOF

# For posterity
find . -ls
cat "$TARGET.checksums"
cat signing_manifest.json


# Upload snaps to Ubuntu Snap Store
# TODO: Make this part an independent task
if [[ "$PUSH_TO_CHANNEL" =~ (^(edge|candidate)$)  ]]; then
  echo "Uploading to Ubuntu Store on channel $PUSH_TO_CHANNEL"
  bash "$SCRIPT_DIRECTORY/fetch_macaroons.sh" "http://taskcluster/secrets/v1/secret/project/releng/snapcraft/firefox/$PUSH_TO_CHANNEL"
  snapcraft push --release "$PUSH_TO_CHANNEL" "$TARGET_FULL_PATH"
else
  echo "No upload done: PUSH_TO_CHANNEL value \"$PUSH_TO_CHANNEL\" doesn't match a known channel."
fi
