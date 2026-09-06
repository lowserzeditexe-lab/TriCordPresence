#!/usr/bin/env bash
# Compile les outils hôte non fournis par devkitPro : makerom, ctrtool, 3gxtool,
# bannertool. Sorties dans tools/bin/. Idempotent (saute ce qui existe déjà).
# Dépendances hôte (Debian/Ubuntu) : build-essential git libyaml-cpp-dev
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
BIN="$ROOT/tools/bin"
mkdir -p "$BIN" "$TP"

clone() { [ -d "$2" ] || git clone -q --depth 1 "$1" "$2"; }

if [ ! -x "$BIN/makerom" ] || [ ! -x "$BIN/ctrtool" ]; then
    echo "== makerom + ctrtool (3DSGuy/Project_CTR)"
    clone https://github.com/3DSGuy/Project_CTR.git "$TP/project_ctr"
    for t in makerom ctrtool; do
        make -C "$TP/project_ctr/$t" deps -j"$(nproc)" >/dev/null 2>&1
        make -C "$TP/project_ctr/$t" -j"$(nproc)" >/dev/null 2>&1
        cp "$TP/project_ctr/$t/bin/$t" "$BIN/"
    done
fi

if [ ! -x "$BIN/3gxtool" ]; then
    echo "== 3gxtool (Nanquitas/3gxtool)"
    clone https://github.com/Nanquitas/3gxtool.git "$TP/3gxtool"
    # Le dépôt embarque une yaml-cpp x86 précompilée : on compile directement
    # contre la lib système (apt install libyaml-cpp-dev) pour rester portable.
    (cd "$TP/3gxtool" && g++ -std=gnu++17 -O2 -I includes -I/usr/include/yaml-cpp sources/*.cpp -lyaml-cpp -o 3gxtool 2>/dev/null)
    cp "$TP/3gxtool/3gxtool" "$BIN/"
fi

if [ ! -x "$BIN/bannertool" ]; then
    echo "== bannertool (Epicpkmn11/bannertool, miroir du dépôt Steveice10 disparu)"
    clone https://github.com/Epicpkmn11/bannertool.git "$TP/bannertool"
    (
        cd "$TP/bannertool"
        sed -i 's#git://github.com/#https://github.com/#g' .gitmodules
        git submodule sync -q && git submodule update --init --recursive -q
        # buildtools ne connaît que x86 : autoriser aarch64/arm64 et retirer -m64
        sed -i 's/ifeq ($(UNAME_M),$(filter $(UNAME_M),x86_64 amd64))/ifeq ($(UNAME_M),$(filter $(UNAME_M),x86_64 amd64 aarch64 arm64))/' buildtools/make_base
        sed -i 's/COMMON_CC_FLAGS += -m64/COMMON_CC_FLAGS +=/' buildtools/make_base
        make -j"$(nproc)" >/dev/null 2>&1
    )
    cp "$TP/bannertool/output/linux-"*"/bannertool" "$BIN/"
fi

echo "Outils hôte : $(ls "$BIN")"
