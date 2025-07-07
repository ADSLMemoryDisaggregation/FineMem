

#include <bits/stdint-uintn.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/sem.h>
#include <semaphore.h>

#pragma once

namespace mralloc {

#ifdef FAULT_INJECTION
std::mt19937 mt_rand = std::mt19937(std::random_device{}());
std::uniform_int_distribution<int> dis = std::uniform_int_distribution<int>(1, 50);
#endif

#ifdef FAULT_INJECTION
    #define POTENTIAL_FAULT    \
        if(dis(mt_rand) == 1)  \
        {                      \
            _exit(0);           \
        }                      
#else
    #define POTENTIAL_FAULT
#endif

typedef enum mi_malloc_kind_e {
    MI_MALLOC_NORMAL,
    MI_MALLOC_SHADOW
} mi_malloc_kind_t;

// thread id's
typedef size_t     mi_threadid_t;

typedef union mi_page_flags_s {
  uint8_t full_aligned;
  struct {
    uint8_t in_full : 1;
    uint8_t has_aligned : 1;
  } x;
} mi_page_flags_t;

typedef uintptr_t mi_thread_free_t;

typedef enum mi_page_kind_e {
  MI_PAGE_SMALL,    // small blocks go into 64KiB pages inside a segment
  MI_PAGE_MEDIUM,   // medium blocks go into 512KiB pages inside a segment
  MI_PAGE_LARGE,    // larger blocks go into a single page spanning a whole segment
  MI_PAGE_HUGE      // huge blocks (>512KiB) are put into a single page in a segment of the exact size (but still 2MiB aligned)
} mi_page_kind_t;

typedef struct mi_page_migrate {
  // "owned" by the segment
  uint8_t               segment_idx;       // index in the segment `pages` array, `page == &segment->pages[page->segment_idx]`
  uint8_t               segment_in_use:1;  // `true` if the segment allocated this page
  uint8_t               is_committed:1;    // `true` if the page virtual memory is committed
  uint8_t               is_zero_init:1;    // `true` if the page was initially zero initialized

  // layout like this to optimize access in `mi_malloc` and `mi_free`
  uint16_t              capacity;          // number of blocks committed, must be the first field, see `segment.c:page_clear`
  uint16_t              reserved;          // number of blocks reserved in memory
  mi_page_flags_t       flags;             // `in_full` and `has_aligned` flags (8 bits)
  uint8_t               free_is_zero:1;    // `true` if the blocks in the free list are zero initialized
  uint8_t               retire_expire:7;   // expiration count for retired blocks

  uint64_t           free[128];              // list of available free blocks (`malloc` allocates from this list)
  uint32_t              used;              // number of blocks in use (including blocks in `local_free` and `thread_free`)
  uint32_t              xblock_size;       // size available in each block (always `>0`)
  uint64_t           local_free[128];        // list of deferred free blocks by this thread (migrates to `free`)
  uint64_t          remote_free[128];

  #if (MI_ENCODE_FREELIST || MI_PADDING)
  uintptr_t             keys[2];           // two random keys to encode the free lists (see `_mi_block_next`) or padding canary
  #endif

  uint64_t  xthread_free[128];  // list of deferred free blocks freed by other threads
  uintptr_t        xheap;

  struct mi_page_s*     next;              // next page owned by this thread with the same `block_size`
  struct mi_page_s*     prev;              // previous page owned by this thread with the same `block_size`
    mi_malloc_kind_t    malloc_kind;
    pthread_mutex_t mutex;
    void* page_start;
} mi_page_t;

typedef struct mi_segment_migrate {
  // constant fields
  bool                 allow_decommit;
  bool                 allow_purge;
  uintptr_t             segment_start;
  size_t               segment_size;     // for huge pages this may be different from `MI_SEGMENT_SIZE`
  
  // segment fields
  struct mi_segment_s* abandoned_next;
  struct mi_segment_s* next;             // must be the first segment field after abandoned_next -- see `segment.c:segment_init`
  struct mi_segment_s* prev;

  size_t               abandoned;        // abandoned pages (i.e. the original owning thread stopped) (`abandoned <= used`)
  size_t               abandoned_visits; // count how often this segment is visited in the abandoned list (to force reclaim if it is too long)

  size_t               used;             // count of pages in use (`used <= capacity`)
  size_t               capacity;         // count of available pages (`#free + used`)
  size_t               segment_info_size;// space we are using from the first page for segment meta-data and possible guard pages.
  uintptr_t            cookie;           // verify addresses in secure mode: `_mi_ptr_cookie(segment) == segment->cookie`

  // layout like this to optimize access in `mi_free`
  size_t                 page_shift;     // `1 << page_shift` == the page sizes == `page->block_size * page->reserved` (unless the first page, then `-segment_info_size`).
  mi_threadid_t thread_id;      // unique id of the thread owning this segment
  mi_page_kind_t       page_kind;        // kind of pages: small, medium, large, or huge
  mi_malloc_kind_t      malloc_kind;
  mi_page_migrate            pages[64];         // up to `MI_SMALL_PAGES_PER_SEGMENT` pages
};

const uint64_t BITMAP_SIZE = (uint64_t)1024*1024*1024*16;
const uint32_t nprocs = 48;
const uint32_t max_alloc_item = 16;
const uint32_t max_free_item = 16;

enum LegoOpcode {LegoIdle, LegoFindSeg, LegoFindSegSucc, LegoReg, LegoAlloc, LegoFree, LegoDereg, LegoRemoteFree, LegoTransfer};

struct CpuBuffer {
    LegoOpcode opcode_;
    char buffer_[1024*1024];
    uint64_t doorbell_id;
    uint64_t retbell_id;
    uint64_t lock_id;
};

template <typename T>
class ring_buffer{
public:
    ring_buffer(uint32_t max_length, T* buffer, T zero, uint32_t* reader, uint32_t* writer)
         : max_length_(max_length), buffer_(buffer), zero_(zero), reader_(reader), writer_(writer) {
    }
    
