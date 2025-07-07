

#include <bits/stdint-uintn.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace mralloc{

const uint64_t server_base_addr = 0x10000000;
const bool fusee_enable = true;

#define MAX_REP_NUM         10

#define RACE_HASH_GLOBAL_DEPTH              (5)
#define RACE_HASH_INIT_LOCAL_DEPTH          (5)
#define RACE_HASH_SUBTABLE_NUM              (1 << RACE_HASH_GLOBAL_DEPTH)
#define RACE_HASH_INIT_SUBTABLE_NUM         (1 << RACE_HASH_INIT_LOCAL_DEPTH)
#define RACE_HASH_MAX_GLOBAL_DEPTH          (5)
#define RACE_HASH_MAX_SUBTABLE_NUM          (1 << RACE_HASH_MAX_GLOBAL_DEPTH)
#define RACE_HASH_ADDRESSABLE_BUCKET_NUM    (34000ULL)
#define RACE_HASH_SUBTABLE_BUCKET_NUM       (RACE_HASH_ADDRESSABLE_BUCKET_NUM * 3 / 2)
#define RACE_HASH_ASSOC_NUM                 (7)
#define RACE_HASH_RESERVED_MAX_KV_NUM       (1024ULL * 1024 * 10)
#define RACE_HASH_KVOFFSET_RING_NUM         (1024ULL * 1024 * 16)
#define RACE_HASH_KV_BLOCK_LENGTH           (64ULL)
#define SUBTABLE_USED_HASH_BIT_NUM          (32)
#define RACE_HASH_MASK(n)                   ((1 << n) - 1)


#define ROOT_RES_LEN          (sizeof(RaceHashRoot))
#define SUBTABLE_LEN          (RACE_HASH_ADDRESSABLE_BUCKET_NUM * sizeof(RaceHashBucket))
#define SUBTABLE_RES_LEN      (RACE_HASH_MAX_SUBTABLE_NUM * SUBTABLE_LEN)
#define KV_RES_LEN            (RACE_HASH_RESERVED_MAX_KV_NUM * RACE_HASH_KV_BLOCK_LENGTH)
#define META_AREA_LEN         (256 * 1024 * 1024)
#define GC_AREA_LEN           (0)
#define HASH_AREA_LEN         (128 * 1024 * 1024)
#define CLIENT_META_LEN       (1 * 1024 * 1024)
#define CLIENT_GC_LEN         (1 * 1024 * 1024)

static inline uint64_t round_up(uint64_t addr, uint32_t align) {
    return ((addr) + align - 1) - ((addr + align - 1) % align);
}

typedef struct __attribute__((__packed__)) TagRaceHashSlot {
    uint8_t fp;
    uint8_t kv_len;
    uint8_t server_id;
    uint8_t pointer[5];
} RaceHashSlot;

typedef struct __attribute__((__packed__)) TagRacsHashBucket {
    uint32_t local_depth;
    uint32_t prefix;
    RaceHashSlot slots[RACE_HASH_ASSOC_NUM];
} RaceHashBucket;

typedef struct TagRaceHashSubtableEntry {
    uint8_t lock;
    uint8_t local_depth;
    uint8_t server_id;
    uint8_t pointer[5];
} RaceHashSubtableEntry;

typedef struct TagRaceHashRoot {
    uint64_t global_depth;
    uint64_t init_local_depth;
    uint64_t max_global_depth;
    uint64_t prefix_num;
    uint64_t subtable_res_num;
    uint64_t subtable_init_num;
    uint64_t subtable_hash_num;
    uint64_t subtable_hash_range;
    uint64_t subtable_bucket_num;
    uint64_t seed;

    uint64_t mem_id;
    uint64_t root_offset;
    uint64_t subtable_offset;
    uint64_t kv_offset;
    uint64_t kv_len;
    
    uint64_t lock;
    RaceHashSubtableEntry subtable_entry[RACE_HASH_MAX_SUBTABLE_NUM][MAX_REP_NUM];
} RaceHashRoot;

