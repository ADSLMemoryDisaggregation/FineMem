# FineMem - Main Repo

FineMem is a distributed remote memory management system designed to support fine-grained, high-performance memory (de)allocation in RDMA-connected DM. This ReadMe has four part, first is how to build FineMem, then an easy case to run FineMem, following the content explaniation and experiments reproduction.

## Build 

* Recommendate running on `Cloudlab r650/r6525/c6525/d6515/xl170` 

* OS & Driver version
  
  * Ubuntu 22.04/Ubuntu 20.04

  * MLNX_OFED 5.x/4.x

* All nodes need install drivers and libraries as following commands:

```shell
> git clone https://github.com/MemoryDisaggregation/FineMem.git
> cd FineMem/
> sudo ./scripts/env_setup.sh # Warning: node will reboot here
> mkdir build; cd build
> cmake ..; make 
```

## Hello-World Example

### Memory-side FineMem:

using `ibdev2netdev` to show which RNIC(with status UP) you can use.

```shell
> sudo ./scripts/set_2MB_hugepage.sh 200000 
> cd ./build/source
> nohup redis-server --bind 10.10.1.1 --port 2222 --protected-mode no & # redis server needed for cross-node synch
> ./server mlx_3<available RDMA device name> 10.10.1.1<server RDMA IP> 1234<serverport>
```


### Compute-side FineMem service:

```shell
> sudo ./scripts/set_2MB_hugepage.sh 2000 
> cd ./build/source
> ./client ../../config/config.json
```
client-side config file:
```json
{
    "node_id":1,
    "rdma_cm_port":1111,
    "memory_node_num":1,
    "memory_ips":[
        "10.10.1.1"
    ]
}
```

### Compute-side Microbench:

```
> cd ./build/microbench
> ./microbench_common 10.10.1.1 1234 16<thread number> 0<size 0-9> pool<allocator type from "cxl", "fusee", "pool">, 1<total node number>
```

## Code content

### Interfaces

When using libmralloc.a, the following libraries are notable:

* ./include/cpu_cache.h: semaphore-based message transfer interface between user applications and server applications.

* ./include/msg.h: RDMA connection settings and RPC packet content defininations
  
* ./include/rdma_conn.h: FineMem communication and raw allocation transaction interfaces
  
* ./include/rdma_conn_manager.h: FineMem communication group manager interfaces
  
* ./include/free_block_magnaer.h: FineMem metadata management and recovery interfaces
  

### Source Code

Core source files to better understand the implementation:
  
* ./source/computing_node.cc: FineMem compute node service
  
* ./source/memory_node.cc: FineMem memory server 

* ./source/free_block_manager.cc: FineMem metadata management and recovery

* ./source/rdma_conn.cc: FineMem communication and allocation transaction



## Evaluation Reproducing

**Critical Path Considerations:**
If your working directory differ from our scripts default home directory reference, please ensure absolute path specification in all these scripts. Our distributed execution (by ssh) initializes from $HOME, making relative path resolution potentially unstable across nodes. You can use command like:
```shell
find -name '*.sh' | xargs perl -pi -e 's|~/FineMem|~/path/to/your/directory/FineMem|g'
```
in the directory ./scripts to replace original path to your work path.

### Microbench

These scripts are configured for an 18-node cluster, numbered 0 through 17. 

After confirming passwordless SSH access (using RSA authentication) from node 0 to nodes 1-17, users can log in to node 0 as the control node (which also serves as the memory node) and execute the scripts from there to manage nodes 1-17 (functioning as compute nodes, with potentially additional memory nodes specified in the "run_different_node" configuration). The results will be collected in CSV format. 

```shell
# execute at node 0
> cd FineMem/scripts/microbench
> ./run_different_size_16.sh # memory node 0, computing node 1-16, about 2 hours, result in different_size_16.csv
> ./run_different_size_128.sh # memory node 0, computing node 1-16, about 2 hours, result in different_size_128.csv
> ./run_different_thread.sh # memory node 0, computing node 1-16, about 2 hours, result in different_thread.csv
> ./run_different_node.sh # memory node 0-5, computing node 6-17, about 2 hours result in different_node.csv
> python3 size_alloc.py
> python3 thread_alloc.py
```

It is recommended to use nohup or tmux to ensure uninterrupted long-term execution. If the scripts terminate abnormally, please first run ./scripts/fresh_all.sh to reset both memory-node and compute-node configurations.

### DM KV-Store System

Similar to the microbenchmark scripts, ​​these​​ scripts can also be ​​executed from node 0 with a single command​​.

```shell
# execute at node 0
> cd FineMem/scripts/
> ./batch_build_fusee.sh 
> ./run_fusee_2MB.sh # memory node 0, computing node 1-16, about 2 hours, result in fusee_4kb.csv
> ./run_fusee_4KB.sh # memory node 0, computing node 1-16, about 3-4 hours, result in fusee_2mb.csv
> python3 draw_kv.py
```

### DM Swap System

About the running of simulator please see Readme in folder Swap_FineMem_Sim.
