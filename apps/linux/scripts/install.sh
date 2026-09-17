#!/usr/bin/env bash
# //! 构建验证后暂存安装；自定义前缀与 --no-start 不操作当前会话。
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
install_prefix="$HOME/.local"
cargo_target_dir=$(realpath -m -- "${CARGO_TARGET_DIR:-$repo_root/target}")
profile=release
cmake_build_type=Release
sample=false
x11_backend=ON
gnome=false
no_start=false
startup=
while (($#)); do
  case "$1" in
    --prefix) install_prefix=${2:?--prefix 需要路径}; shift 2 ;;
    --debug) profile=debug; cmake_build_type=Debug; shift ;;
    --sample) sample=true; shift ;;
    --disable-x11) x11_backend=OFF; shift ;;
    --gnome) gnome=true; shift ;;
    --no-start) no_start=true; shift ;;
    --startup=session|--startup=background) startup=${1#*=}; shift ;;
    --help) echo '用法：install.sh [--gnome] [--no-start] [--startup=session|background] [--prefix 绝对目录] [--debug] [--sample] [--disable-x11]'; exit 0 ;;
    --experimental-x11) x11_backend=ON; shift ;; # 兼容旧安装命令
    *) echo "未知参数：$1" >&2; exit 2 ;;
  esac
