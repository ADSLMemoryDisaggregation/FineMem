
#include <bits/floatn.h>
#include <bits/stdint-uintn.h>
#include <bits/types/FILE.h>
#include <linux/mman.h>
#include <pthread.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include "computing_node.h"
#include "mr_utils.h"
#include <sys/mman.h>
#include <sys/time.h>
#include <signal.h>

const int thread_num = 12;

pthread_barrier_t start_barrier;
pthread_barrier_t end_barrier;
std::ofstream result;
pthread_mutex_t file_lock;

std::atomic<int> record_global[10];
std::atomic<uint64_t> avg;

uint64_t default_node_num = 1;

void* fetch_mem(void* arg) {
    uint64_t avg_time_ = 0;
    uint64_t count_ = 0;
    uint64_t cdf_counter[10];
    uint64_t max_time_ = 0;
    struct timeval start, end;
    mralloc::ComputingNode* heap = (mralloc::ComputingNode*)arg;
    int record[10] = {0};
    mralloc::mr_rdma_addr addr[32];
    
    for(int j = 0; j < 4; j ++) {
        pthread_barrier_wait(&start_barrier);
        gettimeofday(&start, NULL);
        for(int i = 0; i < 32; i ++){
            heap->fetch_mem_block_nocached(addr[i], i%default_node_num);
        }
        gettimeofday(&end, NULL);
        pthread_barrier_wait(&end_barrier);
        char buffer[2][16] = {"aaa", "bbb"};
        char read_buffer[4];  
        uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        if(time > max_time_) max_time_ = time;
        time = time / 32;
        avg_time_ = (avg_time_*count_ + time)/(count_ + 1);
        count_ += 1;
        sleep(1);
    }
    printf("avg time:%lu, max_time:%lu\n", avg_time_, max_time_);
    for(int i=0;i<10;i++){
        record_global[i].fetch_add(record[i]);
    }
    avg.fetch_add(avg_time_);
    return NULL;
}

mralloc::ComputingNode* heap;

bool stopped = false;

void handler(int sig){
    printf("stop!\n");
    stopped = true;
    // heap->stop();
}

int main(int argc, char* argv[]){

    if(argc < 2){
        printf("Usage: %s <config path>\n", argv[0]);
        return 0;
    }

    mralloc::GlobalConfig config;
    mralloc::load_config(argv[1], &config);
    std::string ip[16], port[16];
    for(int i = 0; i < config.memory_node_num; i++){
        ip[i] = std::string(config.memory_ips[i]);
        port[i] = std::to_string(config.rdma_cm_port[i]);
    }
    bool multitest = false;
    if(!multitest) {
        heap = new mralloc::ComputingNode(true, true, true, config.node_id);
        heap->start(ip, port, config.memory_node_num);
        // before the real client running, make a test of iter times allocation
        // << single thread, local test, fetch remote memory >>
        int iter = 0;
        mralloc::mr_rdma_addr addr;
        char buffer[2][64*1024] = {"aaa", "bbb"};
        char read_buffer[4];
        struct timeval start, end;
        gettimeofday(&start, NULL);
        gettimeofday(&end, NULL);
        uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        printf("time cost on read/write test:%lu\n", time);
        signal(SIGTERM, handler);
        signal(SIGINT, handler);
        while(true){
            if(stopped){
                break;
            }
        };
        heap->stop();
        delete heap;
    }

    // << multiple thread, local test, fetch remote memory >>
    else {
        mralloc::ComputingNode* client[thread_num];
        result.open("result.csv");
        for(int i=0;i<10;i++) {
            record_global[i].store(0);
        }
        avg.store(0);
        pthread_mutex_init(&file_lock, NULL);
        pthread_barrier_init(&start_barrier, NULL, thread_num);
        pthread_barrier_init(&end_barrier, NULL, thread_num);
        pthread_t running_thread[thread_num];
        for(int i = 0; i < thread_num; i++) {
            client[i] = new mralloc::ComputingNode(false, false, true, 0);
            client[i]->start(ip, port, config.memory_node_num);
            printf("thread %d\n init success\n", i);
            pthread_create(&running_thread[i], NULL, fetch_mem, client[i]);
        }
        for(int i = 0; i < thread_num; i++) {
            pthread_join(running_thread[i], NULL);
        }
        for(int i = 0; i < thread_num; i++) {
            client[i]->stop();
        }
        for(int i=0;i<10;i++)
            result << record_global[i].load() << std::endl;
        result.close();
        printf("total avg: %luus\n", avg.load()/thread_num);
    }

    return 0;

}