#!/bin/bash

for i in $(seq 1 17) 
do
    ssh X1aoyang@node$i "sudo ~/FineMem/scripts/env_setup_xl170.sh >/dev/null 2>&1 &"
done