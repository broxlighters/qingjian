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
  if command -v gnome-extensions >/dev/null; then
    gnome-extensions disable qingjian@qingjian.local 2>/dev/null || true
  fi
  python3 - "$install_prefix" <<'PY'
import os, pathlib, sys
config = pathlib.Path(os.environ.get('XDG_CONFIG_HOME', str(pathlib.Path.home() / '.config')))
prefix = pathlib.Path(sys.argv[1]).resolve()
autostart = config / 'autostart/qingjian-session-setup.desktop'
if autostart.is_symlink() and str(autostart.resolve()).startswith(str(prefix) + '/'):
    autostart.unlink()
for target in ['default.target', 'graphical-session.target']:
    link = config / f'systemd/user/{target}.wants/qingjian-linux-server.service'
    if link.is_symlink() and str(link.resolve()).startswith(str(prefix) + '/'):
        link.unlink()
dropin = config / 'systemd/user/qingjian-linux-server.service.d/qingjian-mode.conf'
if dropin.is_file() and dropin.read_text().startswith('# Managed by qingjian-session-setup\n'):
    dropin.unlink()
PY
fi
rm -f -- "$install_prefix/bin/qingjian-linux-server" "$install_prefix/bin/qingjian-session-setup" \
  "$install_prefix/bin/qingjian-diagnose" "$install_prefix/lib/fcitx5/qingjian.so" \
  "$install_prefix/share/fcitx5/addon/qingjian.conf" "$install_prefix/share/fcitx5/inputmethod/qingjian.conf" \
  "$install_prefix/share/systemd/user/qingjian-linux-server.service" "$install_prefix/share/pixmaps/qingjian.png"
rm -rf -- "$install_prefix/share/qingjian/resources" "$install_prefix/share/qingjian/management" \
  "$install_prefix/share/qingjian/rollback" "$install_prefix/share/gnome-shell/extensions/qingjian@qingjian.local"
rm -f -- "$install_prefix/share/qingjian/install-manifest.json" "$install_prefix/share/qingjian/install-options.json" \
  "$install_prefix/etc/xdg/autostart/qingjian-session-setup.desktop"
if [[ "$install_prefix" = "$HOME/.local" ]] && command -v systemctl >/dev/null; then
  systemctl --user daemon-reload 2>/dev/null || true
fi
printf '青简程序已卸载；用户配置与学习数据已保留。请在 Fcitx5 配置工具移除青简并重启 Fcitx5。\n'
