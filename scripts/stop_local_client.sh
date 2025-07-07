#!/bin/bash

ps aux | grep 'build/source/client' | awk '{print $2}' | xargs kill
ps aux | grep 'microbench' | awk '{print $2}' | xargs kill
ps aux | grep 'ycsb_test_multi_client' | awk '{print $2}' | xargs kill
rm -rf /dev/shm/*
