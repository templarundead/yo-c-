#!/usr/bin/env bash
# Кросс-сборка Windows-артефактов на Linux-хосте: GUI и CLI через zig
# (mingw-w64 ABI), без Windows SDK и без MSVC.
#
#   tools/build-windows.sh              # собрать и упаковать
#   BUILD=build-win DIST=out tools/build-windows.sh
#
# Требуется: cmake >= 3.28, ninja, zig (pip install ziglang или пакет дистрибутива).
# Результат: dist-win64/ и yo-windows-x64.zip (exe + словари рядом с ними).
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build-win64}
DIST=${DIST:-dist-win64}
ARCHIVE=${ARCHIVE:-yo-windows-x64.zip}

if ! command -v zig >/dev/null 2>&1; then
	echo "нет zig в PATH — поставьте: python3 -m pip install --user ziglang" >&2
	echo "                        или: apt install zig / pacman -S zig" >&2
	exit 1
fi

# YO_STRIP: у зиг-драйвера -Wl,-s срезает таблицу символов и мёртвый код —
# на замерах это ~2x по размеру exe. LTO здесь недоступно (см. README).
cmake -G Ninja -S . -B "$BUILD" \
	-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-windows.cmake \
	-DCMAKE_BUILD_TYPE=Release -DYO_STRIP=ON \
	-DYO_BUILD_GUI=ON -DYO_BUILD_CLI=ON -DYO_BUILD_TESTS=OFF
cmake --build "$BUILD" -j

rm -rf "$DIST"
mkdir -p "$DIST"
cp "$BUILD/src/app/yo-gui.exe" "$BUILD/src/cli/yo-cli.exe" "$DIST/"
# Словари кладутся рядом с exe: ensure_yoficator() ищет их в каталоге
# программы раньше, чем где-либо ещё.
cp tests/data/yox.dat tests/data/yo_regexpr.dat "$DIST/"
cp README.md NOTICE LICENSE "$DIST/"

rm -f "$ARCHIVE"
(cd "$DIST" && cmake -E tar cf "../$ARCHIVE" --format=zip .)

echo
echo "готово: $DIST/ и $ARCHIVE"
ls -lh "$DIST" | sed 's/^/  /'