done
[[ "$install_prefix" = /* && "$install_prefix" != / ]] || { echo '安装前缀必须是非根绝对路径' >&2; exit 2; }
previous_install=false
[[ ! -f "$install_prefix/bin/qingjian-linux-server" ]] || previous_install=true
previous_extension=false
[[ ! -f "$install_prefix/share/gnome-shell/extensions/qingjian@qingjian.local/metadata.json" ]] || previous_extension=true
[[ "$install_prefix" = "$HOME/.local" ]] || no_start=true
# 安装前检查未知 override/混合来源；staging 无需检查开发者安装。
if [[ "$no_start" = false ]]; then
  python3 - "$repo_root/apps/linux/management" "$install_prefix" <<'PY'
import pathlib, sys
sys.path.insert(0, sys.argv[1])
from session import preflight
conflicts = preflight(pathlib.Path(sys.argv[2]))
if conflicts:
    raise SystemExit('请先处理混合安装或未知 override，未安装：' + ', '.join(conflicts))
PY
fi
cargo_args=(build --target-dir "$cargo_target_dir" --manifest-path "$repo_root/Cargo.toml" -p qingjian-linux-server --locked)
[[ "$profile" != release ]] || cargo_args+=(--release)
cargo "${cargo_args[@]}"
cargo_ffi_args=(build --target-dir "$cargo_target_dir" --manifest-path "$repo_root/Cargo.toml" -p qingjian-render-ffi --locked)
[[ "$profile" != release ]] || cargo_ffi_args+=(--release)
cargo "${cargo_ffi_args[@]}"
ffi_lib="$cargo_target_dir/$profile/libqingjian_render_ffi.a"
cmake_args=(-S "$repo_root/apps/linux/fcitx5" -B "$repo_root/build/fcitx5" "-DCMAKE_BUILD_TYPE=$cmake_build_type")
cmake_args+=("-DQINGJIAN_X11_BACKEND=$x11_backend" "-DQINGJIAN_GNOME_PROBE=OFF" "-DQINGJIAN_GNOME_BACKEND=ON" "-DQINGJIAN_RENDER_FFI=ON" "-DQINGJIAN_RENDER_FFI_LIB=$ffi_lib" "-DQINGJIAN_RENDER_FFI_INCLUDE=$repo_root/apps/linux/render-ffi/include")
cmake "${cmake_args[@]}"
cmake --build "$repo_root/build/fcitx5" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$repo_root/build/fcitx5" --output-on-failure
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
cmake --install "$repo_root/build/fcitx5" --prefix "$stage"
# Fcitx 的 addon 搜索路径不含用户 lib；登记实际路径，重启后即可加载。
python3 - "$stage" "$install_prefix" <<'PY'
import pathlib, sys
prefix = pathlib.Path(sys.argv[1])
addon = prefix / 'share/fcitx5/addon/qingjian.conf'
library = pathlib.Path(sys.argv[2]) / 'lib/fcitx5/qingjian'
addon.write_text(addon.read_text().replace('Library=qingjian\n', f'Library={library}\n'))
PY
install -Dm755 "$cargo_target_dir/$profile/qingjian-linux-server" "$stage/bin/qingjian-linux-server"
resource_dir="$stage/share/qingjian/resources"
mkdir -p "$resource_dir/assets" "$resource_dir/data/generated"
for component in sample glossary levels emoji; do
  [[ ! -d "$repo_root/assets/$component" ]] || cp -a "$repo_root/assets/$component" "$resource_dir/assets/"
done
if [[ "$sample" = false && -d "$repo_root/data/generated" ]]; then
  mkdir -p "$resource_dir/data"
  cp -a "$repo_root/data/generated" "$resource_dir/data/"
fi
# 为实际安装的数据记录可复验的校验表。
(cd "$resource_dir" && find assets data -type f ! -name SHA256SUMS -print0 2>/dev/null | sort -z | xargs -0 -r sha256sum > SHA256SUMS)
(cd "$resource_dir" && sha256sum --check --quiet SHA256SUMS)
service_dir="$stage/share/systemd/user"
mkdir -p "$service_dir"
python3 - "$repo_root/apps/linux/data/qingjian-linux-server.service.in" "$service_dir/qingjian-linux-server.service" "$install_prefix" <<'PY'
import pathlib, sys
source, target, prefix = sys.argv[1:]
# systemd 引号规则；% 必须转义以免作为 specifier 展开。
escaped = prefix.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%')
pathlib.Path(target).write_text(pathlib.Path(source).read_text().replace('@PREFIX@', escaped))
PY
for module in common session diagnose mappings package_state; do
  install -Dm644 "$repo_root/apps/linux/management/$module.py" "$stage/share/qingjian/management/$module.py"
done
for entry in qingjian-session-setup qingjian-diagnose; do
  install -Dm755 "$repo_root/apps/linux/management/entry.sh" "$stage/bin/$entry"
done
install -Dm644 "$repo_root/apps/linux/scripts/deploy.py" "$stage/share/qingjian/management/deploy.py"
python3 - "$stage/share/qingjian/install-options.json" "$startup" "$gnome" "$previous_install" "$previous_extension" <<'PY'
import json, pathlib, sys
path, startup, gnome, previous, extension = sys.argv[1:]
pathlib.Path(path).write_text(json.dumps({'startup': startup or None, 'gnome': gnome == 'true', 'previous_install': previous == 'true', 'previous_extension': extension == 'true'}) + '\n')
PY
if [[ "$gnome" = true ]]; then
  [[ -f "$repo_root/apps/linux/gnome/extension/metadata.json" ]] || { echo '正式 GNOME 扩展不存在' >&2; exit 1; }
  mkdir -p "$stage/share/gnome-shell/extensions/qingjian@qingjian.local" "$stage/etc/xdg/autostart"
  cp "$repo_root/apps/linux/gnome/extension/"*.js "$repo_root/apps/linux/gnome/extension/metadata.json" "$stage/share/gnome-shell/extensions/qingjian@qingjian.local/"
  python3 - "$repo_root/apps/linux/data/qingjian-session-setup.desktop.in" "$stage" "$install_prefix" <<'PY'
import pathlib, sys
template, stage, prefix = sys.argv[1:]
escaped = prefix.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
destination = pathlib.Path(stage) / 'etc/xdg/autostart/qingjian-session-setup.desktop'
destination.write_text(pathlib.Path(template).read_text().replace('@PREFIX@', escaped))
PY
fi
deploy_args=(--stage "$stage" --prefix "$install_prefix")
[[ "$no_start" = true ]] || deploy_args+=(--session-state)
python3 "$repo_root/apps/linux/scripts/deploy.py" "${deploy_args[@]}"
if [[ "$no_start" = false ]]; then
  setup_args=(--restart)
  [[ -z "$startup" ]] || setup_args+=("--startup=$startup")
  if ! "$install_prefix/bin/qingjian-session-setup" "${setup_args[@]}"; then
    python3 "$repo_root/apps/linux/scripts/deploy.py" --prefix "$install_prefix" --rollback
    systemctl --user daemon-reload
    printf '会话登记失败，已恢复上一版文件及启动模式；请运行 qingjian-diagnose 检查后再启动。\n' >&2
    exit 1
  fi
fi
printf '已安装到 %s。重启 Fcitx5 后在配置工具添加「青简」。\n' "$install_prefix"
printf '会话登记：%s/bin/qingjian-session-setup\n' "$install_prefix"
printf '诊断：%s/bin/qingjian-diagnose --json\n' "$install_prefix"
[[ "$no_start" = false ]] || printf '仅安装文件：未运行 systemctl、启用扩展或修改当前会话。\n'
