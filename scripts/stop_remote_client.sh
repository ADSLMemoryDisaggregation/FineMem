#!/bin/bash

for i in $(seq $1 $2) 
do
    ssh X1aoyang@node$i "~/FineMem/scripts/stop_local_client.sh"
done