    void clear() {
        for(int i = 0; i < max_length_; i++) {
            buffer_[i] = zero_;
        }
        *reader_=0;
        *writer_=0;
    }

    uint32_t get_length() {
        volatile uint32_t reader = *reader_;
        volatile uint32_t writer = *writer_;
        if (reader == writer) {
            return 0;
        }
        if (writer > reader) {
            return (writer - reader);
        } else {
            return (max_length_ - reader + writer);
        }
    }

    void add_cache(T value){
        // host side fill cache, add write pointer
        volatile uint32_t writer = *writer_;
	    while(get_length() >= max_length_ - 1){
		    printf("busy wait\n");
	    }
        if(get_length() < max_length_-1){
            buffer_[writer] = value;        
            *writer_ = (writer + 1) % max_length_;
        }
    }

    void add_batch(T* value, uint64_t num) {
        volatile uint32_t writer = *writer_;
        if(get_length() < max_length_-num){
            for(int i = 0; i < num; i++){
                buffer_[(writer + i) % max_length_] = value[i];        
            }
            *writer_ = (writer + num) % max_length_;
        }
    }

    bool force_fetch_cache(T &value) {
        volatile uint32_t reader = *reader_;
        while(get_length() == 0 ) ;
        do{
            while(!(buffer_[reader] != zero_)){
                reader = *reader_;
            }
            *reader_ = (reader + 1) % max_length_;
        }while(!(buffer_[reader] != zero_));
        value = buffer_[reader];
        buffer_[reader] = zero_;
        return true;
    }

    bool try_fetch_cache(T &value) {
        volatile uint32_t reader = *reader_;
        if(get_length() == 0) {
            return false;
        } 
        do{
            while(!(buffer_[reader] != zero_)){
                reader = *reader_;
            }
            *reader_ = (reader + 1) % max_length_;
        }while(!(buffer_[reader] != zero_));
        value = buffer_[reader];
        buffer_[reader] = zero_;
        return true;
    }

