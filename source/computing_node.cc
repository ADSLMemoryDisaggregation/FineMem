
#include <bits/stdint-uintn.h>
#include <pthread.h>
#include <sys/types.h>
#include "cpu_cache.h"
#include "free_block_manager.h"
#include "computing_node.h"
#include "rdma_conn.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

namespace mralloc {

void* run_woker_thread(void* arg){
    worker_param* param = (worker_param*)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(param->id, &cpuset);
    pthread_t this_tid = pthread_self();
    uint64_t ret = pthread_setaffinity_np(this_tid, sizeof(cpuset), &cpuset);
    ret = pthread_getaffinity_np(this_tid, sizeof(cpuset), &cpuset);
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i ++) {
        if (CPU_ISSET(i, &cpuset)) {
            printf("filler running on core: %d\n" , i);
        }
    }
    param->heap->woker(param->id);
    return NULL;
}

        /*  allocation class: 
                0 --> 1 --> 4KB <====== using layer 2
                1 --> 2 --> 8KB
                2 --> 4 --> 16KB
                3 --> 8 --> 32KB
                4 --> 16 --> 64KB
                5 --> 32 --> 128KB <====== using layer 1
                6 --> 64 --> 256KB
                7 --> 128 --> 512KB
                8 --> 256 --> 1MB
                9 --> 512 --> 2MB <====== using array
            */
    

void ComputingNode::woker(int proc) {
    section_e section_[32];
    uint32_t section_index_[32];
    uint32_t node_ = current_node_;
    std::vector<ConnectionManager*> m_rdma_conn;
    for(int i = 0; i < node_num_; i++){
        m_rdma_conn.push_back(new ConnectionManager());
        if (m_rdma_conn[i] == nullptr) return;
        if (m_rdma_conn[i]->init(ips[i], ports[i], 1, 1, node_id_)) return;
        m_rdma_conn[i]->find_section(section_[i], section_index_[i], 0, mralloc::alloc_light);
        sleep(1);
    }
    while(1){
        sem_wait(cpu_cache_->doorbell[proc]);
        switch(cpu_cache_->buffer_[proc].opcode_){
            case LegoOpcode::LegoAlloc: {
                uint64_t bin_size = *(uint64_t*)cpu_cache_->buffer_[proc].buffer_;
                bool slow_path = false;
                mr_rdma_addr remote_addr;
                remote_addr.node = node_%node_num_;
                remote_addr.size = bin_size;
                int retry_time = m_rdma_conn[node_%node_num_]->full_alloc(section_[node_%node_num_], section_index_[node_%node_num_], remote_addr.size, remote_addr.addr, remote_addr.rkey);
                node_++; 
                node_ = node_%node_num_;
                ((mr_rdma_addr*)cpu_cache_->buffer_[proc].buffer_)->addr = remote_addr.addr;
                ((mr_rdma_addr*)cpu_cache_->buffer_[proc].buffer_)->rkey = remote_addr.rkey;
                ((mr_rdma_addr*)cpu_cache_->buffer_[proc].buffer_)->node = remote_addr.node;
                ((mr_rdma_addr*)cpu_cache_->buffer_[proc].buffer_)->size = remote_addr.size;
                cpu_cache_->buffer_[proc].opcode_ = LegoOpcode::LegoIdle;
                break;
            }
            
            case LegoOpcode::LegoFree: {
                mr_rdma_addr remote_addr = *(mr_rdma_addr*)cpu_cache_->buffer_[proc].buffer_;
                int result = m_rdma_conn[remote_addr.node]->full_free(remote_addr.addr, remote_addr.size);
                cpu_cache_->buffer_[proc].opcode_ = LegoOpcode::LegoIdle;
                break;
            }

            case LegoOpcode::LegoReg: {
                uint16_t process_id = *(uint16_t*)cpu_cache_->buffer_[proc].buffer_;
                for(int i = 0; i < node_num_; i++){
                    uint64_t alive_count;
                    m_rdma_conn[i]->remote_read(&alive_count, sizeof(uint64_t), (uint64_t)node_info_[i].public_info_ + sizeof(uint64_t)*process_id, global_rkey_[i]);
                    while(m_rdma_conn[i]->remote_CAS(alive_count+1, &alive_count, (uint64_t)node_info_[i].public_info_ + sizeof(uint64_t)*process_id, global_rkey_[i]));
                }
                cpu_cache_->buffer_[proc].opcode_ = LegoOpcode::LegoIdle;
                break;
            }

            case LegoOpcode::LegoDereg: {
                uint16_t process_id = *(uint16_t*)cpu_cache_->buffer_[proc].buffer_;
                for(int i = 0; i < node_num_; i++){
                    uint64_t alive_count;
                    m_rdma_conn[i]->remote_read(&alive_count, sizeof(uint64_t), (uint64_t)node_info_[i].public_info_ + sizeof(uint64_t)*process_id, global_rkey_[i]);
                    while(m_rdma_conn[i]->remote_CAS(alive_count-1, &alive_count, (uint64_t)node_info_[i].public_info_ + sizeof(uint64_t)*process_id, global_rkey_[i]));
                }
                cpu_cache_->buffer_[proc].opcode_ = LegoOpcode::LegoIdle;
                break;
            }

            case LegoOpcode::LegoRemoteFree: {

                mr_rdma_addr free_addr;
                block_e owner_block_header;
                uint64_t block_index = (free_addr.addr-node_info_[free_addr.node].heap_start_)/node_info_[free_addr.node].block_size_;
                
                // read block's owner id 
                m_rdma_conn[free_addr.node]->remote_read((void*)&owner_block_header, sizeof(block_e), 
                    node_info_[free_addr.node].block_header_ + sizeof(uint64_t)*block_index, global_rkey_[free_addr.node]);
                if(owner_block_header.client_id_ == 0){
                    printf("free an invaliadate block!\n");
                }
                uint16_t owner_id = owner_block_header.client_id_ - 1;
                uint16_t node_id;

                // read owner's node id
                m_rdma_conn[free_addr.node]->remote_read((void*)&node_id, sizeof(uint16_t), 
                    (uint64_t)node_info_[free_addr.node].public_info_ + sizeof(uint64_t) * 1024 + sizeof(uint16_t) * owner_id, 
                    global_rkey_[free_addr.node]);
                MsgBuffer msg_buffer_;
                
                // read current msg buffer
                m_rdma_conn[free_addr.node]->remote_read((void*)&msg_buffer_, sizeof(MsgBuffer), 
                    (uint64_t)node_info_[free_addr.node].public_info_ + 1024*sizeof(uint64_t) + 1024*sizeof(uint16_t) + node_id*sizeof(MsgBuffer), 
                    global_rkey_[free_addr.node]);
                
                // send msg to node's heap
                for(int i = 0; i < 8; i++){
                    if(msg_buffer_.msg_type[i]==MRType::MR_IDLE){

                        // set opcode
                        while(m_rdma_conn[free_addr.node]->remote_CAS(MRType::MR_FREE, (uint64_t*)&msg_buffer_.msg_type[i], 
                            (uint64_t)node_info_[free_addr.node].public_info_ + 1024*sizeof(uint64_t) + 1024*sizeof(uint16_t) + node_id*sizeof(MsgBuffer) + sizeof(MRType)*i,
                            global_rkey_[free_addr.node]));
                        
                        // set memory
                        m_rdma_conn[free_addr.node]->remote_write((void*)&free_addr, sizeof(mr_rdma_addr),
                            (uint64_t)msg_buffer_.buffer, 
                            global_rkey_[free_addr.node]);
                        
                        break;
                    }
                }                
                cpu_cache_->buffer_[proc].opcode_ = LegoOpcode::LegoIdle;
                break;
            }
            case LegoOpcode::LegoTransfer: {
                //TODO: Write a full segment with pages and bitmaps
                break;
            }
            default :{
                printf("Wrong opcode\n");
                break;
            }
        }
        sem_post(cpu_cache_->retbell[proc]);
    }
    return;
}

