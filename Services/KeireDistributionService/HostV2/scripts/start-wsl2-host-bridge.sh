#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: start-wsl2-host-bridge.sh --host <dns-name> --upstream-port <port>
       [--listen-address <ipv4>] [--listen-port <port>]

Bridges a Windows-loopback development hostname from WSL2 to the Windows host's
internal Caddy HTTPS port without changing the Hub's production URL or TLS checks.
EOF
}

host="${KEIRE_WSL_BRIDGE_HOST:-}"
upstream_port="${KEIRE_WSL_BRIDGE_UPSTREAM_PORT:-}"
listen_address="${KEIRE_WSL_BRIDGE_LISTEN_ADDRESS:-127.0.0.1}"
listen_port="${KEIRE_WSL_BRIDGE_LISTEN_PORT:-443}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) host="${2:?--host requires a DNS name}"; shift 2 ;;
    --upstream-port) upstream_port="${2:?--upstream-port requires a port}"; shift 2 ;;
    --listen-address) listen_address="${2:?--listen-address requires an IPv4 address}"; shift 2 ;;
    --listen-port) listen_port="${2:?--listen-port requires a port}"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) printf 'Unknown WSL2 bridge option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "$host" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ && "$host" == *.* ]] || {
  printf 'The bridge host must be a plain DNS name.\n' >&2
  exit 2
}
[[ "$listen_address" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]] || {
  printf 'The bridge listen address must be an IPv4 address.\n' >&2
  exit 2
}
for port in "$upstream_port" "$listen_port"; do
  [[ "$port" =~ ^[0-9]+$ && "$port" -ge 1 && "$port" -le 65535 ]] || {
    printf 'Bridge ports must be integers from 1 through 65535.\n' >&2
    exit 2
  }
done
grep -qi microsoft /proc/sys/kernel/osrelease || {
  printf 'The Windows host bridge is supported only inside WSL2.\n' >&2
  exit 2
}

for command_name in curl getent ip socat; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf '%s is required for the WSL2 host bridge.\n' "$command_name" >&2
    exit 1
  }
done

gateway="$(ip -4 route show default | awk 'NR == 1 { print $3 }')"
[[ "$gateway" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]] || {
  printf 'The WSL2 Windows-host gateway could not be determined.\n' >&2
  exit 1
}
resolved="$(getent ahostsv4 "$host" | awk '$2 == "STREAM" { print $1; exit }')"
[[ "$resolved" == "$listen_address" ]] || {
  printf '%s resolves to %s inside WSL2, not the bridge address %s.\n' \
    "$host" "${resolved:-nothing}" "$listen_address" >&2
  exit 1
}

curl --fail --silent --show-error --noproxy '*' --connect-timeout 5 \
  --resolve "$host:$upstream_port:$gateway" \
  "https://$host:$upstream_port/health/ready" >/dev/null
printf 'Bridging https://%s:%s to Windows host %s:%s.\n' \
  "$host" "$listen_port" "$gateway" "$upstream_port"
exec socat "TCP4-LISTEN:$listen_port,bind=$listen_address,reuseaddr,fork" \
  "TCP4:$gateway:$upstream_port,connect-timeout=5"
