#!/bin/bash
# Usage:
#   ./idf.sh build          # 编译
#   ./idf.sh flash monitor  # 烧录+监视
#   ./idf.sh menuconfig     # 配置菜单
#   ./idf.sh erase-flash    # 擦除整个flash（清除配网信息等），之后再flash
export IDF_PATH=~/workspace/esp-idf
source "$IDF_PATH/export.sh"
cd "$(dirname "$0")"
idf.py "$@"
