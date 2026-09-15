#!/usr/bin/env bash
# 默认前缀布局中，卸载只能删除程序资源，不能删除同目录的用户学习文件。
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
test_prefix=$(mktemp -d)
trap 'rm -rf -- "$test_prefix"' EXIT
qingjian_data="$test_prefix/share/qingjian"
mkdir -p "$qingjian_data/resources/assets/sample" "$qingjian_data/dicts"
printf '你好\t7\n' > "$qingjian_data/user.tsv"
printf '用户词库\n' > "$qingjian_data/dicts/custom.tsv"
printf '产品词库\n' > "$qingjian_data/resources/assets/sample/dict.tsv"
bash "$repo_root/apps/linux/scripts/uninstall.sh" --prefix "$test_prefix"
[[ -f "$qingjian_data/user.tsv" && -f "$qingjian_data/dicts/custom.tsv" ]]
[[ ! -e "$qingjian_data/resources" ]]
[[ $(cat "$qingjian_data/user.tsv") = $'你好\t7' ]]
