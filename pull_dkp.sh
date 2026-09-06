#!/usr/bin/env bash
set -euo pipefail
REPO="devkitpro/devkitarm"
LAYERS=(
  "sha256:b3407f3b5b5beb45007b2980ccef71785b08dae5a2dcbdd91272f35c4c5e784f"
  "sha256:821153548aef3505d23a28277f856f08c08e6232f925189f470252bb7a2a6bc9"
  "sha256:0135d32bed34d8c041fb726b74b7a28445bf88713a4172a3080081a2e555e0d8"
  "sha256:a716a160de65205f0e2d51bedbd018832d44df38ca07732569081b22c5bd7f44"
)
mkdir -p /app/dkp_layers /opt/dkp_root
get_token() {
  curl -sS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:${REPO}:pull" \
    | python3 -c "import sys,json;print(json.load(sys.stdin)['token'])"
}
i=0
for d in "${LAYERS[@]}"; do
  i=$((i+1))
  f="/app/dkp_layers/layer${i}.tar.gz"
  if [ ! -s "$f" ]; then
    echo "== downloading layer $i ($d) =="
    TOKEN=$(get_token)
    curl -sSL -H "Authorization: Bearer $TOKEN" \
      "https://registry-1.docker.io/v2/${REPO}/blobs/${d}" -o "$f"
  fi
  echo "  layer $i size: $(du -h "$f" | cut -f1)"
done
echo "== extracting layers into /opt/dkp_root =="
for i in 1 2 3 4; do
  echo "  extracting layer $i"
  tar -xzf "/app/dkp_layers/layer${i}.tar.gz" -C /opt/dkp_root 2>/dev/null || \
  tar -xzf "/app/dkp_layers/layer${i}.tar.gz" -C /opt/dkp_root
done
echo "== DONE =="
ls -la /opt/dkp_root/opt/devkitpro/ 2>/dev/null || echo "NO devkitpro dir!"
