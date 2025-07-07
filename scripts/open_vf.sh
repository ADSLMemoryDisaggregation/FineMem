#!/bin/bash

echo 4 > /sys/class/net/enp202s0f0np0/device/sriov_numvfs

./set_mac.sh 4 enp202s0f0np0

sudo ip addr flush dev enp202s0f0v0
sudo ip addr add 10.10.1.20/24 dev enp202s0f0v0 
sudo ip link set enp202s0f0v0 up
sudo ifconfig enp202s0f0v0 mtu 9000
sudo ip addr flush dev enp202s0f0v1
sudo ip addr add 10.10.1.21/24 dev enp202s0f0v1 
sudo ip link set enp202s0f0v1 up
sudo ifconfig enp202s0f0v1 mtu 9000
sudo ip addr flush dev enp202s0f0v2
sudo ip addr add 10.10.1.22/24 dev enp202s0f0v2 
sudo ip link set enp202s0f0v2 up
sudo ifconfig enp202s0f0v2 mtu 9000
sudo ip addr flush dev enp202s0f0v3
sudo ip addr add 10.10.1.23/24 dev enp202s0f0v3 
sudo ip link set enp202s0f0v3 up
sudo ifconfig enp202s0f0v3 mtu 9000

sudo arp -s 10.10.1.20 00:11:22:22:22:00 -i enp202s0f0v0
sudo arp -s 10.10.1.21 00:11:22:22:22:01 -i enp202s0f0v1
sudo arp -s 10.10.1.22 00:11:22:22:22:02 -i enp202s0f0v2
sudo arp -s 10.10.1.23 00:11:22:22:22:03 -i enp202s0f0v3