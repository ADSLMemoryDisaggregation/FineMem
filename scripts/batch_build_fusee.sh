#!/bin/bash

for i in $(seq $1 $2) 
do
    ssh X1aoyang@node$i "~/FineMem/scripts/build_fusee.sh $i >/dev/null 2>&1 &"
done