#!/bin/bash
#
# Builds the WipeOS USB image from source.
# Tested on Ubuntu 24.04. The first build takes 2–4 hours depending on the machine
# and needs about 30 GB of free disk space.
#
set -euo pipefail

SHREDOS_REPO=https://github.com/PartialVolume/shredos.x86_64.git
SHREDOS_COMMIT=f6fe37735283ca367134db4aac9ef2aca2949594   # tested ShredOS version

ROOT=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$ROOT/work}

echo "==> Fetching LVGL"
git -C "$ROOT" submodule update --init gui/lvgl

if [ ! -d "$WORK/shredos/.git" ]; then
	echo "==> Fetching ShredOS ($SHREDOS_COMMIT)"
	mkdir -p "$WORK"
	git clone --filter=blob:none "$SHREDOS_REPO" "$WORK/shredos"
	git -C "$WORK/shredos" checkout -q "$SHREDOS_COMMIT"
	echo "==> Applying WipeOS changes"
	git -C "$WORK/shredos" apply "$ROOT/buildroot/shredos.patch"
fi

echo "==> Copying WipeOS files"
cp -r "$ROOT/buildroot/board" "$ROOT/buildroot/package" "$WORK/shredos/"
chmod 755 "$WORK/shredos/board/shredos/fsoverlay/usr/bin/shredos_gui_launcher"
mkdir -p "$WORK/shredos/package/shredos"
touch "$WORK/shredos/package/shredos/Config.in"
rsync -a --delete \
	--exclude build --exclude shredos-gui \
	--exclude 'lvgl/docs' --exclude 'lvgl/demos' --exclude 'lvgl/examples' \
	--exclude 'lvgl/tests' --exclude 'lvgl/.git' --exclude 'lvgl/scripts' --exclude 'lvgl/env_support' \
	"$ROOT/gui/" "$WORK/shredos/package/shredos-gui/src/"

cd "$WORK/shredos"
echo "==> Configuring"
make shredos_lite_defconfig
make shredos-gui-dirclean >/dev/null 2>&1 || true
echo "==> Building (this takes a while)"
make

mkdir -p "$ROOT/out"
cp output/images/wipeos-*.img "$ROOT/out/WipeOS.img"
(cd "$ROOT/out" && sha256sum WipeOS.img > WipeOS.img.sha256)
echo
echo "Done: $ROOT/out/WipeOS.img"
