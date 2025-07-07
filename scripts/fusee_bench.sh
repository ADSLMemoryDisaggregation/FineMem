#!/bin/bash

./stop_remote_client.sh $1 $2 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1

./start_server.sh >/dev/null 2>&1

sleep 30

./start_remote_client.sh $1 $2 >/dev/null 2>&1

sleep 30

node_num=$(echo "$2 - $1 + 1" | bc)

redis-cli -h 10.10.1.1 -p 2222 SET stage1 $node_num >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET stage2 $node_num >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET avg 0 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET finished 0 >/dev/null 2>&1

thread_num=$(echo "$node_num * 16" | bc)

for i in $(seq $1 $2) 
do
    ssh X1aoyang@node$i "cd ~/FineMem/applications/FUSEE_FineMem/; jq --arg i "$i" '.server_id = (16 * (($i | tonumber) - 1) + 1)' ./tests/client_config.json > tmp.json && mv tmp.json ./tests/client_config.json"
    ssh X1aoyang@node$i "jq '.block_size = $5' ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json > tmp.json && mv tmp.json ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json"
    ssh X1aoyang@node$i "jq '.workload_run_time = $6' ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json > tmp.json && mv tmp.json ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json"
    ssh X1aoyang@node$i "cd ~/FineMem/applications/FUSEE_FineMem/build/ycsb-test; python3 split-workload.py $thread_num >/dev/null 2>&1"
    ssh X1aoyang@node$i "cd ~/FineMem/applications/FUSEE_FineMem/build/ycsb-test; ./ycsb_test_multi_client ../../tests/client_config.json workloada 16 $3 >/dev/null 2>&1 &"
done


while true; do
    stage1_server=$( redis-cli -h 10.10.1.1 -p 2222 GET "stage1" )
    stage2_server=$( redis-cli -h 10.10.1.1 -p 2222 GET "stage2" )
    current_value=$( redis-cli -h 10.10.1.1 -p 2222 GET "finished" )
    echo "Sateg1 Server: $stage1_server, Sateg2 Server: $stage2_server, Finishied Server: $current_value, Target: $node_num"
    if [ "$current_value" = "$node_num" ]; then
        # echo "Value matched. Exiting."
        break
    fi

    sleep 1
done

average1=$( echo "scale=3; $(redis-cli -h 10.10.1.1 -p 2222 GET avg)" | bc )
printf $average1 >> $4

./stop_remote_client.sh $1 $2 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1