    bool try_fetch_batch(T* value, uint64_t num) {
        volatile uint64_t length = get_length();
        volatile uint32_t reader = *reader_;
        if(length < num) {
            return false;
        } 
        *reader_ = (reader + num) % max_length_;
        for(int i = 0; i< num;i++) {
            while(!(buffer_[(reader + i) % max_length_] != zero_));
            value[i] = buffer_[(reader + i) % max_length_];
            buffer_[(reader + i) % max_length_] = zero_;
        }
        return true;
    }

    int try_fetch_batch_all(T *value) {
        volatile uint64_t length = get_length();
        volatile uint32_t reader = *reader_;
        if(length == 0) {
            return 0;
        } 
        *reader_ = (reader + length) % max_length_;
        for(int i = 0; i< length;i++) {
            while(!(buffer_[(reader + i) % max_length_] != zero_));
            value[i] = buffer_[(reader + i) % max_length_];
            buffer_[(reader + i) % max_length_] = zero_;
        }
        return length;
    }

private:
    uint32_t max_length_;
    T* volatile buffer_;
    T zero_;
    volatile uint32_t* reader_;
    volatile uint32_t* writer_;
};

template <typename T>
class ring_buffer_atomic{
public:
    ring_buffer_atomic(uint32_t max_length, T* buffer, T zero, std::atomic<uint32_t>* reader, std::atomic<uint32_t>* writer)
         : max_length_(max_length), buffer_(buffer), zero_(zero), reader_(reader), writer_(writer) {
    }
    
    void clear() {
        for(int i = 0; i < max_length_; i++) {
            buffer_[i] = zero_;
        }
        reader_->store(0);
        writer_->store(0);
    }

    int get_length() {
        uint32_t reader = reader_->load()%max_length_;
        uint32_t writer = writer_->load()%max_length_;
        if(!(buffer_[reader] != zero_))
            return 0;
        if (reader == writer) {
            return 0;
        }
        if (writer > reader) {
            return (writer - reader);
        } else {
            return (max_length_ - reader + writer);
        }
    }

    void add_cache(T value){
        // host side fill cache, add write pointer
        uint32_t writer;
        if(get_length() < max_length_-1){
            writer = writer_->fetch_add(1)%max_length_;
        } else {
            return;
        }
        buffer_[writer] = value;
    }

    void add_batch(T* value, uint64_t num) {
        uint32_t writer;
        if(get_length() < max_length_-num) {
            writer = writer_->fetch_add(num);
            for(int i = 0; i < num; i++){
                buffer_[(writer + i) % max_length_] = value[i];        
            }
        }
    }

    bool force_fetch_cache(T &value) {
        uint32_t reader = 0;
        reader = reader_->fetch_add(1)%max_length_;
        while(!(buffer_[reader] != zero_));
        value = buffer_[reader];
        buffer_[reader] = zero_;
        return true;
    }

    bool try_fetch_batch(T* value, uint64_t num) {
        uint32_t reader = reader_->load();
        uint32_t new_reader;
        do {
            if(get_length() < num) {
                return false;
            } 
            new_reader = (reader + num) % max_length_;
        }while(!reader_->compare_exchange_strong(reader, new_reader));
        for(int i = 0; i < num; i++){
            while(!(buffer_[(reader + i) % max_length_] != zero_));
            value[i] = buffer_[(reader + i) % max_length_];
            buffer_[(reader + i) % max_length_] = zero_;
        }
        return true;
    }

    int try_fetch_batch_all(T *value) {
        uint64_t length = get_length();
        if(length == 0) {
            return 0;
        } 
        uint32_t reader = reader_->load();
        uint32_t new_reader;
        do {
            new_reader = (reader + length) % max_length_;
        }while(!reader_->compare_exchange_strong(reader, new_reader));
        for(int i = 0; i< length; i++) {
            while(!(buffer_[(reader + i) % max_length_] != zero_));
            value[i] = buffer_[(reader + i) % max_length_];
            buffer_[(reader + i) % max_length_] = zero_;
        }
        return length;
    }