/**
  * @description: start local memory service
  * @param {string} addr   the address string of RemoteHeap to connect
  * @param {string} port   the port of RemoteHeap to connect
  * @return {bool} true for success
  */
bool ComputingNode::start(std::string* addr, std::string* port, uint32_t node_num){
    use_global_rkey_ = true;
    node_num_ = node_num;
    for(int i = 0; i < node_num; i++){
        m_rdma_conn_.push_back(new ConnectionManager());
        ips.push_back(std::string(addr[i])); ports.push_back(std::string(port[i]));
        if (m_rdma_conn_[i] == nullptr) return -1;
        if (m_rdma_conn_[i]->init(addr[i], port[i], 1, 1, node_id_)) return false;
        sleep(1);
        set_global_rkey(m_rdma_conn_[i]->get_global_rkey(), i);
    }
    // init free queue manager, using REMOTE_BLOCKSIZE as init size
    running = 1;
    if(heap_enabled_ && one_side_enabled_) {
        bool ret;
        for(int i = 0; i < node_num; i++){
            one_side_info m_one_side_info_ = m_rdma_conn_[i]->get_one_side_info();
            node_info new_info(m_one_side_info_);
            node_info_.push_back(new_info);
            exclusive_region_.push_back((region_with_rkey*)malloc(sizeof(region_with_rkey) * new_info.region_num_));
        }
        hint_ = rand()%node_info_[0].block_num_;
    } 
    if(cpu_cache_enabled_) {
        mr_rdma_addr remote_addr;
        cpu_cache_ = new cpu_cache(node_info_[0].block_size_);
        pthread_t running_thread;
        pthread_t listening_thread;
        worker_param new_param[nprocs];
        for(int i = 0; i < nprocs; i++) {
            new_param[i].heap = this; new_param[i].id = i;
            pthread_create(&woker_thread_[i], NULL, run_woker_thread, &new_param[i]);
            // sleep(1);
            usleep(100000);
        }
        //pthread_create(&listening_thread, NULL, run_rpc_thread, this);
    }
    return true;
}

