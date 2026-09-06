#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=false
DRY_RUN=false
YES=false

usage() {
  printf 'Usage: %s [--build] [--dry-run] [--yes]\n' "$0"
  printf 'Uploads .pio/build/$PIO_ENV/firmware.bin to $ESP_IP using OTA.\n'
  printf 'Set ESP_IP and PIO_ENV in .env or export them in your shell.\n'
  printf '  --build    Build the selected board before uploading\n'
  printf '  --dry-run  Show the target and file without building or connecting\n'
  printf '  --yes      Skip the confirmation prompt\n'
}

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }

for arg in "$@"; do
  case "$arg" in
    --build) BUILD=true ;;
    --dry-run) DRY_RUN=true ;;
    --yes) YES=true ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; fail "Unknown option: $arg" ;;
  esac
done

exported_ip=${ESP_IP:-}
exported_env=${PIO_ENV:-}
if [[ -f "$ROOT/.env" ]]; then
  source "$ROOT/.env"
fi
ESP_IP=${exported_ip:-${ESP_IP:-}}
PIO_ENV=${exported_env:-${PIO_ENV:-}}

[[ -n "$ESP_IP" ]] || fail 'Set ESP_IP in .env or your shell.'
[[ "$ESP_IP" =~ ^[a-zA-Z0-9][a-zA-Z0-9.-]*(:[0-9]+)?$ ]] ||
  fail 'ESP_IP must be an IPv4 address or hostname, optionally with a port; no http:// or path.'
case "$PIO_ENV" in
  esp32-s3-zero|upesy_wroom) ;;
  *) fail 'Set PIO_ENV to esp32-s3-zero or upesy_wroom, matching your actual board.' ;;
esac

FIRMWARE="$ROOT/.pio/build/$PIO_ENV/firmware.bin"
URL="http://$ESP_IP/update?name=firmware"
printf 'Board:    %s\nFirmware: %s\nTarget:   %s\n' "$PIO_ENV" "$FIRMWARE" "$URL"

if [[ "$BUILD" == false ]]; then
  [[ -s "$FIRMWARE" ]] || fail 'No firmware.bin found for this board. Run again with --build.'
  printf 'Using the existing binary. Use --build to include current source and dependency pins.\n'
fi

if [[ "$DRY_RUN" == true ]]; then
  [[ "$BUILD" == false ]] || printf 'Would build %s first.\n' "$PIO_ENV"
  printf 'Dry run: nothing built or uploaded.\n'
  exit 0
fi

command -v curl >/dev/null || fail 'curl is required.'
if [[ "$YES" == false ]]; then
  printf 'This flashes and reboots the ESP. Check the board selection and make sure the machine is idle.\n'
  read -r -p 'Upload firmware? [y/N] ' answer || fail 'Confirmation required; use --yes for unattended uploads.'
  case "$answer" in
    y|Y|yes|YES) ;;
    *) printf 'Cancelled.\n'; exit 0 ;;
  esac
fi

if [[ "$BUILD" == true ]]; then
  command -v pio >/dev/null || fail 'PlatformIO (pio) is required for --build.'
  pio run --project-dir "$ROOT" -e "$PIO_ENV"
fi
[[ -s "$FIRMWARE" ]] || fail 'Firmware is missing or empty; nothing uploaded.'

RESPONSE=$(mktemp)
trap 'rm -f -- "$RESPONSE"' EXIT
if ! curl --disable --fail --show-error --progress-bar --noproxy '*' \
  --connect-timeout 10 --max-time 180 --retry 0 \
  --form "update=@\"$FIRMWARE\";filename=firmware.bin;type=application/octet-stream" \
  --output "$RESPONSE" "$URL"; then
  fail 'Upload failed or was interrupted. Check the device before retrying; it may already have received the firmware.'
fi

if ! grep -Fq 'Update Success!' "$RESPONSE"; then
  printf 'Device response:\n' >&2
  head -c 2000 "$RESPONSE" >&2
  printf '\n' >&2
  fail 'The device did not confirm a successful update.'
fi

printf 'Firmware accepted. The ESP should now reboot.\n'
