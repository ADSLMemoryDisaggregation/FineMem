
#pragma once

#include <bits/stdint-uintn.h>
#include <mutex>
#include <queue>
#include <thread>
#include "rdma_conn.h"

namespace mralloc {

/* The RDMA connection queue */
class ConnQue {
 public:
  ConnQue() {}

  void enqueue(RDMAConnection *conn) {
    std::unique_lock<std::mutex> lock(m_mutex_);
    m_queue_.push(conn);
  };

  RDMAConnection *dequeue() {
  retry:
    std::unique_lock<std::mutex> lock(m_mutex_);
    while (m_queue_.empty()) {
      lock.unlock();
      std::this_thread::yield();
      goto retry;
    }
    RDMAConnection *conn = m_queue_.front();
    m_queue_.pop();
    return conn;
  }

 private:
  std::queue<RDMAConnection *> m_queue_;
  std::mutex m_mutex_;
};

/* The RDMA connection manager */
class ConnectionManager {
 public:
    ConnectionManager() {}

    ~ConnectionManager() {
      
    }

    int init(const std::string ip, const std::string port, uint32_t rpc_conn_num,
            uint32_t one_sided_conn_num, uint16_t node_id);
    one_side_info get_one_side_info() {return m_one_side_info_;};
    int register_remote_memory(uint64_t &addr, uint32_t &rkey, uint64_t size);
    int unregister_remote_memory(uint64_t addr);
    int remote_read(void *ptr, uint64_t size, uint64_t remote_addr,
                    uint32_t rkey);
    int remote_write(void *ptr, uint64_t size, uint64_t remote_addr,
                    uint32_t rkey);
    bool remote_CAS(uint64_t swap, uint64_t* compare, uint64_t remote_addr, 
                        uint32_t rkey);
    int remote_mw(uint64_t addr, uint32_t rkey, uint64_t size, uint32_t &newkey);
    int remote_rebind(uint64_t addr, uint32_t &newkey);
    int remote_rebind_batch(uint64_t *addr, uint32_t *newkey);
    int remote_memzero(uint64_t addr, uint64_t size);
    int remote_fetch_block(uint64_t &addr, uint32_t &rkey, uint16_t size_class);
    int remote_free_block(uint64_t addr);
    int remote_print_alloc_info(uint64_t &mem_usage);
    uint32_t get_global_rkey() {return global_rkey_;};
    RDMAConnection* fetch_connector() {
        return m_rpc_conn_queue_->dequeue();
    };

    void return_connector(RDMAConnection* connector) {
        m_rpc_conn_queue_->enqueue(connector);
    };

    int full_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey);
    int full_free(uint64_t addr, uint16_t block_class);

    bool force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise);
    int find_section(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, alloc_advise advise) ;
    int section_alloc(uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey);

    int fetch_region(section_e &alloc_section, uint32_t section_offset, uint16_t size_class, bool use_chance, region_e &alloc_region, uint32_t &region_index, uint32_t skip_mask) ;
    int region_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey);
    int chunk_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, bool use_chance, uint64_t &addr, uint32_t &rkey);

    int fetch_region_block(section_e &alloc_section, region_e &alloc_region, uint64_t &addr, uint32_t &rkey, bool is_exclusive, uint32_t region_index, uint16_t block_class) ;
    int fetch_region_batch(section_e &alloc_section, region_e &alloc_region, mr_rdma_addr* addr, uint64_t num, bool is_exclusive, uint32_t region_index);
    
    int fetch_block(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey, uint16_t size_class) ;
    int free_block(uint64_t addr, uint16_t size_class) ;
    int fetch_block_bitmap(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey) ;
    int free_block_bitmap(uint64_t addr) ;
    int free_region_batch(uint32_t region_offset, uint32_t free_bitmap, bool is_exclusive);

    bool fetch_exclusive_region_rkey(uint32_t region_index, rkey_table_e* rkey_list);
    int free_region_block(uint64_t addr, bool is_exclusive, uint16_t block_class) ;

    inline uint32_t get_addr_region_index(uint64_t addr) {return (addr-heap_start_) / region_size_;};
    inline uint32_t get_addr_region_offset(uint64_t addr) {return (addr-heap_start_) % region_size_ / block_size_;};
    inline uint64_t get_section_region_addr(uint32_t section_offset, uint32_t region_offset) {return heap_start_ + section_offset*section_size_ + region_offset * region_size_ ;};
    inline uint64_t get_region_addr(uint32_t region_index) {return heap_start_ + region_index * region_size_;};
    inline uint64_t get_region_block_addr(uint32_t region_index, uint32_t block_offset) {return heap_start_ + region_index * region_size_ + block_offset * block_size_;} ;
    inline uint32_t get_region_block_rkey(uint32_t region_index, uint32_t block_offset) {
        rkey_table_e rkey;
        remote_read(&rkey, sizeof(rkey), block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey), global_rkey_);
        return rkey.main_rkey_;
    };

    inline uint32_t rebind_region_block_rkey(uint32_t region_index, uint32_t block_offset) {
        rkey_table_e rkey;
        remote_read(&rkey, sizeof(rkey), block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey), global_rkey_);
        rkey_table_e new_rkey;
        do{
            if(rkey.backup_rkey_ == (uint32_t)-1 || rkey.backup_rkey_ == 0){
                return 0;
            }
            new_rkey.main_rkey_ = rkey.backup_rkey_;
            new_rkey.backup_rkey_ = (uint32_t)-1;
        }while(!remote_CAS(*(uint64_t*)&new_rkey, (uint64_t*)&rkey, block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey), global_rkey_) );
        // remote_write(&rkey_new, sizeof(uint32_t), backup_rkey_ + (region_index*block_per_region + block_offset)*sizeof(uint32_t), global_rkey_);
        return new_rkey.main_rkey_;
    };
    
    inline uint32_t get_block_num() {return block_num_;};
    inline uint64_t get_block_size() {return block_size_;};

 private:
    
    ConnQue *m_rpc_conn_queue_;
    ConnQue *m_one_sided_conn_queue_;

    one_side_info m_one_side_info_;
    uint32_t global_rkey_;

      // basic info
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

};  // namespace kv
