#!/bin/bash
# Usage:
#   ./idf.sh build          # 编译
#   ./idf.sh flash monitor  # 烧录+监视
#   ./idf.sh menuconfig     # 配置菜单
export IDF_PATH=~/workspace/esp-idf
source "$IDF_PATH/export.sh"
cd "$(dirname "$0")"
idf.py "$@"
