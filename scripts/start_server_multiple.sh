#!/bin/bash

sudo ~/FineMem/scripts/set_2MB_hugepage.sh 20000

nohup redis-server --bind 10.10.1.1 --port 2222 --protected-mode no &

nohup ~/FineMem/build/source/server mlx5_2 10.10.1.1 1111 &

for i in $(seq $1 $2) 
do
    ip=$( echo "$i + 1" | bc )
    ssh X1aoyang@node$i "sudo ~/FineMem/scripts/set_2MB_hugepage.sh 50000"
    ssh X1aoyang@node$i "~/FineMem/build/source/server mlx5_2 10.10.1.$ip 1111 >/dev/null 2>&1 &"
done