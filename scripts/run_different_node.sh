#!/bin/bash

printf "Memory Node Number, FineMem, Premmap-One-sided, Premmap-RPC\n" > different_node.csv

printf "1, " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "pool" 0 "different_node.csv" 1
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "cxl" 0 "different_node.csv" 1
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "fusee" 0 "different_node.csv" 1
printf "\n2, " >> different_node.csv

./start_microbench_multiple.sh 6 13 16 "pool" 0 "different_node.csv" 2
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "cxl" 0 "different_node.csv" 2
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "fusee" 0 "different_node.csv" 2
printf "\n3, " >> different_node.csv

./start_microbench_multiple.sh 6 13 16 "pool" 0 "different_node.csv" 3
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "cxl" 0 "different_node.csv" 3
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "fusee" 0 "different_node.csv" 3
printf "\n4, " >> different_node.csv

./start_microbench_multiple.sh 6 13 16 "pool" 0 "different_node.csv" 4
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "cxl" 0 "different_node.csv" 4
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "fusee" 0 "different_node.csv" 4
printf "\n5, " >> different_node.csv

./start_microbench_multiple.sh 6 13 16 "pool" 0 "different_node.csv" 5
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "cxl" 0 "different_node.csv" 5
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "fusee" 0 "different_node.csv" 5
printf "\n6, " >> different_node.csv

./start_microbench_multiple.sh 6 13 16 "pool" 0 "different_node.csv" 6
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "cxl" 0 "different_node.csv" 6
printf ", " >> different_node.csv
./start_microbench_multiple.sh 6 13 16 "fusee" 0 "different_node.csv" 6
printf "\n" >> different_node.csv
