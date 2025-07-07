#!/bin/bash

redis-cli -h 10.10.1.1 -p 2222 SET bench_start 0 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET avg 0 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET finished 0 >/dev/null 2>&1

ps aux | grep "redis-server 10.10.1.1:2222" | awk '{print $2}' | xargs kill

ps aux | grep "server mlx5_2 10.10.1.1 1111" | awk '{print $2}' | xargs kill

#!/bin/bash

for i in $(seq $1 $2) 
do
    ssh X1aoyang@node$i "~/FineMem/scripts/stop_server.sh"
done