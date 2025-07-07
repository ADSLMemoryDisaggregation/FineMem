#!/bin/bash

printf "Thread Number, FineMem, Premmap-One-sided, Premmap-RPC\n" > different_thread.csv

printf "16, " >> different_thread.csv
./start_microbench.sh 1 8 2 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 2 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 2 "fusee" 0 "different_thread.csv"
printf "\n32, " >> different_thread.csv

./start_microbench.sh 1 8 4 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 4 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 4 "fusee" 0 "different_thread.csv"
printf "\n48, " >> different_thread.csv

./start_microbench.sh 1 8 6 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 6 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 6 "fusee" 0 "different_thread.csv"
printf "\n64, " >> different_thread.csv

./start_microbench.sh 1 8 8 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 8 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 8 "fusee" 0 "different_thread.csv"
printf "\n80, " >> different_thread.csv

./start_microbench.sh 1 8 10 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 10 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 10 "fusee" 0 "different_thread.csv"
printf "\n96, " >> different_thread.csv

./start_microbench.sh 1 8 12 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 12 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 12 "fusee" 0 "different_thread.csv"
printf "\n112, " >> different_thread.csv

./start_microbench.sh 1 8 14 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 14 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 14 "fusee" 0 "different_thread.csv"
printf "\n128, " >> different_thread.csv

./start_microbench.sh 1 8 16 "pool" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 16 "cxl" 0 "different_thread.csv"
printf ", " >> different_thread.csv
./start_microbench.sh 1 8 16 "fusee" 0 "different_thread.csv"
printf "\n" >> different_thread.csv