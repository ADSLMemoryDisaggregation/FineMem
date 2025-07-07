#!/bin/bash

printf "Client Number, FineMem, Premmap-One-sided, Premmap-RPC\n" > fusee_4kb.csv

printf "16, " >> fusee_4kb.csv
./fusee_bench.sh 1 1 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 1 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 1 "fusee" "fusee_4kb.csv" 4096 60
printf "\n32, " >> fusee_4kb.csv

./fusee_bench.sh 1 2 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 2 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 2 "fusee" "fusee_4kb.csv" 4096 60
printf "\n48, " >> fusee_4kb.csv

./fusee_bench.sh 1 3 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 3 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 3 "fusee" "fusee_4kb.csv" 4096 60
printf "\n64, " >> fusee_4kb.csv

./fusee_bench.sh 1 4 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 4 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 4 "fusee" "fusee_4kb.csv" 4096 60
printf "\n80, " >> fusee_4kb.csv

./fusee_bench.sh 1 5 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 5 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 5 "fusee" "fusee_4kb.csv" 4096 60
printf "\n96, " >> fusee_4kb.csv

./fusee_bench.sh 1 6 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 6 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 6 "fusee" "fusee_4kb.csv" 4096 60
printf "\n112, " >> fusee_4kb.csv

./fusee_bench.sh 1 7 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 7 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 7 "fusee" "fusee_4kb.csv" 4096 60
printf "\n128, " >> fusee_4kb.csv

./fusee_bench.sh 1 8 "pool" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 8 "cxl" "fusee_4kb.csv" 4096 60
printf ", " >> fusee_4kb.csv
./fusee_bench.sh 1 8 "fusee" "fusee_4kb.csv" 4096 60
