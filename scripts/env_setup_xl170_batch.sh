#!/bin/bash

for i in $(seq 1 17) 
do
    ssh X1aoyang@node$i "git clone https://github.com/MemoryDisaggregation/FineMem; cd ./FineMem/scripts/; sudo ./env_setup_xl170.sh >/dev/null 2>&1 &"
done

