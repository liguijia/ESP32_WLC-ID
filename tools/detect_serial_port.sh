#!/usr/bin/env bash

set -euo pipefail

QUIET=0
LIST_ONLY=0
PRINT_SOURCE=0

usage() {
  cat <<'EOF'
用法:
  ./tools/detect_serial_port.sh [--quiet] [--list] [--source]

说明:
  在当前 Docker / Dev Container 环境中自动发现 ESP32 可用串口。
  优先级如下：
    1. /host-dev/serial/by-id/*
    2. /dev/serial/by-id/*
    3. /host-dev/ttyACM*
    4. /host-dev/ttyUSB*
    5. /dev/ttyACM*
    6. /dev/ttyUSB*

行为:
  - 某一优先级组只有 1 个设备时：输出该串口路径并返回 0
  - 某一优先级组有多个设备时：报歧义并返回非 0，要求显式指定 PORT
  - 没找到设备时：返回非 0

选项:
  --quiet   失败时不输出说明，仅通过返回码表示结果
  --list    列出所有候选设备，不做自动选择
  --source  输出自动选中串口的来源标签，而不是设备路径
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --quiet)
      QUIET=1
      shift
      ;;
    --list)
      LIST_ONLY=1
      shift
      ;;
    --source)
      PRINT_SOURCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf '未知选项: %s\n' "$1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

print_err() {
  if [[ ${QUIET} -eq 0 ]]; then
    printf '%s\n' "$*" >&2
  fi
}

append_matches() {
  local pattern="$1"
  shift
  local -n out_ref=$1
  local path

  shopt -s nullglob
  for path in ${pattern}; do
    [[ -e "${path}" ]] || continue
    out_ref+=("${path}")
  done
  shopt -u nullglob
}

unique_paths() {
  python3 - "$@" <<'PY'
import sys

seen = set()
for item in sys.argv[1:]:
    if item in seen:
        continue
    seen.add(item)
    print(item)
PY
}

GROUP_LABELS=(
  "/host-dev/serial/by-id"
  "/dev/serial/by-id"
  "/host-dev/ttyACM*"
  "/host-dev/ttyUSB*"
  "/dev/ttyACM*"
  "/dev/ttyUSB*"
)

GROUP_SOURCES=(
  "host-dev-by-id"
  "dev-by-id"
  "host-dev-ttyACM"
  "host-dev-ttyUSB"
  "dev-ttyACM"
  "dev-ttyUSB"
)

GROUP_PATTERNS=(
  "/host-dev/serial/by-id/*"
  "/dev/serial/by-id/*"
  "/host-dev/ttyACM*"
  "/host-dev/ttyUSB*"
  "/dev/ttyACM*"
  "/dev/ttyUSB*"
)

if [[ ${LIST_ONLY} -eq 1 ]]; then
  for i in "${!GROUP_PATTERNS[@]}"; do
    matches=()
    append_matches "${GROUP_PATTERNS[$i]}" matches
    if [[ ${#matches[@]} -eq 0 ]]; then
      continue
    fi
    printf '[%s]\n' "${GROUP_LABELS[$i]}"
    unique_paths "${matches[@]}"
  done
  exit 0
fi

for i in "${!GROUP_PATTERNS[@]}"; do
  matches=()
  append_matches "${GROUP_PATTERNS[$i]}" matches

  if [[ ${#matches[@]} -eq 0 ]]; then
    continue
  fi

  mapfile -t deduped < <(unique_paths "${matches[@]}")

  if [[ ${#deduped[@]} -eq 1 ]]; then
    if [[ ${PRINT_SOURCE} -eq 1 ]]; then
      printf '%s\n' "${GROUP_SOURCES[$i]}"
    else
      printf '%s\n' "${deduped[0]}"
    fi
    exit 0
  fi

  print_err "检测到多个可用串口，无法自动唯一确定："
  for item in "${deduped[@]}"; do
    print_err "  ${item}"
  done
  print_err "请显式指定 PORT，例如："
  print_err "  make flash PORT=${deduped[0]}"
  exit 1
done

print_err "未检测到可用串口。请确认："
print_err "  1. 已通过 usbipd attach 将设备挂到 WSL"
print_err "  2. Dev Container 已重建，且 /dev 已映射到 /host-dev"
print_err "  3. 当前容器内存在 /host-dev/serial/by-id/* 或 ttyACM/ttyUSB 设备"
exit 1