bool ComputingNode::new_cache_section(alloc_advise advise, uint32_t node){
    if(!m_rdma_conn_[node]->find_section(current_section_, current_section_index_, 0, advise) ) {
        printf("cannot find avaliable section\n");
        return false;
    }
    current_node_ = node;
    return true;
}

bool ComputingNode::new_backup_section(uint32_t node){
    if(!m_rdma_conn_[node]->find_section(backup_section_, backup_section_index_, 0, alloc_light) ) {
        printf("cannot find avaliable backup section\n");
        return false;
    }
    backup_node_ = node;
    return true;
}

bool ComputingNode::new_cache_region() {
    // exclusive, and fetch rkey must
    region_e new_region; uint32_t new_region_index;
    while(!m_rdma_conn_[current_node_]->fetch_region(current_section_, current_section_index_, 0, false, new_region, new_region_index, 0) ) {
        if(!new_cache_section(alloc_empty, current_node_)){
            current_node_ = (current_node_+1) % node_num_;
            printf("[cache single region] try to scan next node %d\n", current_node_);
        }
    }
    current_region_ = &exclusive_region_[current_node_][new_region_index];
    current_region_->region = new_region;
    current_region_->index = new_region_index;
    m_rdma_conn_[current_node_]->fetch_exclusive_region_rkey(current_region_->index, current_region_->rkey);
    return true;
}

bool ComputingNode::new_backup_region() {
        // exclusive, and fetch rkey must
    while(!m_rdma_conn_[backup_node_]->fetch_region(backup_section_, backup_section_index_, 0, true, backup_region_, backup_region_index_, 0) ) {
        if(!new_backup_section(backup_node_)){
            backup_node_ = (backup_node_+1) % node_num_;
            printf("[cache backup region] try to scan next node %d\n", backup_node_);
            // return false;
        }
    }
    backup_counter += 1;
    exclusive_region_[backup_node_][backup_region_index_].region = backup_region_;
    exclusive_region_[backup_node_][backup_region_index_].index = backup_region_index_;
    return true;
}

int block_counter = 0;

bool ComputingNode::fill_cache_block(){
    return false;
}

bool ComputingNode::fetch_mem_block(mr_rdma_addr &remote_addr){
    return false;
}

bool ComputingNode::free_mem_batch(uint32_t region_offset, uint32_t free_map, uint32_t node){
    return false;
}

bool ComputingNode::free_mem_block_slow(mr_rdma_addr remote_addr) {
    return false;
}

bool ComputingNode::free_mem_block(mr_rdma_addr remote_addr){
    return false;
}

// This is a DEBUG interface for a direct block fetch using LegoHeader
bool ComputingNode::fetch_mem_block_nocached(mr_rdma_addr &remote_addr, uint32_t node){
    while(!m_rdma_conn_[node]->fetch_region_block(current_section_ ,current_region_->region, remote_addr.addr, remote_addr.rkey, false, current_region_->index, 0)) {
        new_cache_region();
    }
    remote_addr.node = node;
    if(use_global_rkey_) remote_addr.rkey = get_global_rkey(node);
    return true;
}

/**
  * @description: stop local memory service
  * @return {void}
  */
void ComputingNode::stop(){
    if(cpu_cache_enabled_) 
        cpu_cache_->free_cache();
    running = 0;
  };

/**
  * @description: get memory alive state
  * @return {bool}  true for alive
  */
bool ComputingNode::alive() { return true; }

// API for test
bool ComputingNode::fetch_mem_block_remote(mr_rdma_addr &remote_addr, uint32_t node) {
    if (m_rdma_conn_[node]->remote_fetch_block(remote_addr.addr, remote_addr.rkey, 0)) return false;
    remote_addr.node = node;
    return true;
}
 
// API for test
bool ComputingNode::mr_bind_remote(uint64_t size, mr_rdma_addr remote_addr, uint32_t &newkey) {
  if (m_rdma_conn_[remote_addr.node]->remote_mw(remote_addr.addr, remote_addr.rkey, size, newkey)) return false;
  return true;
}

}
