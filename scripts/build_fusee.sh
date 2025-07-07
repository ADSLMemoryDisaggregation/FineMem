sudo apt install -y jq
mkdir ~/FineMem/applications
cd ~/FineMem/applications
git clone https://github.com/ADSLMemoryDisaggregation/FUSEE_FineMem.git
cd FUSEE_FineMem/
git stash; git pull
cp ~/FineMem/include/cpu_cache.h ./src/
cp ~/FineMem/include/free_block_manager.h ./src/
cp ~/FineMem/include/msg.h ./src/
cp ~/FineMem/include/rdma_conn* ./src/
cp ~/FineMem/build/source/libmralloc.a ./lib/
mkdir build; cd build;
cmake ..; make -j
cd ycsb-test; make -j; cd ..
cd ../setup; ./download_workload.sh
cd ../; mkdir build/ycsb-test/workloads
cp ./setup/workloads/* ./build/ycsb-test/workloads/
rm ./setup/workloads/*
cp ./ycsb-test/split-workload.py ./build/ycsb-test/
jq --arg i "$1" '.server_id = (16 * (($i | tonumber) - 1) + 1)' ./tests/client_config.json > tmp.json && mv tmp.json ./tests/client_config.json
cp ./tests/server_config.json ./build/
