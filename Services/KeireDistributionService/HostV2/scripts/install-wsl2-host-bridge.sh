#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: sudo install-wsl2-host-bridge.sh --host <dns-name> --upstream-port <port>
       install-wsl2-host-bridge.sh --host <dns-name> --upstream-port <port> --validate-only
       sudo install-wsl2-host-bridge.sh --uninstall
EOF
}

host=""
upstream_port=""
validate_only=0
uninstall=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) host="${2:?--host requires a DNS name}"; shift 2 ;;
    --upstream-port) upstream_port="${2:?--upstream-port requires a port}"; shift 2 ;;
    --validate-only) validate_only=1; shift ;;
    --uninstall) uninstall=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) printf 'Unknown WSL2 bridge installer option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

service_name=keire-wsl2-host-bridge.service
install_directory=/usr/local/lib/keire-distribution
installed_bridge="$install_directory/start-wsl2-host-bridge.sh"
unit_path="/etc/systemd/system/$service_name"
source_bridge="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/start-wsl2-host-bridge.sh"

if [[ $uninstall -eq 1 ]]; then
  [[ $validate_only -eq 0 && -z "$host" && -z "$upstream_port" ]] || {
    printf -- '--uninstall cannot be combined with other options.\n' >&2
    exit 2
  }
  [[ $EUID -eq 0 ]] || { printf 'Uninstall requires root.\n' >&2; exit 1; }
  systemctl disable --now "$service_name" 2>/dev/null || true
  rm -f -- "$unit_path" "$installed_bridge"
  systemctl daemon-reload
  printf 'Removed the WSL2 distribution-host bridge.\n'
  exit 0
fi

[[ "$host" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ && "$host" == *.* ]] || {
  printf 'The bridge host must be a plain DNS name.\n' >&2
  exit 2
}
[[ "$upstream_port" =~ ^[0-9]+$ && "$upstream_port" -ge 1 && "$upstream_port" -le 65535 ]] || {
  printf 'The upstream port must be an integer from 1 through 65535.\n' >&2
  exit 2
}
[[ -f "$source_bridge" ]] || { printf 'The WSL2 bridge script is missing.\n' >&2; exit 1; }

if [[ $validate_only -eq 1 ]]; then
  bash -n "$source_bridge"
  printf 'WSL2 bridge settings are valid for https://%s/ via Windows port %s.\n' "$host" "$upstream_port"
  exit 0
fi

grep -qi microsoft /proc/sys/kernel/osrelease || {
  printf 'The Windows host bridge installer is supported only inside WSL2.\n' >&2
  exit 2
}
[[ $EUID -eq 0 ]] || { printf 'Installation requires root.\n' >&2; exit 1; }
command -v systemctl >/dev/null 2>&1 || { printf 'systemd is required.\n' >&2; exit 1; }
for command_name in curl getent ip socat; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf '%s is required before installing the WSL2 host bridge.\n' "$command_name" >&2
    exit 1
  }
done

install -D -m 0755 -- "$source_bridge" "$installed_bridge"
unit_temporary="$(mktemp)"
trap 'rm -f -- "$unit_temporary"' EXIT
cat >"$unit_temporary" <<EOF
[Unit]
Description=Kéire WSL2 distribution-host loopback bridge
After=network-online.target
Wants=network-online.target
StartLimitIntervalSec=0

[Service]
Type=simple
DynamicUser=yes
ExecStart=$installed_bridge --host $host --upstream-port $upstream_port
Restart=on-failure
RestartSec=5s
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
PrivateDevices=true
PrivateTmp=true
ProtectControlGroups=true
ProtectHome=true
ProtectKernelModules=true
ProtectKernelTunables=true
ProtectSystem=strict
RestrictAddressFamilies=AF_INET AF_INET6 AF_NETLINK
RestrictNamespaces=true
RestrictRealtime=true

[Install]
WantedBy=multi-user.target
EOF
install -m 0644 -- "$unit_temporary" "$unit_path"
systemctl daemon-reload
systemctl enable --now "$service_name"
systemctl is-active --quiet "$service_name" || {
  systemctl status --no-pager "$service_name" >&2 || true
  exit 1
}
printf 'Installed the WSL2 distribution-host bridge for https://%s/.\n' "$host"
