
#pragma once

#include "msg.h"
#include "cpu_cache.h"
#include <bits/stdint-uintn.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <queue>
#include <mutex>
#include <fstream>
#include <random>
#include <map>

namespace mralloc {

// #define REMOTE_MEM_SIZE 134217728
// #define REMOTE_MEM_SIZE 16777216
// #define REMOTE_MEM_SIZE 67108864
// #define REMOTE_MEM_SIZE 16384
// #define REMOTE_MEM_SIZE 8192
// #define REMOTE_MEM_SIZE 32768
// #define REMOTE_MEM_SIZE 262144
// #define REMOTE_MEM_SIZE 65536
// #define REMOTE_MEM_SIZE 33554432
// #define REMOTE_MEM_SIZE 2097152
// #define REMOTE_MEM_SIZE 1048576
// #define REMOTE_MEM_SIZE 524288
// #define REMOTE_MEM_SIZE 65536
// #define REMOTE_MEM_SIZE 4096
// #define REMOTE_MEM_SIZE 131072

#define REMOTE_MEM_SIZE 4096
// #define REMOTE_MEM_SIZE 16384
// #define REMOTE_MEM_SIZE 65536
// #define REMOTE_MEM_SIZE 2097152


#define INIT_MEM_SIZE ((uint64_t)20*1024*1024*1024)

const uint64_t large_block_items = 64;

const uint64_t region_per_section = 16;
const uint64_t block_per_region = 32;
const uint64_t max_region_num = 4096;
// const uint64_t max_region_num = INIT_MEM_SIZE/REMOTE_MEM_SIZE/block_per_region;
const uint64_t page_size = (uint64_t)1024*1024*32;
// const uint64_t page_size = (uint64_t)1024*1024*1024*2;

enum alloc_advise {
    alloc_empty,
    alloc_light,
    alloc_heavy,
    alloc_full
};

struct block_header_e {
    uint8_t max_length;
    uint8_t alloc_history;
    uint16_t flag;
    uint32_t bitmap;
};

struct retry_counter {
    uint16_t retry_num[3] = {0};
    uint16_t retry_iter = 0;
};

typedef std::atomic<block_header_e> block_header;
typedef uint64_t bitmap64;
typedef uint32_t bitmap32;
typedef uint16_t bitmap16;

const bitmap16 bitmap16_filled = ~(uint16_t)0;
const bitmap32 bitmap32_filled = ~(uint32_t)0;

struct large_block {
    bitmap64 bitmap;
    block_header header[large_block_items];
    uint32_t rkey[large_block_items];
    large_block* next;
    uint64_t offset;
};

/* section the same as region
*/
struct section_e {
    bitmap16 frag_map_;
    bitmap16 alloc_map_;
    // max_length, 1~32 
    uint16_t retry_ : 2;
    // [TODO] no used?
    // uint16_t exclusive_ : 1;
    // on use to check whether it has been freed
    uint16_t on_use_ : 1;
    uint16_t last_offset_ : 5;
    uint16_t last_timestamp_ : 8;
    uint16_t num : 3;
    uint16_t last_modify_id_ : 13;
};
typedef std::atomic<section_e> section;

struct rkey_table_e {
    uint32_t main_rkey_;
    uint32_t backup_rkey_;
};
typedef std::atomic<rkey_table_e> rkey_table;

struct region_e {
    bitmap32 base_map_;
    // max_length, 1~32 
    uint16_t retry_ : 2;
    // [TODO] no used?
    uint16_t exclusive_ : 1;
    // on use to check whether it has been freed
    uint16_t on_use_ : 1;
    uint16_t last_offset_ : 5;
    uint16_t last_timestamp_ : 7;
    uint16_t num : 3;
    uint16_t last_modify_id_ : 13;
};

typedef std::atomic<region_e> region;

struct block_e {
    uint64_t client_id_ : 16;
    uint64_t timestamp_ : 16;
    uint64_t size_ : 32;

};

typedef std::atomic<block_e> block;

// typedef std::atomic<block_e> block;

struct region_with_rkey {
    region_e region;
    uint32_t index;
    rkey_table_e rkey[block_per_region];
    uint32_t node;
};

inline int free_bit_in_bitmap32(uint32_t bitmap) {
    return 32 - __builtin_popcount(bitmap);
}

inline int free_bit_in_bitmap16(uint16_t bitmap) {
    return 16 - __builtin_popcount(bitmap);
}

inline int free_bit_in_bitmap64(uint64_t bitmap) {
    return 64 - __builtin_popcountll(bitmap);
}

inline int find_free_index_from_bitmap64_tail(uint64_t bitmap) {
    if(~bitmap == 0) return -1;
    return __builtin_ctzll(~bitmap);
}

inline int find_free_index_from_bitmap32_tail(uint32_t bitmap) {
    if(~bitmap == 0) return -1;
    return __builtin_ctz(~bitmap);
}

inline int find_free_index_from_bitmap16_tail(uint16_t bitmap) {
    if(bitmap == 0xffff) return -1;
    if(bitmap == 0) return 0;
    return __builtin_ctz(~bitmap);
}

inline int find_free_index_from_bitmap64_lead(uint64_t bitmap) {
    if(~bitmap == 0) return -1;
    return 63-__builtin_clzll(~bitmap);
}

inline int find_free_index_from_bitmap32_lead(uint32_t bitmap) {
    if(~bitmap == 0) return -1;
    return 31-__builtin_clz(~bitmap);
}

inline void raise_bit(uint16_t &alloc_map, uint16_t & frag_map, uint16_t index){
    if((alloc_map >> index) % 2 == 0) {
        alloc_map |= (uint16_t)1<<index;
    } else {
        alloc_map &= ~((uint16_t)1<<index);
        frag_map |= (uint16_t)1<<index;
    }
}

inline void down_bit(uint16_t &alloc_map, uint16_t & frag_map, uint16_t index){
    if((alloc_map >> index) % 2 == 1) {
        alloc_map &= ~((uint16_t)1<<index);
    } else {
        frag_map &= ~((uint16_t)1<<index);
        alloc_map |= (uint16_t)1<<index;
    }
}

// Count the longest free length in a bitmap
// can we do fast?
inline uint8_t max_longbit(uint32_t alloc_map) {
    uint8_t max_long = 0, current_long = 0;
    for(int i = 0; i < 32; i++){
        if(alloc_map % 2 == 0) {
            current_long += 1;
        } else {
            max_long = std::max(max_long, current_long);
            current_long = 0;
        }
        alloc_map = alloc_map >> 1;
    }
    return max_long;
}

class FreeBlockManager{
public:
struct remote_addr {
    uint64_t addr;
    uint32_t rkey;
} ;
    FreeBlockManager(uint64_t block_size): block_size_(block_size) {};
    ~FreeBlockManager() {};
    virtual bool init(uint64_t addr, uint64_t size, uint32_t rkey) {return true;};
    virtual bool init(uint64_t meta_addr, uint64_t addr, uint64_t size, uint32_t rkey) {return true;};
    virtual void init_size_align(uint64_t addr, uint64_t size, uint64_t &init_addr, uint64_t &init_size) {};
    virtual bool fetch(uint64_t size, uint64_t &addr, uint32_t &rkey) {return true;};
    virtual bool fetch(uint64_t start_addr, uint64_t size, uint64_t &addr, uint32_t &rkey) {return true;};
    virtual bool fill_block(uint64_t addr, uint64_t size, uint32_t rkey) {return 0;};
    virtual bool fetch_block(uint64_t &addr, uint32_t &rkey) {return true;};
    virtual void print_state() {};
    uint64_t get_block_size() {return block_size_;};
protected:
    uint64_t block_size_;
};

class ServerBlockManager {
public:
    PublicInfo* public_info_;
    ServerBlockManager(uint64_t block_size):block_size_(block_size) {
        region_size_ = block_size_ * block_per_region;
        section_size_ = region_size_ * region_per_section;
        mem_record_.open("result.csv");

    };
    ~ServerBlockManager() {
        mem_record_.close();
    };
    void recovery(int node);
    inline uint64_t num_align_upper(uint64_t num, uint64_t align) {
        return (num + align - 1) - ((num + align - 1) % align);
    }

