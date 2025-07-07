#!/bin/bash

printf "Client Number, FineMem, Premmap-One-sided, Premmap-RPC\n" > fusee_2mb.csv

printf "16, " >> fusee_2mb.csv
./fusee_bench.sh 1 2 "pool" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 2 "cxl" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 2 "fusee" "fusee_2mb.csv" 2097152 60
printf "\n32, " >> fusee_2mb.csv

./fusee_bench.sh 1 4 "pool" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 4 "cxl" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 4 "fusee" "fusee_2mb.csv" 2097152 60
printf "\n48, " >> fusee_2mb.csv

./fusee_bench.sh 1 6 "pool" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 6 "cxl" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 6 "fusee" "fusee_2mb.csv" 2097152 60
printf "\n64, " >> fusee_2mb.csv

./fusee_bench.sh 1 8 "pool" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 8 "cxl" "fusee_2mb.csv" 2097152 60
printf ", " >> fusee_2mb.csv
./fusee_bench.sh 1 8 "fusee" "fusee_2mb.csv" 2097152 60
printf "\n80, " >> fusee_2mb.csv

# ./fusee_bench.sh 1 60 "pool" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 60 "cxl" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 60 "fusee" "fusee_2mb.csv" 2097152 60
# printf "\n96, " >> fusee_2mb.csv

# ./fusee_bench.sh 1 12 "pool" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 12 "cxl" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 12 "fusee" "fusee_2mb.csv" 2097152 60
# printf "\n112, " >> fusee_2mb.csv

# ./fusee_bench.sh 1 14 "pool" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 14 "cxl" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 14 "fusee" "fusee_2mb.csv" 2097152 60
# printf "\n128, " >> fusee_2mb.csv

# ./fusee_bench.sh 1 16 "pool" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 16 "cxl" "fusee_2mb.csv" 2097152 60
# printf ", " >> fusee_2mb.csv
# ./fusee_bench.sh 1 16 "fusee" "fusee_2mb.csv" 2097152 60
