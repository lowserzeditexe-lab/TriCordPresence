#!/usr/bin/env bash
# Source ce fichier pour exposer devkitARM/devkitPro et les outils hôte du projet.
#   source tools/env.sh
# devkitPro est attendu dans /opt/devkitpro (installation dkp-pacman classique) ;
# à défaut on cherche un arbre extrait de l'image Docker devkitpro/devkitarm.
_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -d /opt/devkitpro/devkitARM ]; then
    export DEVKITPRO=/opt/devkitpro
elif [ -d /opt/dkp_root/opt/devkitpro/devkitARM ]; then
    export DEVKITPRO=/opt/dkp_root/opt/devkitpro
fi
export DEVKITARM="$DEVKITPRO/devkitARM"
export CTRULIB="$DEVKITPRO/libctru"
export CTRPFLIB="$DEVKITPRO/libctrpf"
export PORTLIBS="$DEVKITPRO/portlibs/3ds"
export PATH="$_ROOT/tools/bin:$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
unset _ROOT
