#!/usr/bin/env bash
# //! 可重定位的用户管理入口；只加载同前缀的管理模块。
set -euo pipefail
prefix=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
case "$(basename -- "$0")" in
  qingjian-session-setup) module=session ;;
  qingjian-diagnose) module=diagnose ;;
  *) echo '未知青简管理入口' >&2; exit 2 ;;
esac
exec python3 "$prefix/share/qingjian/management/$module.py" --prefix "$prefix" "$@"
