import pandas as pd
import matplotlib.pyplot as plt

plt.rcParams['font.size'] = 20

data1 = pd.read_csv('fusee_4kb.csv') 
data2 = pd.read_csv('fusee_2mb.csv') 

x1 = data1.iloc[:, 0]  
x_labels1 = data1.columns[0]  

y_labels1 = data1.columns[1:]
y1 = [data1[label] for label in y_labels1]

x2 = data2.iloc[:, 0] 
x_labels2 = data2.columns[0] 

y_labels2 = data2.columns[1:]
y2 = [data2[label] for label in y_labels2]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5), sharey=False) 

markers = ['o', 's', 'D', 'v', '^']
colors = ['#d20962','#f47721', '#7ac143','#00bce4','#7d3f98','#00a78e']

for i in range(len(y_labels1)):
    ax1.plot(x1, y1[i], label=y_labels1[i], linewidth=3, marker=markers[i % len(markers)], color=colors[i % len(colors)])
ax1.set_xlabel(x_labels1)
ax1.set_ylabel('Throughput (Mops)')
ax1.set_title('4KB Block')
ax1.set_xlim(min(x1), max(x1))
ax1.set_xticks(x1)

for i in range(len(y_labels2)):
    ax2.plot(x2, y2[i], label=y_labels2[i], linewidth=3, marker=markers[i % len(markers)], color=colors[i % len(colors)])
ax2.set_xlabel(x_labels2)
ax2.set_ylabel('Throughput (Mops)') 
ax2.set_title('2MB Block')
ax2.set_xlim(min(x2), max(x2))
ax2.set_xticks(x2)

lines_labels = [ax.get_legend_handles_labels() for ax in [ax1]]
lines, labels = [sum(lol, []) for lol in zip(*lines_labels)]
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 1.00), ncol=4, fontsize=16)


plt.subplots_adjust(left=0.1, right=0.9, bottom=0.2, top=0.85, wspace=0.3)


plt.show()
