#!/usr/bin/env bash
set -u

# Run from project root (derived from script location)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

TEMP_DIR="${TEMP_DIR:-$ROOT_DIR/.parse-dir-results}"
mkdir -p "$TEMP_DIR"
LOCK_DIR="$TEMP_DIR/.print_lock"

# Colours (no-op if not a tty)
if [[ -t 1 ]]; then
  RED='\033[0;31m'
  GREEN='\033[0;32m'
  DIM='\033[0;2m'
  BOLD='\033[0;1m'
  NC='\033[0m'
else
  RED= GREEN= DIM= BOLD= NC=
fi

if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
elif command -v sysctl >/dev/null 2>&1; then
  JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
else
  JOBS=1
fi

print_usage() {
  echo "Usage: $0 [-j|--jobs N] <directory>"
  echo "  -j, --jobs N   Number of parallel parses (default: CPU count)"
  echo "  directory      Directory to search for .lean files (required)"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -j|--jobs)
      JOBS="${2:-}"
      shift 2
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Error: Unknown option '$1'"
      print_usage
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -lt 1 ]]; then
  echo "Error: directory required"
  print_usage
  exit 1
fi

TARGET_DIR="$1"
if ! [[ -d "$TARGET_DIR" ]]; then
  echo "Error: not a directory: $TARGET_DIR"
  exit 1
fi

if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [[ "$JOBS" -lt 1 ]]; then
  echo "Error: --jobs must be a positive integer"
  exit 1
fi

cleanup() {
  local pids
  pids=$(jobs -pr 2>/dev/null)
  if [[ -n "$pids" ]]; then
    kill $pids 2>/dev/null || true
  fi
  rm -rf "$TEMP_DIR"
}

trap cleanup EXIT INT TERM

PASS_COUNT=0
FAIL_COUNT=0
FAILED_FILES=()

# Run tree-sitter parse on one .lean file; write status and report under TEMP_DIR
run_single_parse() {
  local lean_file="$1"
  local rel_path="${lean_file#$TARGET_DIR/}"
  local safe_name
  safe_name=$(echo "$rel_path" | tr '/' '_' | tr -cd '[:alnum:]_.-')
  local result_file="$TEMP_DIR/$safe_name.out"
  local report_file="$TEMP_DIR/$safe_name.report"
  local status_file="$TEMP_DIR/$safe_name.status"
  local path_file="$TEMP_DIR/$safe_name.path"
  echo "$rel_path" >"$path_file"

  if tree-sitter parse "$lean_file" >"$result_file" 2>&1; then
    echo "pass" >"$status_file"
    {
      printf '%bPASS%b %b%s%b\n' "$GREEN" "$NC" "$DIM" "$rel_path" "$NC"
    } >"$report_file"
  else
    echo "fail" >"$status_file"
    {
      printf '%bFAIL%b %b%s%b\n' "$RED" "$NC" "$DIM" "$rel_path" "$NC"
    } >"$report_file"
  fi

  print_report "$report_file"
}

print_report() {
  local report_file="$1"
  while ! mkdir "$LOCK_DIR" 2>/dev/null; do
    sleep 0.05
  done
  cat "$report_file"
  rmdir "$LOCK_DIR" 2>/dev/null || true
}

# Collect .lean files (relative or absolute path)
shopt -s nullglob
lean_files=()
while IFS= read -r -d '' f; do
  lean_files+=("$f")
done < <(find "$TARGET_DIR" \( -type d -name '.*' -prune \) -o \( -type f -name '*.lean' -print0 \) 2>/dev/null)

total_files=${#lean_files[@]}
if [[ $total_files -eq 0 ]]; then
  echo "No .lean files found in $TARGET_DIR"
  exit 0
fi

echo -e "${BOLD}Parsing $total_files .lean file(s) in $TARGET_DIR (jobs=$JOBS)${NC}"
echo ""

active_jobs=0
for lean_file in "${lean_files[@]}"; do
  run_single_parse "$lean_file" &
  ((active_jobs++))
  if (( active_jobs >= JOBS )); then
    wait -n
    ((active_jobs--))
  fi
done

while (( active_jobs > 0 )); do
  wait -n
  ((active_jobs--))
done

for status_file in "$TEMP_DIR"/*.status; do
  [[ -f "$status_file" ]] || continue
  status=$(cat "$status_file")
  if [[ "$status" == "pass" ]]; then
    ((PASS_COUNT++))
  elif [[ "$status" == "fail" ]]; then
    ((FAIL_COUNT++))
    base=$(basename "$status_file" .status)
    path_file="$TEMP_DIR/$base.path"
    if [[ -f "$path_file" ]]; then
      FAILED_FILES+=("$(cat "$path_file")")
    else
      FAILED_FILES+=("$base")
    fi
  fi
done

echo ""
echo -e "${BOLD}==== Parse Summary ====${NC}"
echo -e "Total:  $total_files"
echo -e "Passed: ${GREEN}$PASS_COUNT${NC}"
echo -e "Failed: ${RED}$FAIL_COUNT${NC}"
if (( FAIL_COUNT > 0 )); then
  echo -e "${RED}Failed files:${NC}"
  printf '  - %s\n' "${FAILED_FILES[@]}"
fi

exit $(( FAIL_COUNT > 0 ? 1 : 0 ))
