# Overview

This simulator is a modified implementation based on the open-source [simulator](https://github.com/clusterfarmem/clustersim.git) of [FastSwap](https://dl.acm.org/doi/pdf/10.1145/3342195.3387522) (EuroSys'20). It is designed to conveniently reproduce the experimental results of the swap system presented in our paper.

This README is divided into two sections: the first introduces how the simulator works and outlines the differences between simulating FastSwap and FineMem-Swap. The second explains how to reproduce our experimental results.

# Introduction

## DM Swap Systems Simulator

The simulator utilizes multiple simulated compute nodes to model job execution and employs a centralized job scheduler to assign jobs to compute nodes.  Each compute node dynamically adjusts memory configurations (local-remote memory ratios) for its assigned jobs based on pre-profiled performance degradation information, thereby simulating application execution progress. Upon job arrival, the scheduler applies a customized admission policy to identify a suitable compute node for execution. If no eligible node is available, the job is queued and reconsidered for deployment upon subsequent job completion events. The accuracy of this simulation has been validated by FastSwap.

## Job Admission Policy

Our modified simulator employs distinct job admission policies for FastSwap and FineMem-Swap:

- **FastSwap**: A job is admitted if there exists a compute node that can accommodate it without exceeding the fixed per-node remote memory usage limitation (32 GB per node in our setting).
- **FineMem-Swap**: A job is admitted if there exists a compute node that can accommodate it and keeping the total remote memory usage of the entire cluster within total remote memory size (number of compute nodes times 32GB in our setting).

# Run Experiment

The simulator requires several command-line arguments to execute, with complete usage examples available in `run.sh`. This section focus on reproducing the results corresponding to Figure 16 in our paper.

## Prerequisites

Install the required Python dependencies for simulation and plotting:

```sh
sudo apt update && sudo apt install python3-pip

pip install numpy scipy sortedcollections matplotlib
```

## Remote Memory Usage

To reproduce FineMem results shown in **Figure 16(a)**, enter the `clustersim/` directory and run:

```sh
python draw_remote_mem_usage.py <random_seed>

# example: python draw_remote_mem_usage.py 2025
```

`random_seed` is an integer that determines the generated job trace. The script creates a trace of 200 jobs (XGBoost:Snappy:Redis = 2:2:1), simulates it on both FastSwap and FineMem-Swap using 5 compute nodes, and prints the corresponding remote memory usage results to `clustersim/remote_mem_usage/`. It then generates the figure and saves it to `clustersim/figures/remote_mem_usage.png`.

> **Note**: While Figure 16(a) in the paper shows results obtained from a 7-machine testbed, for convenience, the results reproduced during artifact evaluation are generated using this simulator.

## Throughput Improvement

The script `run_small_traces.py` randomly generates 500 traces, each containing 200 jobs, and simulates their execution on both FastSwap and FineMem-Swap with 5 compute nodes. Results for each trace are saved to `clustersim/small_traces/`.

Similarly, `run_large_traces.py` generates and simulates 500 traces with 10,000 jobs each, using 40 compute nodes. Results are saved to `clustersim/large_traces/`.

To reproduce FineMem results in **Figure 16(b)**, run the following commands in the `clustersim/` directory:

```sh
python run_small_traces.py
python run_large_traces.py
python draw_throughput_improvement_cdf.py
```

This will generate the throughput improvement plot at `clustersim/figures/throughput_improvement.png`.

> **Note**: Simulating large traces can be time-consuming. Partial intermediate results can be plotted before all simulations finish.
