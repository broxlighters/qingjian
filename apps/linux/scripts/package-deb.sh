#!/usr/bin/env bash
# //! 原生 Debian 打包；只在临时前缀构建，不安装到宿主或启动用户服务。
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
output_dir="$repo_root/target/deb"
sample=false
gnome=true
while (($#)); do
  case "$1" in
    --output) output_dir=${2:?--output 需要绝对目录}; shift 2 ;;
    --sample) sample=true; shift ;;
    --gnome) gnome=true; shift ;;
    --minimal) gnome=false; shift ;;
    --help) echo '用法：package-deb.sh [--output 绝对目录] [--sample] [--minimal]'; exit 0 ;;
    *) echo "未知参数：$1" >&2; exit 2 ;;
  esac
done
[[ "$output_dir" = /* ]] || { echo '输出目录必须是绝对路径' >&2; exit 2; }
for tool in dpkg-deb dpkg-shlibdeps dpkg-architecture python3 strip; do
  command -v "$tool" >/dev/null || { echo "缺少构建工具：$tool" >&2; exit 1; }
done
architecture=$(dpkg --print-architecture)
multiarch=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
version=$(sed -n 's/^version = "\([^"]*\)"/\1/p' "$repo_root/apps/linux/server/Cargo.toml" | head -1)
[[ -n "$version" ]] || { echo '无法读取 Linux Server 版本' >&2; exit 1; }
commit=$(git -C "$repo_root" rev-parse HEAD)
if [[ "$version" = *-dev ]]; then
  debian_version="${version%-dev}~dev+g${commit:0:7}"
else
  debian_version="$version"
fi
if [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=normal)" ]]; then
  debian_version+='+dirty'
fi
if [[ "$sample" = true ]]; then debian_version+='+sample'; fi
dpkg --validate-version "$debian_version"
# 完整包不能静默退化为样例包；释义无 .qj 时仍由随包 TSV 提供。
if [[ "$sample" = false ]]; then
  for resource in dict.qj lm.qj english.tsv; do
    [[ -f "$repo_root/data/generated/$resource" ]] || {
      echo "缺少产品数据 data/generated/$resource；仅测试包可用 --sample" >&2; exit 1;
    }
  done
fi
mkdir -p "$repo_root/target" "$output_dir"
work_dir=$(mktemp -d "$repo_root/target/deb-work.XXXXXX")
# 保留失败构建的目录以便诊断，不对任意外部目录做递归清理。
stage="$work_dir/debian/qingjian-fcitx5"
packaging="$repo_root/apps/linux/packaging/debian"
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$repo_root" show -s --format=%ct HEAD)}
# 先只复制样例和基础资源，再显式挑选产品文件，避免打入原始语料或续跑数据。
install_args=(--prefix "$stage/usr" --sample --no-start)
[[ "$gnome" = false ]] || install_args+=(--gnome)
"$repo_root/apps/linux/scripts/install.sh" "${install_args[@]}"
resources="$stage/usr/share/qingjian/resources"
# 运行只读参数入口以验证模块依赖完整；--help 不连接用户总线或服务。
PYTHONDONTWRITEBYTECODE=1 "$stage/usr/bin/qingjian-session-setup" --help >/dev/null
PYTHONDONTWRITEBYTECODE=1 "$stage/usr/bin/qingjian-diagnose" --help >/dev/null
[[ -f "$stage/usr/share/qingjian/management/package_state.py" ]] || { echo '缺少系统包迁移模块' >&2; exit 1; }
if [[ "$sample" = false ]]; then
  for name in dict.qj lm.qj glossary-en.qj glossary-ja.qj glossary-zh.qj glossary-es.qj english.tsv english-frequency.tsv; do
    if [[ -f "$repo_root/data/generated/$name" ]]; then
      install -m644 "$repo_root/data/generated/$name" "$resources/data/generated/$name"
    fi
  done
  mkdir -p "$resources/data/generated/dicts"
for file in "$repo_root"/data/generated/dicts/*.qj; do
    [[ ! -f "$file" || "$(basename "$file")" = ._* ]] || install -m644 "$file" "$resources/data/generated/dicts/"
  done
fi
# 插件进入 Debian multiarch 目录；元数据用最终路径，不含 staging 路径。
mkdir -p "$stage/usr/lib/$multiarch/fcitx5" "$stage/usr/lib/systemd/user"
mv "$stage/usr/lib/fcitx5/qingjian.so" "$stage/usr/lib/$multiarch/fcitx5/qingjian.so"
rmdir "$stage/usr/lib/fcitx5"
if [[ "$gnome" = true ]]; then
  mkdir -p "$stage/etc/xdg/autostart"
  sed 's|@PREFIX@|/usr|g' "$repo_root/apps/linux/data/qingjian-session-setup.desktop.in" > "$stage/etc/xdg/autostart/qingjian-session-setup.desktop"
  rm -rf "$stage/usr/etc"
fi
rm -rf "$stage/usr/share/qingjian/rollback" "$stage/usr/share/qingjian/install-manifest.json"
sed "s|^Library=.*|Library=/usr/lib/$multiarch/fcitx5/qingjian|" \
  "$repo_root/apps/linux/fcitx5/data/addon/qingjian.conf" > "$stage/usr/share/fcitx5/addon/qingjian.conf"
sed 's|@PREFIX@|/usr|g' "$repo_root/apps/linux/data/qingjian-linux-server.service.in" \
  > "$stage/usr/lib/systemd/user/qingjian-linux-server.service"
# 删除由用户安装脚本生成的 staging service，避免重复单元。
python3 - "$stage/usr/share/systemd/user/qingjian-linux-server.service" <<'PY_SERVICE'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
path.unlink()
path.parent.rmdir()
path.parent.parent.rmdir()
PY_SERVICE
strip --strip-unneeded "$stage/usr/bin/qingjian-linux-server" "$stage/usr/lib/$multiarch/fcitx5/qingjian.so"
mkdir -p "$stage/DEBIAN" "$stage/usr/share/doc/qingjian-fcitx5"
install -m755 "$packaging/postinst" "$stage/DEBIAN/postinst"
cp "$packaging/control" "$work_dir/debian/control"
awk 'seen || /^Package:/{seen=1} seen' "$packaging/control" > "$stage/DEBIAN/control"
shlibs=$(cd "$work_dir" && dpkg-shlibdeps -O \
  -e"$stage/usr/bin/qingjian-linux-server" \
  -e"$stage/usr/lib/$multiarch/fcitx5/qingjian.so")
shlibs=$(sed -n 's/^shlibs:Depends=//p' <<< "$shlibs")
[[ -n "$shlibs" ]] || { echo '无法计算 Debian 动态库依赖' >&2; exit 1; }
for doc in README.Debian copyright; do
  install -m644 "$packaging/$doc" "$stage/usr/share/doc/qingjian-fcitx5/$doc"
done
install -m644 "$repo_root/LICENSE" "$stage/usr/share/doc/qingjian-fcitx5/LICENSE"
# 随包保留词库与英文数据的来源、版权声明。
for doc in lexicon/README.md lexicon/QINGJIAN.md lexicon/00_meta/THUOCL_LICENSE.txt \
  lexicon/05_english/README.md lexicon/05_english/sources/ESDB_Copyright.txt \
  lexicon/05_english/sources/CSpell_LICENSE-MIT.txt lexicon/05_english/sources/typos_LICENSE-MIT.txt \
  lexicon/05_english/sources/sources.json; do
  install -Dm644 "$repo_root/assets/$doc" "$stage/usr/share/doc/qingjian-fcitx5/assets/$doc"
done
python3 "$packaging/metadata.py" "$stage" "$repo_root" "$debian_version" "$architecture" "$shlibs" "$commit"
(cd "$resources" && find assets data -type f ! -name SHA256SUMS ! -name build-info.json -print0 | sort -z | xargs -0 -r sha256sum > SHA256SUMS)
(cd "$resources" && sha256sum --check --quiet SHA256SUMS)
package="$output_dir/qingjian-fcitx5_${debian_version}_${architecture}.deb"
dpkg-deb --build --root-owner-group --threads-max=2 -Zxz -z6 "$stage" "$work_dir/package.deb"
mv "$work_dir/package.deb" "$package"
(cd "$output_dir" && sha256sum "$(basename "$package")" > "$(basename "$package").sha256")
dpkg-deb --info "$package"
printf '已生成 Debian 包：%s\n' "$package"