    void init_align_hint(uint64_t &addr, uint64_t &size, uint64_t &init_addr, uint64_t &init_size) {
        // uint64_t align = cal_header_size();
        uint64_t align = region_size_ < page_size ? page_size : region_size_;
        size = num_align_upper(size, align);
        if(cal_header_size() > page_size) {
            printf("too large memory region, out of range!\n");
        }
        init_size = size + (uint64_t)1024*1024*1024*20;
        // init_size = size + align;
        init_addr = num_align_upper(addr, align);
        addr = init_addr + (uint64_t)1024*1024*1024*20;
        // addr = init_addr + align;
        assert(init_addr % align == 0);
    };

    uint64_t cal_header_size() {
        uint64_t section_header_size = max_region_num/region_per_section * sizeof(section);
        uint64_t region_header_size = max_region_num * sizeof(region);
        uint64_t block_rkey_size = max_region_num * block_per_region * sizeof(uint32_t);
        return section_header_size + region_header_size + block_rkey_size;
    };

    bool init(uint64_t meta_addr, uint64_t addr, uint64_t size, uint32_t rkey);

    uint64_t print_section_info(int cache, uint64_t reg_size) {
        uint64_t empty=0, exclusive=0;
        uint64_t used = 0;
        double utilization = 0;
        uint64_t managed = 0;
        for(int i = 0; i< section_num_; i++) {
            uint16_t empty_map = section_header_[i].load().alloc_map_ | section_header_[i].load().frag_map_;
            uint16_t exclusive_map = ~section_header_[i].load().alloc_map_ | ~section_header_[i].load().frag_map_;
            uint32_t use_counter;
            for(int j = 0; j < region_per_section; j ++) {
                use_counter = block_per_region - free_bit_in_bitmap32(region_header_[i*region_per_section + j].load().base_map_);
                used += use_counter;
                if(use_counter > 0){
                    managed += 1;
                    // utilization += 1.0*use_counter/block_per_region;
                }
                empty_map >>= 1;
                exclusive_map >>= 1;
            }
        }
        used += exclusive * block_per_region;
        for(int i = 0; i <block_num_; i++) {
            block_e block_head = block_header_[i].load();
            if(*(uint64_t*)(&block_head) == 1) {
                used ++;
            }
        }
        // mem_record_ << 1.0*managed/(section_num_*region_per_section) << "," << utilization/managed << ", "<< (used-cache)*REMOTE_MEM_SIZE + (long long)reg_size*1024*1024 << std::endl;
        mem_record_ << (used-cache)*REMOTE_MEM_SIZE + reg_size << std::endl;
        return (used-cache)*REMOTE_MEM_SIZE + reg_size;
    }

    inline bool check_section(section_e alloc_section, alloc_advise advise, uint32_t offset);
    uint64_t get_heap_start() {return heap_start_;};
    bool force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise);
    bool force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise, alloc_advise compare);
    int find_section(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, alloc_advise advise) ;

    int fetch_region(section_e &alloc_section, uint32_t section_offset, uint16_t size_class, bool use_chance, region_e &alloc_region, uint32_t &region_index, uint32_t skip_mask) ;
    int free_region_block(uint64_t addr, bool is_exclusive, uint16_t block_class);

    inline uint64_t get_section_region_addr(uint32_t section_offset, uint32_t region_offset) {return heap_start_ + section_offset*section_size_ + region_offset * region_size_ ;};
    inline uint64_t get_region_addr(uint32_t region_index) {return heap_start_ + region_index * region_size_;};
    inline uint64_t get_region_block_addr(uint32_t region_index, uint32_t block_offset) {return heap_start_ + region_index * region_size_ + block_offset * block_size_;} ;
    inline uint32_t get_region_block_rkey(uint32_t region_index, uint32_t block_offset) {return block_rkey_[region_index*block_per_region + block_offset].load().main_rkey_;};

    int fetch_region_block(section_e &alloc_section, region_e &alloc_region, uint64_t &addr, uint32_t &rkey, bool is_exclusive, uint32_t region_index, uint16_t block_class) ;

    inline bool set_block_rkey(uint64_t index, uint32_t rkey) {
        rkey_table_e table = block_rkey_[index].load();
        rkey_table_e new_table;
        do{
            new_table = table;
            new_table.main_rkey_ = rkey;
        }while(!block_rkey_[index].compare_exchange_weak(table, new_table));
        return true;
    };
    inline bool set_backup_rkey(uint64_t index, uint32_t rkey) {
        rkey_table_e table = block_rkey_[index].load();
        rkey_table_e new_table;
        do{
            new_table = table;
            new_table.backup_rkey_ = rkey;
        }while(!block_rkey_[index].compare_exchange_weak(table, new_table));
        return true;
    };

    inline uint64_t get_block_num() {return block_num_;};

    inline uint64_t get_block_size() {return block_size_;};

    inline uint64_t get_block_addr(uint64_t index) {return heap_start_ + index * block_size_;};

    // inline block_header get_block_header(uint64_t index) {return header_list[index];};

    inline uint64_t get_metadata() {return (uint64_t)section_header_;};

    inline uint32_t get_block_rkey(uint64_t index) {return block_rkey_[index].load().main_rkey_;};
    
    inline uint32_t get_backup_rkey(uint64_t index) {return block_rkey_[index].load().backup_rkey_;};

    inline uint64_t get_block_index(uint64_t addr) {return (addr-heap_start_)/block_size_;}

    bool fetch(uint64_t size, uint64_t &addr, uint32_t &rkey) {return true;};

    bool fetch(uint64_t start_addr, uint64_t size, uint64_t &addr, uint32_t &rkey) ;

    bool fill_block(uint64_t addr, uint64_t size, uint32_t rkey) {return true;};

    bool fetch_block(uint64_t &addr, uint32_t &rkey) ;

    void print_state() {};
    
private:

    std::mutex m_mutex_;

    // basic info
    uint64_t block_size_;
    uint64_t block_num_;
    uint64_t region_size_;
    uint64_t region_num_;
    uint64_t section_size_;
    uint64_t section_num_;
    uint32_t global_rkey_;

    // info before heap segment
    section* section_header_;
    region* region_header_;
    rkey_table* block_rkey_;
    block* block_header_;
    
    // info of heap segment
    uint64_t heap_start_;
    uint64_t heap_size_;
    std::ofstream mem_record_;

    // info helping accelerate
    struct cache_info{
        uint64_t current_section_;
        region region_block_cache_;
        section section_cache_;  
    } cache_info_;

    uint64_t retry_counter_;
    std::mt19937 mt;
    
};

class ServerBlockManagerv2: public FreeBlockManager{
public:
    ServerBlockManagerv2(uint64_t block_size, uint64_t base_size):FreeBlockManager(block_size), base_size(base_size) {
        if(block_size_/base_size > 32) {
            printf("bitmap cannot store too much bsae page!\n");
        }
    };
    ~ServerBlockManagerv2() {};
    
    inline uint64_t num_align_upper(uint64_t num, uint64_t align) {
        return (num + align - 1) - ((num + align - 1) % align);
    }

    void init_size_align(uint64_t addr, uint64_t size, uint64_t &init_addr, uint64_t &init_size) override {
        uint64_t align = block_size_*large_block_items < 1024*1024*2 ? 1024*1024*2 : block_size_*large_block_items;
        uint64_t base_align = block_size_ < 1024*1024*2 ? 1024*1024*2 : block_size_;
        init_size = num_align_upper(size, align);
        uint64_t block_header_size = num_align_upper(init_size / align * sizeof(large_block),base_align);
        init_size += block_header_size;
        init_addr = addr - block_header_size;
        assert(init_addr % base_align == 0);
    };

    bool init(uint64_t meta_addr, uint64_t addr, uint64_t size, uint32_t rkey) override;

    inline bool set_block_rkey(uint64_t index, uint32_t rkey) {block_info[index / large_block_items].rkey[index % large_block_items] = rkey; return true;};

    bool set_block_base_rkey(uint64_t index, uint64_t offset, uint32_t rkey) {
        perror("unimplemented!\n");
        return false;
    };

    inline uint64_t get_base_num() {return block_size_/base_size;};

    inline uint64_t get_block_num() {return large_block_num;};

    inline uint64_t get_block_addr(uint64_t index) {return heap_start + index * block_size_;};

    inline uint64_t get_block_addr() {return heap_start;};

    // inline block_header get_block_header(uint64_t index) {return header_list[index];};

    inline large_block* get_metadata() {return block_info;};

    inline uint32_t get_block_rkey(uint64_t index) {return block_info[index / large_block_items].rkey[index % large_block_items];};

    inline uint64_t get_block_index(uint64_t addr) {return (addr-heap_start)/block_size_;}

    inline uint64_t get_base_size() {return base_size;};

    bool fetch(uint64_t size, uint64_t &addr, uint32_t &rkey) override {return true;};

    bool fetch(uint64_t start_addr, uint64_t size, uint64_t &addr, uint32_t &rkey) override;

    bool fill_block(uint64_t addr, uint64_t size, uint32_t rkey) override {return true;};

    bool fetch_block(uint64_t &addr, uint32_t &rkey) override ;

    void print_state() override {};
    
private:

    std::mutex m_mutex_;

    uint64_t base_size;

    uint32_t global_rkey;

    large_block* block_info;

    std::atomic<large_block*> free_list;

    uint64_t heap_start;

    uint64_t heap_size;

    uint64_t large_block_num;

    uint64_t last_alloc;

    uint64_t user_start;
    
};

class ServerBlockManagerv1: public FreeBlockManager{
public:
    ServerBlockManagerv1(uint64_t block_size, uint64_t base_size):FreeBlockManager(block_size), base_size(base_size) {
        if(block_size_/base_size > 32) {
            printf("bitmap cannot store too much bsae page!\n");
        }
    };
    ~ServerBlockManagerv1() {};
    
    bool init(uint64_t addr, uint64_t size, uint32_t rkey) override ;

    inline bool set_block_rkey(uint64_t index, uint32_t rkey) {block_rkey_list[index] = rkey; return true;};

    bool set_block_base_rkey(uint64_t index, uint64_t offset, uint32_t rkey) {
        uint64_t start_addr = get_block_addr(index);
        *(uint32_t*)(start_addr + offset*base_size) = rkey;
        return true;
    };

    inline uint64_t get_base_num() {return block_size_/base_size;};

    inline uint64_t get_block_num() {return large_block_num;};

    inline uint64_t get_block_addr(uint64_t index) {return heap_start + index * block_size_;};

    inline uint64_t get_block_addr() {return heap_start;};

    inline block_header* get_metadata() {return header_list;};

    inline uint32_t get_block_rkey(uint64_t index) {return block_rkey_list[index];};

    inline uint64_t get_block_index(uint64_t addr) {return (addr-heap_start)/block_size_;}

    inline uint64_t get_base_size() {return base_size;};

    inline uint64_t get_rkey_list_addr() {return (uint64_t)block_rkey_list;};

    bool fetch(uint64_t size, uint64_t &addr, uint32_t &rkey) override {return true;};

    bool fetch(uint64_t start_addr, uint64_t size, uint64_t &addr, uint32_t &rkey) override;

    bool fill_block(uint64_t addr, uint64_t size, uint32_t rkey) override {return true;};

    bool fetch_block(uint64_t &addr, uint32_t &rkey) override ;

    void print_state() override {};
    
private:

    std::mutex m_mutex_;

    uint64_t base_size;

    uint32_t global_rkey;

    block_header* header_list;

    uint32_t* block_rkey_list;

    uint64_t heap_start;

    uint64_t heap_size;

    uint64_t large_block_num;

    std::atomic<uint64_t> last_alloc;

    uint64_t user_start;
    
};

class ClientBlockManager: public FreeBlockManager {
public:
    ClientBlockManager(uint64_t block_size):FreeBlockManager(block_size) {};
    ~ClientBlockManager() {
        while(!free_block_queue_.empty()){
            free_block_queue_.pop();
        }
    };
    
    bool init(uint64_t addr, uint64_t size, uint32_t rkey) override;

    bool fetch(uint64_t size, uint64_t &addr, uint32_t &rkey) override;

    bool fill_block(uint64_t addr, uint64_t size, uint32_t rkey) override;

    bool fetch_block(uint64_t &addr, uint32_t &rkey) override;

    void print_state() override;
    
private:
    std::queue<remote_addr> free_block_queue_;

    std::mutex m_mutex_;

    uint64_t size_;

    uint64_t total_used_;

};

class FreeQueueManager{
public:
    FreeQueueManager(uint64_t block_size, uint64_t pool_size):block_size_(block_size), pool_size_(pool_size) {};
    ~FreeQueueManager() {
        while(!free_block_queue.empty()){
            free_block_queue.pop();
        }
    };
    
    bool init(mr_rdma_addr addr, uint64_t size);

    bool fetch(uint64_t size, mr_rdma_addr &addr);

    bool fill_block(mr_rdma_addr addr, uint64_t size);

    bool fetch_block(mr_rdma_addr &addr);

    bool return_block(mr_rdma_addr addr, bool &all_free);

    bool return_block_no_free(mr_rdma_addr addr, bool &all_free);

    void print_state();
    
private:
    std::queue<mr_rdma_addr> free_block_queue;

    std::map<mr_rdma_addr, uint64_t*> free_bitmap_;

    // const uint64_t queue_watermark = (uint64_t)1 << 30;

    // uint64_t raw_heap; 

    // uint64_t raw_size;

    // uint32_t raw_node;

    std::mutex m_mutex_;

    uint64_t total_used;

    // uint32_t raw_rkey;

    uint64_t block_size_;

    uint64_t pool_size_;
    
};

} // namespace mralloc