    bool try_fetch_cache(T &value) {
        uint32_t reader = reader_->load();
        uint32_t new_reader;
        do {
            if(get_length() == 0) {
                return false;
            } 
            new_reader = (reader + 1) % max_length_;
        }while(!reader_->compare_exchange_strong(reader, new_reader));
        while(!(buffer_[reader] != zero_));
        value = buffer_[reader];
        buffer_[reader] = zero_;
        return true;
    }

private:
    uint32_t max_length_;
    T* volatile buffer_;
    T zero_;
    std::atomic<uint32_t>* reader_;
    std::atomic<uint32_t>* writer_;
};

struct mr_rdma_addr_{
    uint64_t addr;
    uint32_t rkey;
};

class mr_rdma_addr{
public:
    mr_rdma_addr() {addr = -1; rkey = -1; node = -1;}
    mr_rdma_addr(uint64_t addr, uint32_t rkey, uint32_t node): addr(addr), rkey(rkey), node(node) {}
    bool operator==(mr_rdma_addr &compare) {
        return addr == compare.addr && rkey == compare.rkey && node == compare.node;
    }
    bool operator!=(mr_rdma_addr &compare) {
        return addr != compare.addr && rkey != compare.rkey && node != compare.node;
    }
    bool operator<(const mr_rdma_addr &compare) const {
        return node < compare.node || (node == compare.node && addr < compare.addr) ;
    }
    mr_rdma_addr& operator=(mr_rdma_addr &value) {
        this->addr = value.addr;
        this->rkey = value.rkey;
        this->node = value.node;
        return *this;
    }
    uint64_t addr;
    uint32_t rkey;
    uint16_t node;
    uint16_t size;
} ;

class cpu_cache{
public:
    
    CpuBuffer* buffer_;
    std::atomic<uint64_t>* bitmap_;
    uint64_t current_index = 0;

    sem_t* doorbell[nprocs]; 
    sem_t* retbell[nprocs];
    sem_t* lock[nprocs];

    bool freed = false;

    cpu_cache() {
        int fd = shm_open("/cpu_cache", O_RDWR, 0);
        // int bitmap_fd = shm_open("/bitmaps", O_RDWR, 0);
        if (fd == -1) {
            perror("init failed, no computing node running");
        } else {
            int port_flag = PROT_READ | PROT_WRITE;
            int mm_flag   = MAP_SHARED; 
            buffer_ = (CpuBuffer*)mmap(NULL, sizeof(CpuBuffer)*nprocs, port_flag, mm_flag, fd, 0);   
            bitmap_ = (std::atomic<uint64_t>*)mmap(NULL, BITMAP_SIZE, port_flag, mm_flag, fd, 0);
            for(int i = 0; i < nprocs; i++) {
                doorbell[i] = sem_open(std::to_string(buffer_[i].doorbell_id).c_str(), O_CREAT, 0666, 0);
                retbell[i] = sem_open(std::to_string(buffer_[i].retbell_id).c_str(), O_CREAT, 0666, 0);
                lock[i] = sem_open(std::to_string(buffer_[i].lock_id).c_str(), O_CREAT, 0666, 1);
            }
        }
        
    }