static inline uint64_t roundup_256(uint64_t len) {
    if (len % 256 == 0) {
        return len;
    }
    return (len / 256 + 1) * 256;
}

class RPC_Fusee{
public:
    RPC_Fusee(uint64_t meta_addr, uint64_t hash_addr, uint32_t rkey){
        if(meta_addr == server_base_addr && hash_addr - meta_addr == META_AREA_LEN){
            meta_addr_ = meta_addr;
            hash_addr_ = hash_addr;
            rkey_ = rkey;
            init_hashtable();
        } else {
            printf("meta addr %lx is not set as base_addr %lx!\n", meta_addr, server_base_addr);
        }
    }

    int init_root(void * root_addr) {
        RaceHashRoot * root = (RaceHashRoot *)root_addr;
        root->global_depth = RACE_HASH_GLOBAL_DEPTH;
        root->init_local_depth = RACE_HASH_INIT_LOCAL_DEPTH;
        root->max_global_depth = RACE_HASH_MAX_GLOBAL_DEPTH;
        root->prefix_num = 1 << RACE_HASH_MAX_GLOBAL_DEPTH;
        root->subtable_res_num = root->prefix_num;
        root->subtable_init_num = RACE_HASH_INIT_SUBTABLE_NUM;
        root->subtable_hash_range = RACE_HASH_ADDRESSABLE_BUCKET_NUM;
        root->subtable_bucket_num = RACE_HASH_SUBTABLE_BUCKET_NUM;
        root->seed = rand();
        root->root_offset = hash_addr_;
        root->subtable_offset = root->root_offset + roundup_256(ROOT_RES_LEN);
        root->kv_offset = hash_addr_ + HASH_AREA_LEN;
        root->kv_len = KV_RES_LEN;
        root->lock = 0;

        return 0;
    }

    int init_subtable(void * subtable_addr) {
        uint64_t max_subtables = (hash_addr_ + HASH_AREA_LEN - (uint64_t)subtable_addr) / roundup_256(SUBTABLE_LEN);

        subtable_alloc_map_.resize(max_subtables);
        for (int i = 0; i < max_subtables; i ++) {
            uint64_t cur_subtable_addr = (uint64_t)subtable_addr + i * roundup_256(SUBTABLE_LEN);
            subtable_alloc_map_[i] = 0;
            for (int j = 0; j < RACE_HASH_ADDRESSABLE_BUCKET_NUM; j ++) {
                RaceHashBucket * bucket = (RaceHashBucket *)cur_subtable_addr + j;
                bucket->local_depth = RACE_HASH_INIT_LOCAL_DEPTH;
                bucket->prefix = i;
                bucket ++;
            }
        }

        return 0;
    }

    int init_hashtable() {
        uint64_t root_addr = hash_addr_;
        uint64_t subtable_st_addr = hash_addr_ + roundup_256(ROOT_RES_LEN);
        init_root((void *)(root_addr));
        init_subtable((void *)(subtable_st_addr));
        printf("init success at %lx, %lx\n", root_addr, subtable_st_addr);
        return 0;
    }

    uint64_t mm_alloc_subtable() {
        int ret = 0;
        uint64_t subtable_st_addr = hash_addr_ + roundup_256(ROOT_RES_LEN);
        for (size_t i = 0; i < subtable_alloc_map_.size(); i ++) {
            if (subtable_alloc_map_[i] == 0) {
                subtable_alloc_map_[i] = 1;
                return subtable_st_addr + i * roundup_256(SUBTABLE_LEN);
            }
        }
    return 0;
    }

    uint32_t get_rkey() {
        return rkey_;
    }

private:
    uint64_t meta_addr_;
    uint64_t hash_addr_;
    uint32_t rkey_;
    std::vector<bool> subtable_alloc_map_;

};

};