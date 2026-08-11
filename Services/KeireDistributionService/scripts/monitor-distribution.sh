#!/usr/bin/env bash
set -euo pipefail

base_url="${1:?Usage: monitor-distribution.sh <https-base-url> [state-path] [webhook-url] [--once]}"
state_path="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/monitor-state}"
webhook_url="${3:-}"
mode="${4:-}"
[[ "$base_url" == https://* ]] || { printf 'The external monitor requires an HTTPS URL.\n' >&2; exit 2; }
health_url="${base_url%/}/health/ready"
interval="${KEIRE_MONITOR_INTERVAL_SECONDS:-60}"
[[ "$interval" =~ ^[0-9]+$ && $interval -ge 15 && $interval -le 3600 ]] || {
  printf 'KEIRE_MONITOR_INTERVAL_SECONDS must be between 15 and 3600.\n' >&2
  exit 2
}
mkdir -p -- "$(dirname -- "$state_path")"

while :; do
  status=unhealthy
  message='Distribution readiness check failed.'
  if response="$(curl --fail --silent --show-error --max-time 10 "$health_url" 2>/dev/null)" &&
    [[ "$response" == *'"status":"ready"'* || "$response" == *'"status":"ready-degraded"'* ]]; then
    status=healthy
    message='Distribution origin is ready.'
  fi
  previous=''
  [[ ! -f "$state_path" ]] || previous="$(tr -d '\r\n' < "$state_path")"
  temporary="$state_path.tmp-$$"
  printf '%s\n' "$status" > "$temporary"
  mv -f -- "$temporary" "$state_path"
  if [[ "$previous" != "$status" && -n "$webhook_url" ]]; then
    payload="{\"source\":\"keire-distribution-monitor\",\"event\":\"availability-transition\",\"status\":\"$status\",\"url\":\"$health_url\",\"message\":\"$message\"}"
    curl --fail --silent --show-error --max-time 10 -H 'Content-Type: application/json' \
      --data-binary "$payload" "$webhook_url" >/dev/null
  fi
  printf '%s %s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$status" "$message"
  [[ "$mode" != --once ]] || break
  sleep "$interval"
done

[[ "$status" == healthy ]]