    cpu_cache(uint64_t cache_size) {
        int port_flag = PROT_READ | PROT_WRITE;
        int mm_flag   = MAP_SHARED; 
        int fd = shm_open("/cpu_cache", O_RDWR, 0);
        // int bitmap_fd = shm_open("/bitmaps", O_RDWR, 0);
        if(fd==-1){
            fd = shm_open("/cpu_cache", O_CREAT | O_EXCL | O_RDWR, 0600);
            // bitmap_fd = shm_open("/bitmaps", O_CREAT | O_EXCL | O_RDWR, 0600);
            if(ftruncate(fd, sizeof(CpuBuffer)*nprocs)){
                perror("create shared memory failed");
            }
            // if(ftruncate(bitmap_fd, BITMAP_SIZE)){
            //     perror("create shared memory failed");
            // }
            buffer_ = (CpuBuffer*)mmap(NULL, sizeof(CpuBuffer)*nprocs, port_flag, mm_flag, fd, 0);
            bitmap_ = (std::atomic<uint64_t>*)mmap(NULL, BITMAP_SIZE, port_flag, mm_flag, fd, 0);
            for(int i = 0; i < nprocs; i++) {
                buffer_[i].doorbell_id = i+114514;
                buffer_[i].retbell_id = i+nprocs+114514;
                buffer_[i].lock_id = i+2*nprocs+114514;
                buffer_[i].opcode_ = LegoOpcode::LegoIdle;
                memset(buffer_[i].buffer_, 0, 1024*1024);
                doorbell[i] = sem_open(std::to_string(buffer_[i].doorbell_id).c_str(), O_CREAT, 0666, 0);
                retbell[i] = sem_open(std::to_string(buffer_[i].retbell_id).c_str(), O_CREAT, 0666, 0);
                lock[i] = sem_open(std::to_string(buffer_[i].lock_id).c_str(), O_CREAT, 0666, 1);
            }
        }
        else {
            buffer_ = (CpuBuffer*)mmap(NULL, sizeof(CpuBuffer)*nprocs, port_flag, mm_flag, fd, 0);
            for(int i = 0; i < nprocs; i++) {
                doorbell[i] = sem_open(std::to_string(buffer_[i].doorbell_id).c_str(), O_CREAT, 0666, 0);
                retbell[i] = sem_open(std::to_string(buffer_[i].retbell_id).c_str(), O_CREAT, 0666, 0);
                lock[i] = sem_open(std::to_string(buffer_[i].lock_id).c_str(), O_CREAT, 0666, 1);
            }
        }
    }

    ~cpu_cache(){
        if(buffer_)
            munmap(buffer_, sizeof(CpuBuffer)*nprocs);
        // if(!freed){
        for(int i=0; i<nprocs; i++){
            sem_close(doorbell[i]);
            sem_close(retbell[i]);
            sem_close(lock[i]);
        }
        //     if(buffer_)
        //         munmap(buffer_,  sizeof(CpuBuffer)*nprocs);
        //     shm_unlink("/cpu_cache");
        //     freed = true;
        // }
    }

    uint64_t bitmap_malloc(uint64_t bin_size){
        uint64_t allocate_index = current_index;
        if(bin_size <= 16*1024)
            current_index += (64*1024/bin_size);
        else if (bin_size <= 128*1024)
            current_index += (512*1024/bin_size);
        else if (bin_size <= 2*1024*1024)
            current_index += (4*1024*1024/bin_size);
        return allocate_index;
    }

    void free_cache(){
        // only the global host side need call this, to free all cpu_cache 
        // printf("free\n");
        if(!freed){
            for(int i=0; i<nprocs; i++){
                sem_close(doorbell[i]);
                sem_unlink(std::to_string(buffer_[i].doorbell_id).c_str());
                sem_close(retbell[i]);
                sem_unlink(std::to_string(buffer_[i].retbell_id).c_str());
                sem_close(lock[i]);
                sem_unlink(std::to_string(buffer_[i].lock_id).c_str());
            }
            if(buffer_)
                munmap(buffer_,  sizeof(CpuBuffer)*nprocs);
            shm_unlink("/cpu_cache");
            freed = true;
        }
    }

    bool malloc(uint64_t bin_size, mr_rdma_addr &addr, uint64_t &shm_index){
        unsigned nproc;
        if((nproc = sched_getcpu()) == -1){
            printf("sched_getcpu bad \n");
            return false;
        }
        sem_wait(lock[nproc]);
        buffer_[nproc].opcode_ = LegoOpcode::LegoAlloc;
        *(uint64_t*)buffer_[nproc].buffer_ = bin_size;
        sem_post(doorbell[nproc]);
        // printf("send request to %d\n", nproc);
        sem_wait(retbell[nproc]);
        // printf("recieve from %d\n", nproc);
        addr.addr = ((mr_rdma_addr*)buffer_[nproc].buffer_)->addr;
        addr.rkey = ((mr_rdma_addr*)buffer_[nproc].buffer_)->rkey;
        addr.node = ((mr_rdma_addr*)buffer_[nproc].buffer_)->node;
        addr.size = ((mr_rdma_addr*)buffer_[nproc].buffer_)->size;
        shm_index = *(uint64_t*)((mr_rdma_addr*)buffer_[nproc].buffer_+1);
        sem_post(lock[nproc]);
        return true;
    }
    
