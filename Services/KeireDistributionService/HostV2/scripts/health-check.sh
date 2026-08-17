#!/usr/bin/env bash
set -euo pipefail

base_url="${1:-http://127.0.0.1:5088}"
response="$(curl --fail --silent --show-error --max-time 10 "${base_url%/}/health/ready")"
case "$response" in
  *'"status":"ready"'*|*'"status":"ready-degraded"'*)
    printf '%s\n' "$response"
    ;;
  *)
    printf 'Distribution service readiness response was unexpected: %s\n' "$response" >&2
    exit 1
    ;;
esac
