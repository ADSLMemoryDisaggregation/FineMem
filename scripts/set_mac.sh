#!/bin/bash

pcielist=(`ibdev2netdev -v | awk 'NR>="'$1+1'"' | awk '{print $1,ORS=" "}'`)
for i in ${pcielist[@]} 
do    
	echo $i > /sys/bus/pci/drivers/mlx5_core/unbind
done
for i in $(seq 0 $[${#pcielist[@]}-1]) 
do
    ip link set $2 vf $i mac 00:11:22:22:22:0$i
done
for i in ${pcielist[@]}
do
    echo $i > /sys/bus/pci/drivers/mlx5_core/bind
done