#!/bin/bash

printf "Benchmark, la, le, rp, sh\n" > jemalloc_runtime.csv
printf "Benchmark, la, le, rp, sh\n" > jemalloc_lat.csv

printf "FineMem, " >> jemalloc_runtime.csv
printf "FineMem, " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.larson.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.lean.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.rptest.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.sh8bench.16
printf "\n" >> jemalloc_runtime.csv
printf "\n" >> jemalloc_lat.csv

printf "Premmap-One-sided, " >> jemalloc_runtime.csv
printf "Premmap-One-sided, " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.larson.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.lean.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.rptest.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.sh8bench.16
printf "\n" >> jemalloc_runtime.csv
printf "\n" >> jemalloc_lat.csv

printf "Premmap-RPC, " >> jemalloc_runtime.csv
printf "Premmap-RPC, " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.larson.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.lean.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.rptest.16
printf ", " >> jemalloc_runtime.csv
printf ", " >> jemalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 jemalloc_lat.csv jemalloc_runtime.csv traces/trace.je.sh8bench.16
printf "\n" >> jemalloc_runtime.csv
printf "\n" >> jemalloc_lat.csv

printf "Benchmark, la, le, rp, sh\n" > mimalloc_runtime.csv
printf "Benchmark, la, le, rp, sh\n" > mimalloc_lat.csv

printf "FineMem, " >> mimalloc_runtime.csv
printf "FineMem, " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.larson.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.lean.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.rptest.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.sh8bench.16
printf "\n" >> mimalloc_runtime.csv
printf "\n" >> mimalloc_lat.csv

printf "Premmap-One-sided, " >> mimalloc_runtime.csv
printf "Premmap-One-sided, " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.larson.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.lean.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.rptest.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.sh8bench.16
printf "\n" >> mimalloc_runtime.csv
printf "\n" >> mimalloc_lat.csv

printf "Premmap-RPC, " >> mimalloc_runtime.csv
printf "Premmap-RPC, " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.larson.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.lean.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.rptest.16
printf ", " >> mimalloc_runtime.csv
printf ", " >> mimalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 mimalloc_lat.csv mimalloc_runtime.csv traces/trace.mi.sh8bench.16
printf "\n" >> mimalloc_runtime.csv
printf "\n" >> mimalloc_lat.csv

printf "Benchmark, la, le, rp, sh\n" > ptmalloc_runtime.csv
printf "Benchmark, la, le, rp, sh\n" > ptmalloc_lat.csv

printf "FineMem, " >> ptmalloc_runtime.csv
printf "FineMem, " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.larson.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.lean.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.rptest.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.sh8bench.16
printf "\n" >> ptmalloc_runtime.csv
printf "\n" >> ptmalloc_lat.csv

printf "Premmap-One-sided, " >> ptmalloc_runtime.csv
printf "Premmap-One-sided, " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.larson.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.lean.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.rptest.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.sh8bench.16
printf "\n" >> ptmalloc_runtime.csv
printf "\n" >> ptmalloc_lat.csv

printf "Premmap-RPC, " >> ptmalloc_runtime.csv
printf "Premmap-RPC, " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.larson.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.lean.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.rptest.16
printf ", " >> ptmalloc_runtime.csv
printf ", " >> ptmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 ptmalloc_lat.csv ptmalloc_runtime.csv traces/trace.sys.sh8bench.16
printf "\n" >> ptmalloc_runtime.csv
printf "\n" >> ptmalloc_lat.csv

printf "Benchmark, la, le, rp, sh\n" > tcmalloc_runtime.csv
printf "Benchmark, la, le, rp, sh\n" > tcmalloc_lat.csv

printf "FineMem, " >> tcmalloc_runtime.csv
printf "FineMem, " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.larson.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.lean.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.rptest.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 pool 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.sh8bench.16
printf "\n" >> tcmalloc_runtime.csv
printf "\n" >> tcmalloc_lat.csv

printf "Premmap-One-sided, " >> tcmalloc_runtime.csv
printf "Premmap-One-sided, " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.larson.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.lean.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.rptest.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 cxl 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.sh8bench.16
printf "\n" >> tcmalloc_runtime.csv
printf "\n" >> tcmalloc_lat.csv

printf "Premmap-RPC, " >> tcmalloc_runtime.csv
printf "Premmap-RPC, " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.larson.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.lean.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.rptest.16
printf ", " >> tcmalloc_runtime.csv
printf ", " >> tcmalloc_lat.csv
./start_microbench_trace.sh 1 8 16 fusee 0 tcmalloc_lat.csv tcmalloc_runtime.csv traces/trace.tcg.sh8bench.16
printf "\n" >> tcmalloc_runtime.csv
printf "\n" >> tcmalloc_lat.csv