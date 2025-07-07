#!/bin/bash

./stop_remote_client.sh $1 $2 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1

./start_server.sh >/dev/null 2>&1

sleep 10

./start_remote_client.sh $1 $2 >/dev/null 2>&1

sleep 30

node_num=$(echo "$2 - $1 + 1" | bc)

redis-cli -h 10.10.1.1 -p 2222 SET stage2 $node_num >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET avg_lat 0 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET finished 0 >/dev/null 2>&1

for i in $(seq $1 $2) 
do
    ssh X1aoyang@node$i "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 $3 $5 $4 $node_num >log 2>&1 &"
done

while true; do
    start_server=$( redis-cli -h 10.10.1.1 -p 2222 GET "stage2" )
    current_value=$( redis-cli -h 10.10.1.1 -p 2222 GET "finished" )
    echo "Start Server: $start_server, Finishied Server: $current_value, Target: $node_num"
    if [ "$current_value" = "$node_num" ]; then
        # echo "Value matched. Exiting."
        break
    fi

    sleep 1
done

average1=$( echo "scale=3; $(redis-cli -h 10.10.1.1 -p 2222 GET avg_lat) / $node_num" | bc )
printf $average1 >> $6

./stop_remote_client.sh $1 $2 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1