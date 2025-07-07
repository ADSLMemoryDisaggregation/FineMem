#!/bin/bash

# sudo echo 16000 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

sudo ~/FineMem/scripts/set_2MB_hugepage.sh 20000

nohup redis-server --bind 10.10.1.1 --port 2222 --protected-mode no &

nohup ~/FineMem/build/source/server mlx5_2 10.10.1.1 1111 &