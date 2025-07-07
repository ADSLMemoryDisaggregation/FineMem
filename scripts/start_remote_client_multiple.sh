#!/bin/bash

for i in $(seq $1 $2) 
do
    ssh X1aoyang@node$i "sudo ~/FineMem/scripts/set_2MB_hugepage.sh 10000"
    ssh X1aoyang@node$i "sudo ulimit -n 8000"
    ssh X1aoyang@node$i "~/FineMem/build/source/client ~/FineMem/config/config_node_num_$3.json >/dev/null 2>&1 &"
done