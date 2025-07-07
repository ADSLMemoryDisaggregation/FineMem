#!/bin/bash

# usage: sudo ./set_2MB_hugepages.sh [page_size]

sudo echo $1 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages