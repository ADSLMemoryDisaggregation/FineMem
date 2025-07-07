
#pragma once

#include <arpa/inet.h>
#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <map>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include "msg.h"
#include "free_block_manager.h"
#include "cpu_cache.h"
#include <random>

namespace mralloc {

#define RESOLVE_TIMEOUT_MS 5000

// const int retry_threshold = 3;
const int retry_threshold = 1000000;
const int low_threshold = 100000;
// const int low_threshold = 2;

struct one_side_info {
    uint64_t block_size_;
    uint64_t block_num_;
    uint32_t global_rkey_;
    uint64_t section_header_;
    uint64_t heap_start_;
};


/* RDMA connection */
class RDMAConnection {
public:
    int init(const std::string ip, const std::string port, uint8_t access_type, uint16_t pid);
    one_side_info get_one_side_info() {return m_one_side_info_;};
    int register_remote_memory(uint64_t &addr, uint32_t &rkey, uint64_t size);
    int unregister_remote_memory(uint64_t addr);
    int remote_read(void *ptr, uint64_t size, uint64_t remote_addr,
                    uint32_t rkey);
    int remote_write(void *ptr, uint64_t size, uint64_t remote_addr,
                    uint32_t rkey);
    bool remote_CAS(uint64_t swap, uint64_t* compare, uint64_t remote_addr, 
                        uint32_t rkey);
    int remote_fetch_block(uint64_t &addr, uint32_t &rkey, uint16_t size_class);
    int remote_free_block(uint64_t addr);
    int remote_mw(uint64_t addr, uint32_t rkey, uint64_t size, uint32_t &newkey);
    int remote_rebind(uint64_t addr, uint32_t &newkey);
    int remote_rebind_batch(uint64_t *addr, uint32_t *newkey);
    int remote_memzero(uint64_t addr, uint64_t size);
    int remote_fusee_alloc(uint64_t &addr, uint32_t &rkey);
    int remote_print_alloc_info(uint64_t &mem_usage);
    uint32_t get_rkey() {return m_fusee_rkey;};
    uint32_t get_global_rkey() {return global_rkey_;};
    ibv_qp* get_qp() {return m_cm_id_->qp;};
    ibv_cq* get_cq() {return m_cq_;};
    ibv_pd* get_pd() {return m_pd_;};
    ibv_context* get_ctx() {return m_cm_id_->verbs;};

    // << one-sided fetch API >>

    inline uint64_t section_metadata_addr(uint64_t section_offset) {return (uint64_t)((section_e*)section_header_ + section_offset);};
    inline uint64_t region_metadata_addr(uint64_t region_offset) {return (uint64_t)((region_e*)region_header_ + region_offset);};

