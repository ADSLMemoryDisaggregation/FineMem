
#pragma once
#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <sched.h>
#include <atomic>
#include <queue>
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
#include <memkind.h>
#include <thread>
#include <sys/time.h>

namespace mralloc {

const int accelerate_thread = 16;

const int cache_length = 72*1024;

static void * run_mw_release(ibv_mw** block_mw, ibv_mw** backup_mw, int start, int end, ibv_pd* pd) {
    for(int i = start; i < end; i++){
        // uint64_t block_addr_ = server_block_manager_->get_block_addr(i);
        ibv_dealloc_mw(block_mw[i]);
        ibv_dealloc_mw(backup_mw[i]);
    }
};


class MWPool {
 public:
  MWPool(ibv_pd *pd) :pd_(pd) {
    for(int i=0; i<10; i++){
        ibv_mw* new_mw_ = ibv_alloc_mw(pd_, IBV_MW_TYPE_1);
        // printf("generate rkey:%x\n", new_mw_->rkey);
        mw_queue_.push(new_mw_);
    }
  }

  void enqueue(ibv_mw *conn) {
    std::unique_lock<std::mutex> lock(mw_mutex_);
    mw_queue_.push(conn);
  };

  ibv_mw *dequeue() {
    std::unique_lock<std::mutex> lock(mw_mutex_);
    if(mw_queue_.empty()) {
      for(int i=0; i<10; i++){
        ibv_mw* new_mw_ = ibv_alloc_mw(pd_, IBV_MW_TYPE_1);
        mw_queue_.push(new_mw_);
      }
    }
    ibv_mw *mw = mw_queue_.front();
    mw_queue_.pop();
    return mw;
  }

 private:
  std::queue<ibv_mw*> mw_queue_;
  std::mutex mw_mutex_;
  ibv_pd *pd_;
};

class MemoryNode {
public:
    struct WorkerInfo {
        CmdMsgBlock *cmd_msg;
        CmdMsgRespBlock *cmd_resp_msg;
        struct ibv_mr *msg_mr;
        struct ibv_mr *resp_mr;
        rdma_cm_id *cm_id;
        struct ibv_cq *cq;
    };    
    MemoryNode(bool one_sided_enabled): one_sided_enabled_(one_sided_enabled) {
        ring_cache = new ring_buffer_atomic<mr_rdma_addr>(cache_length, ring_cache_content, mr_rdma_addr(-1, -1, -1), &reader, &writer);
        ring_cache -> clear();
    };
    ~MemoryNode(){
        uint64_t block_num_ = server_block_manager_->get_block_num() ;
        std::thread* mw_thread[accelerate_thread];
        struct timeval start, end;
        gettimeofday(&start, NULL);
        uint64_t interval = block_num_ / accelerate_thread;
        for(int i = 0; i < accelerate_thread; i++){
            mw_thread[i] = new std::thread(&run_mw_release, block_mw, backup_mw, i*interval, (i+1)*interval, m_pd_);
        }
        for(int i = 0; i < accelerate_thread; i++){
            mw_thread[i]->join();
        }
    };

    bool start(const std::string addr, const std::string port, const std::string device);
    void stop();
    bool alive();
    void rebinder();
    void recovery(int id);

    bool new_cache_section();
    bool new_cache_region();
    bool fill_cache_block();

    bool fetch_mem_block(uint64_t &addr, uint32_t &rkey);
    bool free_mem_block(uint64_t addr);


    uint64_t print_alloc_info();

    bool init_mw(ibv_qp* qp, ibv_cq *cq);
    bool bind_mw(ibv_mw* mw, uint64_t addr, uint64_t size, ibv_qp* qp, ibv_cq *cq);
    bool bind_mw_async(ibv_mw* mw, uint64_t addr,  uint64_t id, uint64_t size, ibv_qp* qp, ibv_cq* cq);
    bool bind_mw_async_poll(ibv_cq* cq);
    bool bind_mw_type2(ibv_mw* mw, uint64_t addr, uint64_t size, ibv_qp* qp, ibv_cq *cq);
    bool unbind_mw_type2(ibv_mw* mw, uint64_t addr, uint64_t size, ibv_qp* qp, ibv_cq *cq);

    void set_global_rkey(uint32_t rkey) {
      global_rkey_ = rkey;
    }

    uint32_t get_global_rkey() {
      return global_rkey_;
    }
    
private:

    bool init_memory_heap(uint64_t size);

    void handle_connection();

    int create_connection(struct rdma_cm_id *cm_id, uint8_t connect_type, uint16_t node_id);

    struct ibv_mr *rdma_register_memory(void *ptr, uint64_t size);

    int remote_write(volatile WorkerInfo *work_info, uint64_t local_addr, uint32_t lkey,
                    uint32_t length, uint64_t remote_addr, uint32_t rkey);

    int allocate_and_register_memory(uint64_t &addr, uint32_t &rkey,
                                    uint64_t size);

    int deallocate_and_unregister_memory(uint64_t addr);

    void worker(volatile WorkerInfo *work_info, uint32_t num);

    memkind_t memkind_;

    // << allocation metadata >>
    section_e current_section_;
    uint32_t current_section_index_;
    region_e current_region_;
    uint32_t current_region_index_;

    // << reserved block cache>>
    ring_buffer_atomic<mr_rdma_addr>* ring_cache;
    mr_rdma_addr ring_cache_content[cache_length];
    std::atomic<uint32_t> reader, writer;
    uint64_t simple_cache_addr[32];
    uint32_t simple_cache_rkey[32];
    uint64_t simple_cache_watermark;

    bool use_global_rkey = true;
    // << function enabled >>
    bool one_sided_enabled_;
    ibv_qp* rebinder_qp; ibv_cq* rebinder_cq;
    ibv_qp* one_side_qp_[MAX_SERVER_WORKER*MAX_SERVER_CLIENT];
    ibv_cq* one_side_cq_[MAX_SERVER_WORKER*MAX_SERVER_CLIENT];

    struct ibv_mr *global_mr_;
    struct rdma_event_channel *m_cm_channel_;
    struct rdma_cm_id *m_listen_id_;
    struct ibv_pd *m_pd_;
    struct ibv_context *m_context_;
    bool m_stop_;
    std::thread *m_conn_handler_;
    volatile WorkerInfo *volatile* volatile m_worker_info_;
    volatile uint32_t m_worker_num_;
    std::thread **m_worker_threads_;
    MWPool* mw_queue_;
    std::unordered_map<uint64_t, ibv_mr*> mr_recorder;
    ibv_mw **m_mw_handler;
    RPC_Fusee* rpc_fusee_;
    uint64_t heap_total_size_;
    uint64_t heap_start_addr_;
    bool mw_binded;
    ibv_cq* mw_cq;
    ibv_qp* mw_qp;

    // << memory window? >>
    ibv_mw** block_mw;
    ibv_mw** backup_mw;
    ServerBlockManager *server_block_manager_;
    FreeQueueManager *free_queue_manager_;
    uint8_t running;
    uint32_t global_rkey_;
    std::atomic<uint64_t> reg_size_;
    std::atomic<uint64_t> heap_pointer_;

    std::mutex m_mutex_;
    std::mutex m_mutex_2;

    std::queue<uint64_t> free_addr_;

};
}