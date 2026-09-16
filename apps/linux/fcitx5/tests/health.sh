#!/usr/bin/env bash
# //! 用独立 Xvfb 隔离 selection 测试，绝不在用户 X server 上模拟合成器消失。
set -euo pipefail
server=${QINGJIAN_XVFB:-Xvfb}
command -v "$server" >/dev/null || exit 77
test_dir=$(mktemp -d)
server_pid=
cleanup() {
  if [[ -n "$server_pid" ]]; then
    kill -CONT "$server_pid" 2>/dev/null || true
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  python3 - "$test_dir" <<'PY'
import pathlib, shutil, sys
shutil.rmtree(pathlib.Path(sys.argv[1]))
PY
}
trap cleanup EXIT
"$server" -displayfd 3 -screen 0 800x600x24 -nolisten tcp -ac 3>"$test_dir/display" >"$test_dir/server.log" 2>&1 &
server_pid=$!
for ((attempt = 0; attempt < 100; ++attempt)); do
  [[ ! -s "$test_dir/display" ]] || break
  kill -0 "$server_pid" 2>/dev/null || { cat "$test_dir/server.log" >&2; exit 1; }
  sleep .05
done
[[ -s "$test_dir/display" ]] || { cat "$test_dir/server.log" >&2; exit 1; }
"$1" ":$(cat "$test_dir/display")" "$server_pid"
