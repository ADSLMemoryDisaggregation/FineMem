import itertools
import random
import workloads_simu as workloads
import subprocess


max_far = 1310720


workload2 = ['xgboost', 'redis', 'snappy']


f1 = open('large_traces/res.txt', 'w')
f2 = open('large_traces/detailed_res.txt', 'w')




for i in range(500):
    selected_workloads = workload2
    min_time = min(workloads.get_workload_class(w).y[0] for w in selected_workloads)
    min_mem = min(workloads.get_workload_class(w).ideal_mem for w in selected_workloads)
    #workload_values = [(min_time / workloads.get_workload_class(w).y[0]) * (min_mem / workloads.get_workload_class(w).ideal_mem) * (0.3 + 1 - workloads.get_workload_class(w).min_ratio) for w in selected_workloads]
    workload_values2 = [random.uniform(1.6, 2.4) for w in workload2]
    workload_values = [random.uniform(1, 3) for w in workload2]
    total_value = sum(workload_values)
    workload_ratios = [round(1000 * v / total_value) for v in workload_values]


    while sum(workload_ratios) > 1000:
        max_index = workload_ratios.index(max(workload_ratios))
        workload_ratios[max_index] -= 1
    while sum(workload_ratios) < 1000:
        min_index = workload_ratios.index(min(workload_ratios))
        workload_ratios[min_index] += 1

    workload_min_ratios = [int(workloads.get_workload_class(w).min_ratio * 100) for w in selected_workloads]
    randseed = random.randint(1,10000)

    cmd = 'python simulation_one_time.py' + ' ' + '{}'.format(randseed) + ' ' + '--num_servers 40 --cpus 64 --mem 81920' + ' '\
            + '--workload ' + ','.join(selected_workloads) + '   ' + '--use_shrink' + ' '\
            + '--ratios ' + ':'.join(map(str, workload_ratios)) + ' ' \
            + '--workload_ratios ' + ','.join(map(str, workload_min_ratios)) + ' '\
            + '--remotemem --until 2000 --size 10000 --max_far {}'.format(max_far)
    
    process1 = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    output1, error = process1.communicate()

    cmd += ' --use_fastswap' + ' '
    process2 = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    output2, error = process2.communicate()

    output1 = output1.decode("utf-8").strip()
    output2 = output2.decode("utf-8").strip()
    #print(output1)
    #print(output2)
    f1.write('{}'.format(float(output2)/float(output1)) + ' \n')   
    f2.write('Workloads:{}'.format(selected_workloads) + ' ' + 'seed:{}'.format(randseed) + ' ' + \
             ':'.join(map(str, workload_ratios)) + '\t  FineMem-Swap:{}, FastSwap:{} \n'.format( int(output1), int(output2)))
    f1.flush()
    f2.flush()
