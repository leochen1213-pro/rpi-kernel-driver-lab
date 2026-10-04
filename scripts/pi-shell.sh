#!/usr/bin/env bash
# 進入交叉編譯容器：整個 repo 掛載成 /work，並把 Pi 的 headers 掛回原本的絕對路徑。
# 升級 Pi 核心後，只需要改下面的 KBASE（或執行時帶 KBASE=... 覆蓋）。
set -euo pipefail

KBASE="${KBASE:-6.18.50+rpt}"
IMAGE="${IMAGE:-pi-kernel-cross}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

ARCH_DIR="linux-headers-${KBASE}-rpi-v8"
COMMON_DIR="linux-headers-${KBASE}-common-rpi"

for d in "$ARCH_DIR" "$COMMON_DIR"; do
	if [ ! -d "$ROOT/$d" ]; then
		echo "找不到 $ROOT/$d" >&2
		echo "請先從 Pi 用 scp 拷回來，見 README 的「準備 headers」。" >&2
		exit 1
	fi
done

exec docker run --rm -it \
	-e KVER="${KBASE}-rpi-v8" \
	-v "$ROOT":/work \
	-v "$ROOT/$ARCH_DIR":"/usr/src/$ARCH_DIR" \
	-v "$ROOT/$COMMON_DIR":"/usr/src/$COMMON_DIR" \
	-w /work \
	"$IMAGE" bash
