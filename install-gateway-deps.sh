#!/usr/bin/env bash
set -euo pipefail

# Keep the NAT-enabled core separate from the legacy firmware's pinned SDK.
GATEWAY_CORE_VERSION="3.3.2"
GATEWAY_DATA_DIR="$HOME/.arduino15-esp32-gateway"
GATEWAY_CONFIG="$HOME/.arduino15/arduino-cli-gateway.yaml"

command -v arduino-cli >/dev/null || {
    echo "Run ./install-deps.sh first to install Arduino CLI and the libraries." >&2
    exit 1
}
mkdir -p "$GATEWAY_DATA_DIR" "$HOME/.arduino15/staging"
cat > "$GATEWAY_CONFIG" <<EOF
directories:
  data: $GATEWAY_DATA_DIR
  downloads: $HOME/.arduino15/staging
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
network:
  connection_timeout: 600s
EOF

GATEWAY_CLI=(arduino-cli --config-file "$GATEWAY_CONFIG")
"${GATEWAY_CLI[@]}" core update-index
version="$("${GATEWAY_CLI[@]}" core list | awk '$1 == "esp32:esp32" {print $2}')"
if [ "$version" != "$GATEWAY_CORE_VERSION" ]; then
    "${GATEWAY_CLI[@]}" core install "esp32:esp32@$GATEWAY_CORE_VERSION"
fi
"${GATEWAY_CLI[@]}" core list
