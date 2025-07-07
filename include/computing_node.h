
#pragma once

#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <sched.h>
#include <atomic>
#include <set>
#include <unordered_map>
#include <sys/sysinfo.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "msg.h"
#include "rdma_conn.h"
#include "rdma_conn_manager.h"
#include "string"
#include "thread"
#include "unordered_map"
#include "free_block_manager.h"
#include "cpu_cache.h"
#include "rpc_server.h"

namespace mralloc {

const uint32_t ring_buffer_size = 40000;

struct node_info {
    node_info(one_side_info m_one_side_info_){
        block_size_ = m_one_side_info_.block_size_;
        block_num_ = m_one_side_info_.block_num_;
        region_size_ = block_size_ * block_per_region;
        region_num_ = block_num_ / block_per_region;
        section_size_ = region_size_ * region_per_section;
        section_num_ = region_num_ / region_per_section;

        section_header_ = m_one_side_info_.section_header_;

        region_header_ = (uint64_t)((section_e*)section_header_ + section_num_);
        block_rkey_ = (uint64_t)((region_e*)region_header_ + region_num_);
        block_header_ = (uint64_t)((rkey_table_e*)block_rkey_ + block_num_);
        public_info_ = (PublicInfo*)((uint64_t*)block_header_ + block_num_);           
        heap_start_ = m_one_side_info_.heap_start_;
    }
    uint64_t block_size_;
    uint64_t block_num_;
    uint64_t region_size_;
    uint64_t region_num_;
    uint64_t section_size_;
    uint64_t section_num_;

    // info before heap segment
    uint64_t section_header_;
    uint64_t region_header_;
    uint64_t block_rkey_;
    uint64_t heap_start_;
    uint64_t block_header_;
    PublicInfo* public_info_;
};

class ComputingNode {
public:
    typedef struct {
        uint64_t addr;
        uint32_t rkey;
    } rdma_mem_t;

    ComputingNode(bool heap_enabled, bool cache_enabled, bool one_side_enabled, uint16_t node_id): 
        heap_enabled_(heap_enabled), cpu_cache_enabled_(cache_enabled), one_side_enabled_(one_side_enabled), node_id_(node_id){
        if(cpu_cache_enabled_)  assert(heap_enabled_);
        ring_cache = new ring_buffer_atomic<mr_rdma_addr>(ring_buffer_size, ring_cache_content, mr_rdma_addr(-1, -1, -1), &reader, &writer);
        ring_cache->clear();
    }

    bool start(std::string* addr, std::string* port, uint32_t node_num) ;

    void stop() ;

    bool alive() ;

    void run() {};

    ~ComputingNode() { 
        cpu_cache_->free_cache();
        destory(); 
    }

    void woker(int proc);
    void listenser();
    void cache_filler() ;
    void print_cpu_cache() ;
    void recycler() ;

    inline void show_ring_length() {
        printf("ring length:%u\n", ring_cache->get_length());
        return ;
    }

    inline uint64_t get_region_block_addr(uint32_t region_index, uint32_t block_offset, uint32_t node) {
        return node_info_[node].heap_start_ + region_index * node_info_[node].region_size_ 
            + block_offset * node_info_[node].block_size_;
    }
    bool new_cache_section(alloc_advise advise, uint32_t node);
    bool new_backup_section(uint32_t node);
    bool new_cache_region();
    bool new_backup_region();
    bool fill_cache_block();

    bool fetch_mem_block_nocached(mr_rdma_addr &remote_addr, uint32_t node);
    bool fetch_mem_block(mr_rdma_addr &remote_addr);
    bool free_mem_block(mr_rdma_addr remote_addr);
    bool free_mem_block_slow(mr_rdma_addr remote_addr);
    bool free_mem_batch(uint32_t region_offset, uint32_t free_map, uint32_t node);

    // << RPC block fetch >>
    bool fetch_mem_block_remote(mr_rdma_addr &remote_addr, uint32_t node);

    // << local heap/cache fetch >>
    void fetch_cache(uint8_t nproc, mr_rdma_addr &remote_addr);

    // UNUSED
    bool mr_bind_remote(uint64_t size, mr_rdma_addr remote_addr, uint32_t &newkey);

    ConnectionManager* get_conn(uint32_t node){return m_rdma_conn_[node];};

    void set_global_rkey(uint32_t rkey, uint32_t node) {
        while(node > global_rkey_.size()){
            global_rkey_.push_back(0);
        }
        if(node == global_rkey_.size())
            global_rkey_.push_back(rkey);
        else
            global_rkey_[node] = rkey;
    }

    uint32_t get_global_rkey(uint32_t node) {
        if(node >= global_rkey_.size()){
            return 0;
        }
        return global_rkey_[node];
    }

private:
    void destory(){};

    uint16_t node_id_;

    FreeBlockManager *free_queue_manager;
    uint8_t running;
    bool use_global_rkey_;
    uint32_t node_num_;
    std::vector<uint32_t> global_rkey_;
    
    pthread_t woker_thread_[nprocs];

    std::vector<node_info> node_info_;
    uint64_t fill_counter = 0;

    // TODO: important node selection policy
    uint32_t current_node_ = 0;
    uint32_t backup_node_ = 0;
    section_e current_section_;
    uint32_t current_section_index_;
    // a pointer to current region(only one)
    region_with_rkey* current_region_;
    // different nodes' region cache, not only exclusive region, but only used when exclusive
    std::vector<region_with_rkey*> exclusive_region_;
    // free regions, a set
    std::set<region_with_rkey*> free_region_;
    section_e backup_section_;
    uint32_t backup_section_index_;
    region_e backup_region_;
    uint32_t backup_region_index_;
    int backup_counter = 0;
    int backup_cycle = -1;
    
    // << reserved block cache>>
    ring_buffer_atomic<mr_rdma_addr>* ring_cache;
    mr_rdma_addr ring_cache_content[ring_buffer_size];
    // rdma_mem_t ring_cache[ring_buffer_size];
    std::atomic<uint32_t> reader, writer;
    float cache_watermark_low;
    float cache_watermark_high;
    int cache_upper_bound;
    int cpu_cache_watermark[nprocs];

    // << function enabled >>
    bool heap_enabled_;
    bool cpu_cache_enabled_;
    bool one_side_enabled_;

    // << cpu cache >>
    cpu_cache* cpu_cache_;
    std::atomic<uint8_t> heap_worker_id_;
    uint8_t heap_worker_num_;

    block_header_e* header_list;
    uint32_t* rkey_list;
    uint64_t last_alloc_;
    uint64_t hint_ = 0;
    std::vector<ConnectionManager*> m_rdma_conn_;
    std::vector<std::string> ips;
    std::vector<std::string> ports;
    std::vector<rdma_mem_t> m_used_mem_; /* the used mem */
    std::mutex m_mutex_;                 /* used for concurrent mem allocation */
    
    uint64_t time_stamp_;
    struct bitmap_record{
        uint64_t offset;
        uint64_t bin_size;
    };
    std::map<mr_rdma_addr, bitmap_record> offset_record_;

};

struct worker_param{
    ComputingNode* heap;
    int id;
};

}
