import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

plt.rcParams['font.size'] = 14

with open('small_traces/res.txt', 'r') as f:
    data_small = [float(line.strip()) for line in f]
    data_small = [d - 1 for d in data_small]

with open('large_traces/res.txt', 'r') as f:
    data_large = [float(line.strip()) for line in f]
    data_large = [d - 1 for d in data_large]

data_small = np.sort(data_small)
data_large = np.sort(data_large)
p = 1. * np.arange(len(data_small)) / (len(data_small) - 1)
p2 = 1. * np.arange(len(data_large)) / (len(data_large) - 1)

plt.figure(figsize=(8, 5))
l1 = plt.plot(data_small, p, color='#00a78e', linewidth=4)
l2 = plt.plot(data_large, p2, color='#f47721', linewidth=4)
l1_patch = mpatches.Patch(color='#00a78e', label='200 jobs')
l2_patch = mpatches.Patch(color='#f47721', label='10000 jobs')

plt.xlim([-0.06, 0.23]) 
plt.xlabel('Improvement')

plt.legend(handles=[l1_patch, l2_patch])

plt.ylabel('CDF')
#plt.title('Throughput Improvement CDF')
plt.grid(True)
plt.show()
plt.savefig("figures/throughput_improvement.pdf")
plt.savefig("figures/throughput_improvement.png")