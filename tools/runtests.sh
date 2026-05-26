#!/bin/bash
# tools/runtests.sh – наглядный тестировщик BFASM с поддержкой множественных проверок

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BFASM="$SCRIPT_DIR/../bfasm"
BFRUN="$SCRIPT_DIR/../tools/bfrun"
STRICT=0
SINGLE_FILE=""

# ---------- helpers ----------
hex_to_bytes() {
  local input="$1"
  local result=""
  local i=0
  local len=${#input}
  while (( i < len )); do
    local ch="${input:$i:1}"
    if [[ "$ch" == '"' ]]; then
      # начало строки в кавычках
      local j=$((i+1))
      while (( j < len )) && [[ "${input:$j:1}" != '"' ]]; do
        result+="${input:$j:1}"
        ((j++))
      done
      ((i=j+1))  # пропустить закрывающую кавычку
    elif [[ "$ch" == '0' && "${input:$i:4}" =~ ^0x[0-9a-fA-F]{2} ]]; then
      # шестнадцатеричный байт
      local hex="${input:$i+2:2}"
      result+="\\x$hex"
      ((i+=4))
    else
      result+="$ch"
      ((i++))
    fi
  done
  echo -ne "$result"
}

# ---------- метаданные теста ----------
parse_metadata() {
  local f="$1"
  local line count=0
  META_DESC=""
  META_STDIN=()
  META_EXPECT=()
  META_EXPECT_ERROR=""
  while IFS= read -r line && (( count++ < 20 )); do
    if [[ "$line" =~ ^\;.*@desc\ (.*) ]]; then
      META_DESC="${BASH_REMATCH[1]}"
    elif [[ "$line" =~ ^\;.*@stdin\ (.*) ]]; then
      local val="${BASH_REMATCH[1]}"
      val="${val%%;*}"
      val="${val%"${val##*[![:space:]]}"}"
      # убрать внешние двойные кавычки, если есть
      if [[ "$val" =~ ^\"(.*)\"$ ]]; then
        val="${BASH_REMATCH[1]}"
      fi
      META_STDIN+=("$val")
    elif [[ "$line" =~ ^\;.*@expect\ (.*) ]]; then
      local val="${BASH_REMATCH[1]}"
      val="${val%%;*}"
      val="${val%"${val##*[![:space:]]}"}"
      META_EXPECT+=("$val")
    elif [[ "$line" =~ ^\;.*@expect_error\ (.*) ]]; then
      local val="${BASH_REMATCH[1]}"
      # убираем внешние кавычки, если есть
      val="${val%\"}"
      val="${val#\"}"
      META_EXPECT_ERROR="$val"
    fi
  done < "$f"
}

# ---------- тестирование одного файла ----------
test_file() {
  local f="$1"
  echo "=== $f ==="

  parse_metadata "$f"

  # предупреждение
  if [[ -z "${META_DESC:-}" && ${#META_EXPECT[@]} -eq 0 && -z "${META_EXPECT_ERROR:-}" ]]; then
    if [[ $STRICT -eq 1 ]]; then
      echo "FAIL: no expectations (strict mode)"
      return 1
    else
      echo "WARN: no expectations"
    fi
  fi

  # компиляция
  local tmp_bf=$(mktemp)
  local tmp_err=$(mktemp)
  "$BFASM" "$f" > "$tmp_bf" 2> "$tmp_err"
  local comp_status=$?

  if [[ -n "${META_EXPECT_ERROR:-}" ]]; then
    if grep -qF "$META_EXPECT_ERROR" "$tmp_err"; then
      echo "PASS (expected compile error)"
    else
      echo "FAIL: expected compile error '$META_EXPECT_ERROR' but got:"
      cat "$tmp_err"
    fi
    rm -f "$tmp_bf" "$tmp_err"
    return
  fi

  if [[ $comp_status -ne 0 ]]; then
    echo "FAIL [COMPILE] – compiler error:"
    cat "$tmp_err"
    rm -f "$tmp_bf" "$tmp_err"
    return 1
  fi
  rm -f "$tmp_err"

  # выполнение и проверка по раундам
  local rounds=${#META_EXPECT[@]}
  if [[ $rounds -eq 0 ]]; then
    echo "PASS (compile only)"
    rm -f "$tmp_bf"
    return
  fi

  local overall_pass=1
  for ((i=0; i<rounds; i++)); do
    local stdin_val="${META_STDIN[$i]:-}"
    local expect_val="${META_EXPECT[$i]}"

    local tmp_out=$(mktemp)
    local tmp_runerr=$(mktemp)
    if [[ -n "$stdin_val" ]]; then
      echo -n "$stdin_val" | "$BFRUN" "$tmp_bf" > "$tmp_out" 2> "$tmp_runerr"
    else
      "$BFRUN" "$tmp_bf" > "$tmp_out" 2> "$tmp_runerr"
    fi
    local run_status=$?

    if [[ $run_status -ne 0 ]]; then
      echo "FAIL [EXEC round $((i+1))] – runtime error:"
      cat "$tmp_runerr"
      overall_pass=0
      rm -f "$tmp_out" "$tmp_runerr"
      continue
    fi
    rm -f "$tmp_runerr"

    # сравнение
    local expected_bytes
    expected_bytes=$(hex_to_bytes "$expect_val")
    if diff <(echo -n "$expected_bytes") "$tmp_out" > /tmp/bftest.diff 2>&1; then
      echo "PASS round $((i+1))"
    else
      echo "FAIL round $((i+1)) [CHECK] – output mismatch:"
      echo "  expected: $(echo -n "$expected_bytes" | xxd -p | tr -d '\n')"
      echo "  got:      $(xxd -p < "$tmp_out" | tr -d '\n')"
      overall_pass=0
    fi
    rm -f "$tmp_out"
  done

  if [[ $overall_pass -eq 1 ]]; then
    echo "OVERALL: PASS"
  else
    echo "OVERALL: FAIL"
  fi
  rm -f "$tmp_bf"
}

# ---------- main ----------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --strict) STRICT=1; shift ;;
    -f) SINGLE_FILE="$2"; shift 2 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

if [[ ! -x "$BFASM" ]]; then
  echo "ERROR: bfasm not found: $BFASM"
  exit 1
fi
if [[ ! -x "$BFRUN" ]]; then
  echo "ERROR: bfrun not found: $BFRUN"
  exit 1
fi

if [[ -n "$SINGLE_FILE" ]]; then
  test_file "$SINGLE_FILE"
else
  cd "$SCRIPT_DIR/.."
  find tests -name '*.bfasm' -print0 | while IFS= read -r -d '' f; do
    test_file "$f"
    echo ""
  done
fi
