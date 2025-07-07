import workloads_simu as workloads
import subprocess

import random
import math
import os


#workload1 = ['pagerank', 'xsbench']
#workload2 = [ 'xgboost', 'snappy', 'redis']
selected_workloads = ['xgboost', 'snappy', 'redis']
f1 = open('small_traces/FineMemSwap.txt', 'w')
f2 = open('small_traces/FastSwap.txt', 'w')
f3 = open('small_traces/res.txt', 'w')

max_far = 163840


for i in range(500):
    min_time = min(workloads.get_workload_class(w).y[0] for w in selected_workloads)
    min_mem = min(workloads.get_workload_class(w).ideal_mem for w in selected_workloads)

    
    workload_values = [random.uniform(1, 4.5) for w in selected_workloads]
    total_value = sum(workload_values)
    workload_ratios = [round(100 * v / total_value) for v in workload_values]


    while sum(workload_ratios) > 100:
        max_index = workload_ratios.index(max(workload_ratios))
        workload_ratios[max_index] -= 1
    while sum(workload_ratios) < 100:
        min_index = workload_ratios.index(min(workload_ratios))
        workload_ratios[min_index] += 1



    workload_min_ratios = [int(workloads.get_workload_class(w).min_ratio * 100) for w in selected_workloads]
    
    rand = random.randint(1,10000)
    trace = 'python simulation_one_time.py' + ' ' + '{}'.format(rand) + ' ' + '--num_servers 5 --cpus 64 --mem 81920' + ' '\
            + '--workload ' + ','.join(selected_workloads) + '   ' + '--use_shrink' + ' '\
            + '--ratios ' + ':'.join(map(str, workload_ratios)) + ' ' \
            + '--workload_ratios ' + ','.join(map(str, workload_min_ratios)) + ' '\
            + '--remotemem --until 200 --size 200 --max_far {}'.format(max_far)

    trace2 = 'python simulation_one_time.py' + ' ' + '{}'.format(rand) + ' ' + '--num_servers 5 --cpus 64 --mem 81920' + ' '\
            + '--workload ' + ','.join(selected_workloads) + '   ' + '--use_shrink' + ' '\
            + '--ratios ' + ':'.join(map(str, workload_ratios)) + ' ' \
            + '--workload_ratios ' + ','.join(map(str, workload_min_ratios)) + ' '\
            + '--remotemem --until 200 --size 200 --max_far {}'.format(max_far) + ' ' + '--use_fastswap'
    
    process1 = subprocess.Popen(trace, shell=True, stdout=subprocess.PIPE)
    output1, error = process1.communicate()

    process2 = subprocess.Popen(trace2, shell=True, stdout=subprocess.PIPE)
    output2, error = process2.communicate()

    output1 = output1.decode("utf-8").strip()
    output2 = output2.decode("utf-8").strip()

    f1.write(str(trace) + '\n')
    f2.write(str(trace2) + '\n')
    f3.write('{}'.format(float(output2)/float(output1)) + ' \n')  

    f1.flush()
    f2.flush()
    f3.flush() 