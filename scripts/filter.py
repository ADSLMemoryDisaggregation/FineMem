import re
from datetime import datetime
import math
import sys

# 定义处理数据的函数
def process_strace_file(file_path):
    # 打开文件并读取内容
    with open(file_path, 'r') as f:
        lines = f.readlines()
    
    # 用于存储筛选出来的数据
    filtered_data = []
    
    # 第一行的时间，后续行与第一行的时间做差
    first_time = None
    
    # 遍历每一行进行筛选
    for line in lines:
        # 正则匹配mprotect系统调用及其参数
        match = re.match(r'(\d+)\s+([\d:.]+)\s+mprotect\((0x[0-9a-f]+),\s*(\d+),\s*(PROT_READ\|PROT_WRITE)', line)
        if match:
            timestamp = match.group(2)  # 时间戳
            mem_size = int(match.group(4))  # 第二个参数内存大小
            
            mem_size = math.ceil(math.log(mem_size, 2)) - int(math.log(4096, 2))

            if mem_size > 9:
                mem_size = 9

            # 如果是第一次出现的时间
            if first_time is None:
                first_time = datetime.strptime(timestamp, "%H:%M:%S.%f")
                continue
            
            # 计算当前时间与第一行时间的差
            current_time = datetime.strptime(timestamp, "%H:%M:%S.%f")
            time_diff = current_time - first_time
            time_diff_seconds = time_diff.total_seconds() * 1e6 # 转换为秒
            
            # 存储符合条件的数据
            filtered_data.append((time_diff_seconds, mem_size))
        else:
            match = re.match(r'(\d+)\s+([\d:.]+)\s+mmap\((NULL|0x[0-9a-f]+),\s*(\d+),\s*(PROT_[A-Z|_]+),\s*(MAP_[A-Z|_]+),\s*(-?\d+),\s*(\d+)', line)
            if match:
                timestamp = match.group(2)  # 时间戳
                mem_size = int(match.group(4))  # 第二个参数内存大小
                flags = match.group(6)
                if 'MAP_NORESERVE' not in flags:
                    continue
                mem_size = math.ceil(math.log(mem_size, 2)) - int(math.log(4096, 2))
                # print(mem_size)

                if mem_size > 9:
                    mem_size = 9

                # 如果是第一次出现的时间
                if first_time is None:
                    first_time = datetime.strptime(timestamp, "%H:%M:%S.%f")
                    # continue
                
                # 计算当前时间与第一行时间的差
                current_time = datetime.strptime(timestamp, "%H:%M:%S.%f")
                time_diff = current_time - first_time
                time_diff_seconds = time_diff.total_seconds() * 1e6 # 转换为秒
                
                # 存储符合条件的数据
                filtered_data.append((time_diff_seconds, mem_size))
    
    return filtered_data

# 输出处理结果
def output_filtered_data(filtered_data):
    for time_diff, mem_size in filtered_data:
        # print(f"Time Diff: {time_diff:.6f} seconds, Memory Size: {mem_size} bytes")
        print(f"{time_diff:.0f} {mem_size}")

if __name__ == "__main__":
    # 输入文件路径
    file_path = sys.argv[1]
    
    # 处理文件
    filtered_data = process_strace_file(file_path)
    
    # 输出筛选后的数据
    output_filtered_data(filtered_data)
