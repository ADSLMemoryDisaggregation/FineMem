#!/bin/bash

./stop_remote_client.sh 1 17 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1

./start_server.sh >/dev/null 2>&1

sleep 10

./start_remote_client.sh 1 16 >/dev/null 2>&1

sleep 30

redis-cli -h 10.10.1.1 -p 2222 SET stage1 8 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET stage2 16 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET avg 0 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET avg_lat 0 >/dev/null 2>&1
redis-cli -h 10.10.1.1 -p 2222 SET finished 0 >/dev/null 2>&1

node_num=16

for i in $(seq 1 8) 
do
    ssh X1aoyang@node$i "cd ~/FineMem/applications/FUSEE_FineMem/; jq --arg i "$i" '.server_id = (16 * (($i | tonumber) - 1) + 1)' ./tests/client_config.json > tmp.json && mv tmp.json ./tests/client_config.json"
    ssh X1aoyang@node$i "jq '.block_size = 4096' ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json > tmp.json && mv tmp.json ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json"
    ssh X1aoyang@node$i "jq '.workload_run_time = 10' ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json > tmp.json && mv tmp.json ~/FineMem/applications/FUSEE_FineMem/tests/client_config.json"
    ssh X1aoyang@node$i "cd ~/FineMem/applications/FUSEE_FineMem/build/ycsb-test; python3 split-workload.py 128 >/dev/null 2>&1"
    ssh X1aoyang@node$i "cd ~/FineMem/applications/FUSEE_FineMem/build/ycsb-test; ./ycsb_test_multi_client ../../tests/client_config.json workloada 16 $1 >/dev/null 2>&1 &"
done

# for i in $(seq 9 16) 
# do
ssh X1aoyang@node9 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 0 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node10 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 0 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node11 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 1 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node12 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 1 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node13 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 2 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node14 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 3 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node15 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 4 $1 $node_num >/dev/null 2>&1 &"
ssh X1aoyang@node16 "~/FineMem/build/microbench/microbench_common 10.10.1.1 1111 16 5 $1 $node_num >/dev/null 2>&1 &"
# done


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

average1=$( echo "scale=3; $(redis-cli -h 10.10.1.1 -p 2222 GET avg_lat) / 8" | bc )
printf $average1 >> $2

average2=$( echo "scale=3; $(redis-cli -h 10.10.1.1 -p 2222 GET avg) " | bc )
printf $average2 >> $3

./stop_remote_client.sh 1 16 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1