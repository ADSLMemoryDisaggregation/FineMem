import subprocess
import sys
import matplotlib.pyplot as plt

colors = ['#d20962', '#7ac143', '#f47721']
labels = ['FineMem-Swap', 'FastSwap', 'Total Remote Memory']
plt.rcParams['font.size'] = 16

def run_simulation(seed_value):
    cmd1 = [
        "python", "simulation_one_time.py",
        str(seed_value),
        "--num_servers", "5",
        "--cpus", "64",
        "--mem", "81920",
        "--workload", "snappy,xgboost,redis",
        "--use_shrink",
        "--ratios", "2:2:1",
        "--workload_ratios", "80,50,60",
        "--remotemem",
        "--until", "200",
        "--size", "200",
        "--max_far", "163840",
        "--print_mem"
    ]
    
    cmd2 = cmd1.copy()
    cmd2.append("--use_fastswap")
    
    result1 = subprocess.run(cmd1, capture_output=True, text=True)
    if result1.stderr:
        print("error_finememswap:", result1.stderr)
    
    result2 = subprocess.run(cmd2, capture_output=True, text=True)
    if result2.stderr:
        print("error_fastswap", result2.stderr)


def read_data(filename):
    with open(filename, 'r') as file:
        data = [float(line.strip()) for line in file if line.strip()]
    return data

def plot_data(file1, file2):

    data1 = read_data(file1)
    data2 = read_data(file2)
    data1 = [d / 1024 for d in data1]
    data2 = [d / 1024 for d in data2]
    data3 = [160] * max(len(data1), len(data2))
    
    plt.figure(figsize=(10, 6))
    
    plt.plot(data1, label=labels[0], linestyle='-', linewidth=3, color=colors[0])
    plt.plot(data2, label=labels[1], linestyle='-', linewidth=3, color=colors[1])
    plt.plot(data3, label=labels[2], linestyle='--', linewidth=3, color=colors[2])

    plt.xlabel('Time(s)')
    plt.ylabel('Remote Memory(GB)')
    
    plt.legend()
    
    plt.grid(True, linestyle='--', alpha=0.7)
    
    plt.savefig("figures/remote_mem_usage.png")
    plt.savefig("figures/remote_mem_usage.pdf")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("need random_seed to run.")
        print("example: python run_simulation.py <random_seed>.")
        sys.exit(1)
    
    try:
        seed_value = int(sys.argv[1])
        run_simulation(seed_value)
    except ValueError:
        print("error random_seed type.")
        sys.exit(1)

    file1 = 'remote_mem_usage/finememswap.txt'  
    file2 = 'remote_mem_usage/fastswap.txt' 
    
    plot_data(file1, file2)