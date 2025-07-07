#!/bin/bash

printf "Allocation size(4KB), FineMem, Premmap-One-sided, Premmap-RPC\n" > different_size_16.csv

printf "1, " >> different_size_16.csv
./start_microbench.sh 1 8 2 "pool" 0 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 0 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 0 "different_size_16.csv"
printf "\n2, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 1 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 1 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 1 "different_size_16.csv"
printf "\n4, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 2 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 2 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 2 "different_size_16.csv"
printf "\n8, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 3 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 3 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 3 "different_size_16.csv"
printf "\n1, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 4 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 4 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 4 "different_size_16.csv"
printf "\n32, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 5 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 5 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 5 "different_size_16.csv"
printf "\n64, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 6 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 6 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 6 "different_size_16.csv"
printf "\n128, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 7 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 7 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 7 "different_size_16.csv"
printf "\n256, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 8 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 8 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 8 "different_size_16.csv"
printf "\n512, " >> different_size_16.csv

./start_microbench.sh 1 8 2 "pool" 9 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "cxl" 9 "different_size_16.csv"
printf ", " >> different_size_16.csv
./start_microbench.sh 1 8 2 "fusee" 9 "different_size_16.csv"
printf "\n" >> different_size_16.csv