    bool try_find_segment(uint64_t bin_size, mi_segment_migrate& segment){
        unsigned nproc;
        if((nproc = sched_getcpu()) == -1){
            printf("sched_getcpu bad \n");
            return false;
        }
        sem_wait(lock[nproc]);
        buffer_[nproc].opcode_ = LegoOpcode::LegoFindSeg;
        *(uint64_t*)buffer_[nproc].buffer_ = bin_size;
        sem_post(doorbell[nproc]);
        // printf("send request to %d\n", nproc);
        sem_wait(retbell[nproc]);
        // printf("recieve from %d\n", nproc);
        if(buffer_[nproc].opcode_ == LegoOpcode::LegoFindSegSucc){
            //TODO: copy a full segment, with its bitmaps
            sem_post(lock[nproc]);
            return true;
        } else {
            sem_post(lock[nproc]);
            return false;
        }
        return false;
    }

    bool free(mr_rdma_addr addr){
        unsigned nproc;
        if((nproc = sched_getcpu()) == -1){
            printf("sched_getcpu bad \n");
            return false;
        }
        sem_wait(lock[nproc]);
        buffer_[nproc].opcode_ = LegoOpcode::LegoFree;
        ((mr_rdma_addr*)buffer_[nproc].buffer_)->addr = addr.addr;
        ((mr_rdma_addr*)buffer_[nproc].buffer_)->rkey = addr.rkey;
        ((mr_rdma_addr*)buffer_[nproc].buffer_)->size = addr.size;
        ((mr_rdma_addr*)buffer_[nproc].buffer_)->node = addr.node;
        sem_post(doorbell[nproc]);
        // printf("send request to %d\n", nproc);
        sem_wait(retbell[nproc]);
        // printf("recieve from %d\n", nproc);
        sem_post(lock[nproc]);
        return true;
    }

    bool remote_free(mr_rdma_addr addr){
        unsigned nproc;
        if((nproc = sched_getcpu()) == -1){
            printf("sched_getcpu bad \n");
            return false;
        }
        sem_wait(lock[nproc]);
        buffer_[nproc].opcode_ = LegoOpcode::LegoRemoteFree;
        *(mr_rdma_addr*)buffer_[nproc].buffer_ = addr;
        sem_post(doorbell[nproc]);
        // printf("send request to %d\n", nproc);
        sem_wait(retbell[nproc]);
        // printf("recieve from %d\n", nproc);
        sem_post(lock[nproc]);
        return true;
    }

    bool registe(uint16_t process_id){
        unsigned nproc;
        if((nproc = sched_getcpu()) == -1){
            printf("sched_getcpu bad \n");
            return false;
        }
        sem_wait(lock[nproc]);
        buffer_[nproc].opcode_ = LegoOpcode::LegoReg;
        *(uint16_t*)buffer_[nproc].buffer_ = process_id;
        sem_post(doorbell[nproc]);
        // printf("send request to %d\n", nproc);
        sem_wait(retbell[nproc]);
        // printf("recieve from %d\n", nproc);
        sem_post(lock[nproc]);
        return true;
    }

    bool deregiste(uint16_t process_id){
        unsigned nproc;
        if((nproc = sched_getcpu()) == -1){
            printf("sched_getcpu bad \n");
            return false;
        }
        sem_wait(lock[nproc]);
        buffer_[nproc].opcode_ = LegoOpcode::LegoDereg;
        *(uint16_t*)buffer_[nproc].buffer_ = process_id;
        sem_post(doorbell[nproc]);
        // printf("send request to %d\n", nproc);
        sem_wait(retbell[nproc]);
        // printf("recieve from %d\n", nproc);
        sem_post(lock[nproc]);
        return true;
    }

};

}
