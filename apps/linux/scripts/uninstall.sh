#!/usr/bin/env bash
# 卸载程序与随包资源；用户配置、学习数据和日志全部保留。
set -euo pipefail
install_prefix="$HOME/.local"
if (($#)); then
  [[ "$1" = --prefix && $# = 2 ]] || { echo '用法：uninstall.sh [--prefix 绝对路径]' >&2; exit 2; }
  install_prefix=$2
fi
[[ "$install_prefix" = /* && "$install_prefix" != / ]] || { echo '安装前缀必须是非根绝对路径' >&2; exit 2; }
if [[ "$install_prefix" = "$HOME/.local" ]] && command -v systemctl >/dev/null; then
  systemctl --user disable --now qingjian-linux-server.service 2>/dev/null || true
fi
rm -f -- "$install_prefix/bin/qingjian-linux-server" "$install_prefix/lib/fcitx5/qingjian.so" \
  "$install_prefix/share/fcitx5/addon/qingjian.conf" "$install_prefix/share/fcitx5/inputmethod/qingjian.conf" \
  "$install_prefix/share/systemd/user/qingjian-linux-server.service" "$install_prefix/share/pixmaps/qingjian.png"
rm -rf -- "$install_prefix/share/qingjian/resources"
printf '青简程序已卸载；用户配置与学习数据已保留。请在 Fcitx5 配置工具移除青简并重启 Fcitx5。\n'
