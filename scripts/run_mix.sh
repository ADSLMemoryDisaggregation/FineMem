#!/bin/bash

printf "Single/Mix, FineMem, Premmap-One-sided, Premmap-RPC\n" > lat.csv
printf "Single/Mix, FineMem, Premmap-One-sided, Premmap-RPC\n" > band.csv

./fresh_all.sh

printf "Mix, " >> lat.csv
printf "Mix, " >> band.csv
./start_mix.sh "pool" "lat.csv" "band.csv"
./fresh_all.sh
printf ", " >> lat.csv
printf ", " >> band.csv
./start_mix.sh "cxl" "lat.csv" "band.csv"
./fresh_all.sh
printf ", " >> lat.csv
printf ", " >> band.csv
./start_mix.sh "fusee" "lat.csv" "band.csv"
./fresh_all.sh
printf "\n " >> lat.csv
printf "\n " >> band.csv

printf "Single, " >> lat.csv
./start_microbench.sh 1 8 16 "pool" 0 "lat.csv"
./fresh_all.sh
printf ", " >> lat.csv
./start_microbench.sh 1 8 16 "cxl" 0 "lat.csv"
./fresh_all.sh
printf ", " >> lat.csv
./start_microbench.sh 1 8 16 "fusee" 0 "lat.csv"
./fresh_all.sh
printf "\n " >> lat.csv

printf "Single, " >> band.csv
./fusee_bench.sh 1 8 "pool" "band.csv" 4096 10
./fresh_all.sh
printf ", " >> band.csv
./fusee_bench.sh 1 8 "cxl" "band.csv" 4096 10
./fresh_all.sh
printf ", " >> band.csv
./fusee_bench.sh 1 8 "fusee" "band.csv" 4096 10
./fresh_all.sh
printf "\n " >> band.csv
