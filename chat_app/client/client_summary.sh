#!/bin/bash

# 检查是否提供了输出文件名参数
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output_filename>"
    exit 1
fi

output_filename=$1

# 检查client.c和client.h文件是否存在
if [ ! -f client.c ] || [ ! -f client.h ]; then
    echo "Error: client.c and/or client.h do not exist."
    exit 1
fi

# 将client.c的内容写入输出文件
cat client.c > "$output_filename"

# 添加三个换行
echo -e "\n\n\n" >> "$output_filename"

# 将client.h的内容写入输出文件
cat client.h >> "$output_filename"

echo "Files client.c and client.h have been combined into $output_filename"
