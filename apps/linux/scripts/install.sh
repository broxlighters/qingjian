#!/usr/bin/env bash
# 构建并安装到用户前缀；不修改 Fcitx profile，也不自动启动服务。
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
install_prefix="$HOME/.local"
profile=release
sample=false
while (($#)); do
  case "$1" in
    --prefix) install_prefix=${2:?--prefix 需要路径}; shift 2 ;;
    --debug) profile=debug; shift ;;
    --sample) sample=true; shift ;;
    *) echo "未知参数：$1" >&2; exit 2 ;;
  esac
done
[[ "$install_prefix" = /* && "$install_prefix" != / ]] || { echo '安装前缀必须是非根绝对路径' >&2; exit 2; }
cargo_args=(build --manifest-path "$repo_root/Cargo.toml" -p qingjian-linux-server --locked)
[[ "$profile" != release ]] || cargo_args+=(--release)
cargo "${cargo_args[@]}"
cmake -S "$repo_root/apps/linux/fcitx5" -B "$repo_root/build/fcitx5" -DCMAKE_BUILD_TYPE=Release
cmake --build "$repo_root/build/fcitx5" --parallel
ctest --test-dir "$repo_root/build/fcitx5" --output-on-failure
cmake --install "$repo_root/build/fcitx5" --prefix "$install_prefix"
install -Dm755 "$repo_root/target/$profile/qingjian-linux-server" "$install_prefix/bin/qingjian-linux-server"
resource_dir="$install_prefix/share/qingjian/resources"
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
service_dir="$install_prefix/share/systemd/user"
mkdir -p "$service_dir"
python3 - "$repo_root/apps/linux/data/qingjian-linux-server.service.in" "$service_dir/qingjian-linux-server.service" "$install_prefix" <<'PY'
import pathlib, sys
source, target, prefix = sys.argv[1:]
# systemd 引号规则；% 必须转义以免作为 specifier 展开。
escaped = prefix.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%')
pathlib.Path(target).write_text(pathlib.Path(source).read_text().replace('@PREFIX@', escaped))
PY
printf '已安装到 %s。重启 Fcitx5 后在配置工具添加「青简」。\n' "$install_prefix"
printf '启动：%s/bin/qingjian-linux-server\n' "$install_prefix"
printf '可选：systemctl --user daemon-reload && systemctl --user enable --now qingjian-linux-server.service\n'