    uint64_t get_heap_start() {return heap_start_;};
    inline bool check_section(section_e alloc_section, alloc_advise advise, uint32_t offset);
    bool force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise);
    bool force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise, alloc_advise compare);
    int find_section(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, alloc_advise advise) ;

    int fetch_region(section_e &alloc_section, uint32_t section_offset, uint16_t size_class, bool use_chance, region_e &alloc_region, uint32_t &region_index, uint32_t skip_mask) ;
    bool fetch_exclusive_region_rkey(uint32_t region_index, rkey_table_e* rkey_list) {
        // uint32_t new_rkey[block_per_region];
        // memset(new_rkey, (uint32_t)-1, sizeof(uint32_t)*block_per_region);
        remote_read(rkey_list, sizeof(rkey_table_e)*block_per_region, block_rkey_ + region_index*block_per_region*sizeof(rkey_table_e), global_rkey_);
        // for(int i = 0; i < block_per_region; i++) {
        //     while(rkey_list[i] == (uint32_t)-1){
        //         remote_read(&rkey_list[i], sizeof(uint32_t), block_rkey_ + (region_index*block_per_region+i)*sizeof(uint32_t), global_rkey_);
        //     }
        // }
        // remote_write(new_rkey, sizeof(uint32_t)*block_per_region, block_rkey_ + region_index*block_per_region*sizeof(uint32_t), global_rkey_);
        return true;
    }

    inline uint64_t get_section_region_addr(uint32_t section_offset, uint32_t region_offset) {return heap_start_ + section_offset*section_size_ + region_offset * region_size_ ;};
    inline uint64_t get_region_addr(uint32_t region_index) {return heap_start_ + region_index * region_size_;};
    inline uint64_t get_region_block_addr(uint32_t region_index, uint32_t block_offset) {return heap_start_ + region_index * region_size_ + block_offset * block_size_;} ;
    inline uint64_t get_block_addr(uint32_t block_offset) {return heap_start_ + block_offset * block_size_;} ;
    inline uint32_t get_region_block_rkey(uint32_t region_index, uint32_t block_offset) {
        rkey_table_e rkey;
        remote_read(&rkey, sizeof(rkey), block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey_table_e), global_rkey_);
        return rkey.main_rkey_;
    };
    inline uint32_t get_block_rkey(uint32_t block_offset) {
        rkey_table_e rkey;
        remote_read(&rkey, sizeof(rkey), block_rkey_ + (block_offset)*sizeof(rkey_table_e), global_rkey_);
        return rkey.main_rkey_;
    };
    
    inline uint32_t rebind_region_block_rkey(uint32_t region_index, uint32_t block_offset) {
        rkey_table_e rkey;
        remote_read(&rkey, sizeof(rkey), block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey), global_rkey_);
        rkey_table_e new_rkey;
        do{
            // while(rkey.backup_rkey_ == (uint32_t)-1 || rkey.backup_rkey_ == 0){
            //     remote_read(&rkey, sizeof(rkey), block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey), global_rkey_);
            // }
            if(rkey.backup_rkey_ == (uint32_t)-1 || rkey.backup_rkey_ == 0){
                return 0;    
            }
            new_rkey.main_rkey_ = rkey.backup_rkey_;
            new_rkey.backup_rkey_ = (uint32_t)-1;
        }while(!remote_CAS(*(uint64_t*)&new_rkey, (uint64_t*)&rkey, block_rkey_ + (region_index*block_per_region + block_offset)*sizeof(rkey), global_rkey_) );
        // remote_write(&rkey_new, sizeof(uint32_t), backup_rkey_ + (region_index*block_per_region + block_offset)*sizeof(uint32_t), global_rkey_);
        return new_rkey.main_rkey_;
    };
    
    int fetch_region_block(section_e &alloc_section, region_e &alloc_region, uint64_t &addr, uint32_t &rkey, bool is_exclusive, uint32_t region_index, uint16_t block_class) ;
    int fetch_region_batch(section_e &alloc_section, region_e &alloc_region, mr_rdma_addr* addr, uint64_t num, bool is_exclusive, uint32_t region_index) ;
    int free_region_block(uint64_t addr, bool is_exclusive, uint16_t block_class) ;
    int free_region_batch(uint32_t region_offset, uint32_t free_bitmap, bool is_exclusive);

    int full_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey);
    int full_free(uint64_t addr, uint16_t block_class);

    int fetch_block(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey, uint16_t size_class) ;
    int region_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey);
    int chunk_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, bool use_chance, uint64_t &addr, uint32_t &rkey);
    
    int free_block(uint64_t addr, uint16_t size_class) ;
    int section_alloc(uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey);

    int fetch_block_bitmap(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey) ;
    int free_block_bitmap(uint64_t addr) ;


private:

    struct ibv_mr *rdma_register_memory(void *ptr, uint64_t size);

    // << one-sided read/write >>
    int rdma_remote_read(uint64_t local_addr, uint32_t lkey, uint64_t length,
                        uint64_t remote_addr, uint32_t rkey);

    int rdma_remote_write(uint64_t local_addr, uint32_t lkey, uint64_t length,
                            uint64_t remote_addr, uint32_t rkey);

    struct rdma_event_channel *m_cm_channel_;
    struct ibv_pd *m_pd_;
    struct ibv_cq *m_cq_;
    struct rdma_cm_id *m_cm_id_;
    uint64_t m_server_cmd_msg_;
    uint32_t m_server_cmd_rkey_;
    uint32_t m_fusee_rkey;
    uint32_t m_remote_size_;
    struct CmdMsgBlock *m_cmd_msg_;
    struct CmdMsgRespBlock *m_cmd_resp_;
    struct ibv_mr *m_msg_mr_;
    struct ibv_mr *m_resp_mr_;
    char *m_reg_buf_;
    struct ibv_mr *m_reg_buf_mr_;
    uint16_t conn_id_;
    uint16_t node_id_;

    // << one-sided support >>
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

    uint64_t retry_counter_;
    std::mt19937 mt;

    region_e cache_region;
    region_e cache_region_array[16];
    int cache_section_index = -1;
    int cache_region_index = -1;

    section_e cache_section_array[64];
    int cache_section_array_index = -1;

    int skip_section;
    int skip_region;

};


}  // namespace kv
