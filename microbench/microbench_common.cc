
#include <pthread.h>
#include <sys/select.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include "free_block_manager.h"
#include "msg.h"
#include "rdma_conn.h"
#include "rdma_conn_manager.h"
#include "cpu_cache.h"
#include <sys/time.h>
#include <gperftools/profiler.h>
#include <random>
#include <memkind.h>
#include <fixed_allocator.h>
#include "hiredis/hiredis.h"
#include <string>

const int iteration = 1000;
const int free_num = 250;
const int epoch = 500;
int size_class = 0;
int node_num = 0;

enum alloc_type { cxl_shm_alloc, fusee_alloc, rpc_alloc, share_alloc, exclusive_alloc, pool_alloc, bitmap_alloc, cache_alloc, cache_thread };

enum test_type { stage_test, shuffle_test, short_test, frag_test };

// alloc_type type = cxl_shm_alloc;
alloc_type type = share_alloc;

test_type test = short_test;

pthread_barrier_t start_barrier;
pthread_barrier_t end_barrier;
std::ofstream malloc_result;
std::ofstream free_result;
std::ofstream result_detail;
pthread_mutex_t file_lock;

std::atomic<int> malloc_record_global[100000];
std::atomic<int> free_record_global[100000];
std::atomic<uint64_t> allocate_size;
volatile double malloc_avg[128] = {0};
volatile double cas_avg[128] = {0};
volatile int cas_max[128] = {0};
std::atomic<uint64_t> free_avg;
std::atomic<uint64_t> id;

void* run_woker_thread(void* arg){
    std::ofstream result;
    result.open("mem_trace");
    mralloc::ConnectionManager* conn = (mralloc::ConnectionManager*)arg;
    while(1){
      uint64_t remote_usage;
      conn->remote_print_alloc_info(remote_usage);
      result << remote_usage/1024/1024 << "," << allocate_size.load()/1024/1024 << std::endl;
      usleep(100000);
    }
    return NULL;
}

static void
init_random_values (unsigned int* random_offsets)
{
    srand(0);
    for (size_t i = 0; i < iteration; i++)
        random_offsets[i] = i;
    size_t x,y;
    unsigned int swap;
    for(size_t i = 0; i < 10*iteration; i++) {
        x = rand () % iteration; y = rand () % iteration;
        swap = random_offsets[x];
        random_offsets[x] = random_offsets[y];
        random_offsets[y] = swap;
    }
}

static void
random_values (unsigned int* random_offsets, std::mt19937 &e)
{
    time_t t;
    // srand((unsigned) time(&t));
    size_t x,y;
    unsigned int swap;
    for(size_t i = 0; i < 10*iteration; i++) {
        x = e() % iteration; y = e() % iteration;
        swap = random_offsets[x];
        random_offsets[x] = random_offsets[y];
        random_offsets[y] = swap;
    }
}

static unsigned int
get_random_offset (unsigned int* random_offsets, unsigned int *state)
{
  unsigned int idx = *state;

  if (idx >= iteration - 1)
    idx = 0;
  else
    idx++;

  *state = idx;

  return random_offsets[idx];
}


class test_allocator{
public:
    test_allocator() {};
    virtual bool malloc(mralloc::mr_rdma_addr &remote_addr) {return false;};
    virtual bool free(mralloc::mr_rdma_addr remote_addr) {return false;};
    virtual bool print_state() {return false;};
    virtual double get_avg_retry() {return 0;};
    virtual int get_max_retry() {return 0;};
    ~test_allocator() {}; 
};

class cxl_shm_allocator : test_allocator{
public:
    cxl_shm_allocator(mralloc::ConnectionManager* conn, uint64_t start_hint) {
        conn_ = conn;
        current_index_ = start_hint;
        std::random_device e;
        mt.seed(e());
    }
    ~cxl_shm_allocator() {};
    bool malloc(mralloc::mr_rdma_addr &remote_addr) override {
        // current_index_ += mt();
        int retry_time = conn_->fetch_block(current_index_, remote_addr.addr, remote_addr.rkey, remote_addr.size);
        if(retry_time) {
            if(retry_time > max_retry) 
                max_retry = retry_time;
            avg_retry = (avg_retry*alloc_num + retry_time)/(alloc_num+1);
	        alloc_num ++;
            return true;
        } else {
            return false;
        }
    };
    bool free(mralloc::mr_rdma_addr remote_addr) override {
        return conn_->free_block(remote_addr.addr, remote_addr.size);
    };
    bool print_state() override {printf("%lf, %d\n", avg_retry, max_retry); return true;};
    double get_avg_retry() {return avg_retry;};
    int get_max_retry() {return max_retry;};

private:
    double avg_retry=0;
    int alloc_num=0;
    int max_retry=0;
    uint64_t current_index_;
    mralloc::ConnectionManager* conn_;
    std::mt19937 mt;
};

class fusee_allocator : test_allocator{
public:
    fusee_allocator(mralloc::ConnectionManager* conn) {
        // uint64_t addr = (uint64_t)mmap(NULL, 1024*1024*1024, PROT_READ | PROT_WRITE, 
            // MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        // memset((void*)addr, 0, 1024*1024*1024);
        // memkind_create_fixed((void*)addr, 1024*1024*1024, &kind_);
        conn_ = conn;
    }
    ~fusee_allocator() {};
    bool malloc(mralloc::mr_rdma_addr &remote_addr) override {
        // remote_addr.addr = (uint64_t)memkind_malloc(kind_, remote_addr.size);
        while(conn_->remote_fetch_block(remote_addr.addr, remote_addr.rkey, remote_addr.size)); 
        // printf("%lx\n", remote_addr.addr);
	    return true;
    };
    bool free(mralloc::mr_rdma_addr remote_addr) override {
        // memkind_free(kind_, (void*)remote_addr.addr);
        while(conn_->remote_free_block(remote_addr.addr));
	    return true;
    };
    bool print_state() override {return false;};
private:
    mralloc::ConnectionManager* conn_;
    memkind_t kind_;
};

std::mutex mutex_;

class rpc_allocator : test_allocator{
public:
    rpc_allocator(mralloc::ConnectionManager* conn) {
        conn_ = conn;
    }
    ~rpc_allocator() {};
    bool malloc(mralloc::mr_rdma_addr &remote_addr) override {
        return !conn_->register_remote_memory(remote_addr.addr, remote_addr.rkey, REMOTE_MEM_SIZE*1<<remote_addr.size);
    };
    bool free(mralloc::mr_rdma_addr remote_addr) override {
        return !conn_->unregister_remote_memory(remote_addr.addr);
    };
    bool print_state() override {return false;};
private:
    mralloc::ConnectionManager* conn_;
};

class share_allocator : test_allocator{
public:
    share_allocator(mralloc::ConnectionManager* conn, uint64_t start_hint) {
        conn_ = conn;
        skip_mask = 0;
        cache_region_index = start_hint;
        conn_->find_section(cache_section, cache_section_index, alloc_class, mralloc::alloc_light);
        // conn_->fetch_region(cache_section, cache_section_index, alloc_class, false, cache_region, cache_region_index, skip_mask);
    }
    ~share_allocator() {};
    bool malloc(mralloc::mr_rdma_addr &remote_addr) override {
        int retry_time = conn_->full_alloc(cache_section, cache_section_index, remote_addr.size, remote_addr.addr, remote_addr.rkey);
        return true;
    }
    bool free(mralloc::mr_rdma_addr remote_addr) override {
        int result = conn_->full_free(remote_addr.addr, remote_addr.size);
        return true;
    };
    bool print_state() override {printf("%lf, %d, cas %d, region %d, section %d\n", avg_retry, max_retry, max_cas, max_region, max_section);return false;};
    double get_avg_retry() {return avg_retry;};
    int get_max_retry() {return max_retry;};
private:
    double avg_retry=0;
    int alloc_num = 0;
    int max_retry = 0, max_cas = 0, max_section = 0, max_region = 0;
    uint32_t cache_section_index;
    uint32_t cache_region_index;
    uint32_t skip_mask;
    uint16_t alloc_class = 6;
    mralloc::section_e cache_section;
    mralloc::region_e cache_region;
    mralloc::ConnectionManager* conn_;
};

class pool_allocator : test_allocator{
    public:
        pool_allocator() {
            cpu_cache_ = new mralloc::cpu_cache(4*1024*1024);
        }
        ~pool_allocator() {};
        bool malloc(mralloc::mr_rdma_addr &remote_addr) override {
            uint64_t unused;
            return cpu_cache_->malloc(remote_addr.size, remote_addr, unused);
        };
        bool free(mralloc::mr_rdma_addr remote_addr) override {
            cpu_cache_->free(remote_addr);
            return true;
        };
        bool print_state() override {return false;};
    
    private:
        mralloc::cpu_cache* cpu_cache_;
};

void warmup(test_allocator* alloc) {
    uint64_t addr[iteration];
    uint32_t rkey[iteration];
    /*
     * for(int i = 0; i < iteration; i++) {
        if(!alloc->malloc(addr, rkey)){
            printf("warmup malloc failed\n");
        }
        if(!alloc->free(addr)){
            printf("warmup free failed\n");
        }
    }*/
    /*
    for(int i = 0; i < iteration; i++) {
    	alloc->malloc(addr[i], rkey[i]);
    }
    for(int i = 0; i < iteration; i++) {
        alloc->free(addr[i]);
    }*/
}

void stage_alloc(mralloc::ConnectionManager* conn, test_allocator* alloc, uint64_t thread_id) {
    uint64_t mem_use;
    unsigned int random_offsets[iteration];
    init_random_values(random_offsets);
    uint64_t malloc_avg_time_ = 0, free_avg_time_ = 0;
    uint64_t malloc_count_ = 0, free_count_ = 0;
    struct timeval start, end;
    int malloc_record[100000] = {0};
    int free_record[100000] = {0};
    mralloc::mr_rdma_addr remote_addr[iteration];
    uint64_t current_index = 0;
    int rand_iter = iteration;
    for(int j = 0; j < epoch; j ++) {
        pthread_barrier_wait(&start_barrier);
	    gettimeofday(&start, NULL);
        int allocated = 0;
        for(int i = 0; i < rand_iter; i ++){
            if(remote_addr[i].addr != 0 && remote_addr[i].addr != 0) 
                continue;
            if(!alloc->malloc(remote_addr[i])){
                printf("alloc false\n");
            }
	    //printf("alloc%p,%u\n",addr[i], rkey[i]);
            allocated ++;
        }
        gettimeofday(&end, NULL);
        pthread_barrier_wait(&end_barrier);
        printf("epoch %d malloc finish\n", j);
        if (thread_id == 1)
            conn->remote_print_alloc_info(mem_use);

        // valid check
        /*
	 * char buffer[2][16] = {"aaa", "bbb"};
        char read_buffer[4];
        for(int i = 0; i < rand_iter; i ++){
            if(conn->remote_write(buffer[i%2], 64, addr[i], rkey[i])) {
                printf("wrong write addr %ld, %u\n", addr[i], rkey[i]);
            }
            if(conn->remote_read(read_buffer, 4, addr[i], rkey[i])) {
                printf("wrong read addr %ld, %u\n", addr[i], rkey[i]);
            }
            // printf("access addr %p, %u\n", addr[i], rkey[i]);
            // assert(read_buffer[0] == buffer[i%2][0]);
        }        
        */
	uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        time = time / rand_iter;
	if(time < 1) time = 1;
        if(time > 0 && time < 100000)
            malloc_record[(int)time] += 1;
        malloc_avg_time_ = (malloc_avg_time_*malloc_count_ + time)/(malloc_count_ + 1);
        malloc_count_ += 1;
        printf("epoch %d check finish\n", j);
        
        // free
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        pthread_barrier_wait(&start_barrier);
        gettimeofday(&start, NULL);
        int result; 
        allocated = 0;
        unsigned int offset_state = 0;
        unsigned int next_idx ;
        for(int i = 0; i < rand_iter; i ++){
            next_idx = get_random_offset(random_offsets, &offset_state);
            if(rand()%100 > 20 && remote_addr[next_idx].addr != 0){
                if(!alloc->free(remote_addr[next_idx]))
                    printf("free error!\n");
                remote_addr[next_idx].addr = 0;
                remote_addr[next_idx].rkey = 0;
                allocated ++;
            }
        }
        gettimeofday(&end, NULL);
        pthread_barrier_wait(&end_barrier);
        time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        time = time / rand_iter;
        if(time < 100000)
            free_record[(int)time] += 1;
        free_avg_time_ = (free_avg_time_*free_count_ + time)/(free_count_ + 1);
        free_count_ += 1;
        printf("epoch %d free finish\n", j);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (thread_id == 1)
            conn->remote_print_alloc_info(mem_use);
    }
    for(int i = 0; i < rand_iter; i++) {
	    if(remote_addr[i].addr!=0)
		alloc->free(remote_addr[i]);
    }
    alloc->print_state();
    for(int i=0;i<100000;i++){
        malloc_record_global[i].fetch_add(malloc_record[i]);
        free_record_global[i].fetch_add(free_record[i]);
    }
    malloc_avg[thread_id] = malloc_avg_time_;
    cas_avg[thread_id] = alloc->get_avg_retry();
    cas_max[thread_id] = alloc->get_max_retry();
    free_avg.fetch_add(free_avg_time_);
}

void shuffle_alloc(mralloc::ConnectionManager* conn, test_allocator* alloc, uint64_t thread_id) {
    uint64_t mem_use;
    unsigned int random_offsets[iteration];
    init_random_values(random_offsets);
    double malloc_avg_time_ = 0, free_avg_time_ = 0;
    uint64_t malloc_count_ = 0, free_count_ = 0;
    struct timeval start, end;
    int malloc_record[100000] = {0};
    int free_record[100000] = {0};
    mralloc::mr_rdma_addr remote_addr[iteration];
    // uint64_t addr[iteration]; uint32_t rkey[iteration];
    uint64_t current_index = 0;
    int rand_iter = iteration;
    if(thread_id % 2 == 0){
        pthread_barrier_wait(&start_barrier);
        for(int j = 0; j < epoch; j ++) {
            //pthread_barrier_wait(&start_barrier);
            gettimeofday(&start, NULL);
            int allocated = 0;
            for(int i = 0; i < rand_iter; i ++){
                if(remote_addr[i].addr != 0 && remote_addr[i].rkey != 0) 
                    continue;
                if(!alloc->malloc(remote_addr[i]) || remote_addr[i].addr == 0 || remote_addr[i].addr == -1){
                    printf("alloc false\n");
                }
                allocated ++;
            }
            gettimeofday(&end, NULL);
            //pthread_barrier_wait(&end_barrier);
            // printf("epoch %d malloc finish\n", j);
            if (thread_id == 1)
                conn->remote_print_alloc_info(mem_use);
                
            // valid check
            // char buffer[2][16] = {"aaa", "bbb"};
            // char read_buffer[4];
            // for(int i = 0; i < rand_iter; i ++){
            //     conn->remote_write(buffer[i%2], 64, addr[i], rkey[i]);
            //     conn->remote_read(read_buffer, 4, addr[i], rkey[i]);
            //     // printf("access addr %p\n", addr[i]);
            //     assert(read_buffer[0] == buffer[i%2][0]);
            // }        
            double time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
            time = time / rand_iter;
	    if(time < 1) time  = 1;
            if(time < 100000)
                malloc_record[(int)(time)] += 1;
            malloc_avg_time_ = (malloc_avg_time_*malloc_count_ + time)/(malloc_count_ + 1);
            malloc_count_ += 1;
            // printf("epoch %d check finish\n", j);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            // free
            //pthread_barrier_wait(&start_barrier);
            gettimeofday(&start, NULL);
            int result;        
            unsigned int offset_state = 0;
            unsigned int next_idx ;
            allocated = 0;
            for(int i = 0; i < rand_iter; i ++){
                next_idx = get_random_offset(random_offsets, &offset_state);
                if(rand()%100 > 60 && remote_addr[next_idx].addr != 0){
                    if(!alloc->free(remote_addr[next_idx]))
                        printf("free error!\n");
                    remote_addr[next_idx].addr = 0;
                    remote_addr[next_idx].rkey = 0;
                    allocated++;
                }
            }
            gettimeofday(&end, NULL);
            //pthread_barrier_wait(&end_barrier);
            time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
            time = time / rand_iter;
            if(time < 100000)
                free_record[(int)time] += 1;
            free_avg_time_ = (free_avg_time_*free_count_ + time)/(free_count_ + 1);
            free_count_ += 1;
            // printf("epoch %d free finish\n", j);
            // if (thread_id == 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (thread_id == 1)
                conn->remote_print_alloc_info(mem_use);
            //     conn->remote_print_alloc_info();
        }
    } else {
        for(int i = 0; i < rand_iter; i ++){
            if(remote_addr[i].addr != 0 && remote_addr[i].rkey != 0) 
                continue;
            if(!alloc->malloc(remote_addr[i])|| remote_addr[i].addr == 0){
                printf("alloc false\n");
            }
        }
        pthread_barrier_wait(&start_barrier);
        for(int j = 0; j < epoch; j ++) {
            // valid check
            // char buffer[2][16] = {"aaa", "bbb"};
            // char read_buffer[4];
            // for(int i = 0; i < rand_iter; i ++){
            //     conn->remote_write(buffer[i%2], 64, addr[i], rkey[i]);
            //     conn->remote_read(read_buffer, 4, addr[i], rkey[i]);
            //     // printf("access addr %p\n", addr[i]);
            //     assert(read_buffer[0] == buffer[i%2][0]);
            // }        
            
            // free
            //pthread_barrier_wait(&start_barrier);
            gettimeofday(&start, NULL);
            unsigned int offset_state = 0;
            unsigned int next_idx ;
            int allocated = 0;
            for(int i = 0; i < rand_iter; i ++){
                next_idx = get_random_offset(random_offsets, &offset_state);
                if(rand()%100 > 20 && remote_addr[next_idx].addr != 0){
                    if(!alloc->free(remote_addr[next_idx]))
                        printf("free error!\n");
                    remote_addr[next_idx].addr = 0;
                    remote_addr[next_idx].rkey = 0;
                    allocated ++;
                }
            }
            gettimeofday(&end, NULL);
            //pthread_barrier_wait(&end_barrier);
            uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
            time = time / rand_iter;
	    if (time < 1) time = 1;
            if(time < 100000)
                free_record[(int)time] += 1;
            free_avg_time_ = (free_avg_time_*free_count_ + time)/(free_count_ + 1);
            free_count_ += 1;
            // printf("epoch %d free finish\n", j);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            // if (thread_id == 1)
            //     conn->remote_print_alloc_info();
            
            //pthread_barrier_wait(&start_barrier);
            //gettimeofday(&start, NULL);
            //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            gettimeofday(&start, NULL);
            allocated = 0;
            for(int i = 0; i < rand_iter; i ++){
                if(remote_addr[i].addr != 0 && remote_addr[i].rkey != 0) 
                    continue;
                if(!alloc->malloc(remote_addr[i])|| remote_addr[i].addr == 0|| remote_addr[i].addr == -1){
                    printf("alloc false\n");
                }
                allocated++;
            }
            gettimeofday(&end, NULL);
            //pthread_barrier_wait(&end_barrier);
            // printf("epoch %d malloc finish\n", j);
            if (thread_id == 1)
                conn->remote_print_alloc_info(mem_use);
                
            time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
            time = time / rand_iter;
	    if(time < 1) time  =1;
            if(time < 100000)
                malloc_record[(int)time] += 1;
            malloc_avg_time_ = (malloc_avg_time_*malloc_count_ + time)/(malloc_count_ + 1);
            malloc_count_ += 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        for(int i = 0; i < rand_iter; i ++){
            if(rand()%100 > 20 && remote_addr[i].addr != 0){
                if(!alloc->free(remote_addr[i]))
                    printf("free error!\n");
                    remote_addr[i].addr = 0;
                    remote_addr[i].rkey = 0;
        }
    }
    }
    for(int i=0;i<100000;i++){
        malloc_record_global[i].fetch_add(malloc_record[i]);
        free_record_global[i].fetch_add(free_record[i]);
    }
    for(int i = 0; i < rand_iter; i++) {
    	if(remote_addr[i].addr!=0)
		alloc->free(remote_addr[i]);
    }
    alloc->print_state();
    malloc_avg[thread_id] = malloc_avg_time_;
    cas_avg[thread_id] = alloc->get_avg_retry();
    cas_max[thread_id] = alloc->get_max_retry();
    free_avg.fetch_add(free_avg_time_);
}

void frag_alloc(mralloc::ConnectionManager* conn, test_allocator* alloc, uint64_t thread_id) {
    std::random_device r;
    // std::mt19937 rand_val(r());
    std::mt19937 rand_val(0);
    unsigned int random_offsets[iteration];
    uint64_t time_record[iteration];
    init_random_values(random_offsets);
    unsigned int offsets_record[iteration];
    double malloc_avg_time_ = 0, free_avg_time_ = 0, time = 0;
    uint64_t malloc_count_ = 0, free_count_ = 0;
    struct timeval start, end;
    int malloc_record[100000] = {0};
    int free_record[100000] = {0};
    mralloc::mr_rdma_addr remote_addr[iteration];
    uint64_t current_index = 0;
    int rand_iter = iteration;
    struct timeval total_start, total_end;
    gettimeofday(&total_start, NULL);
    for(int j = 0; j < epoch; j ++) {
        pthread_barrier_wait(&start_barrier);
        int allocated = 0;

        if(j > 0) {
            unsigned int next_idx ;
            for(int i = 0; i < free_num; i ++){
                next_idx = offsets_record[i];
                if(remote_addr[next_idx].addr != -1 && remote_addr[next_idx].rkey != -1){
                    printf("alloc false\n");
                    continue;
                }
                gettimeofday(&start, NULL);
                remote_addr[next_idx].size = size_class;
                // remote_addr[next_idx].size = rand_val()%10;
                if(!alloc->malloc(remote_addr[next_idx]) || remote_addr[next_idx].addr == 0 || remote_addr[next_idx].addr == -1){
                    printf("alloc false\n");
                }
                gettimeofday(&end, NULL);
                time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
                time_record[i] = time;
                malloc_avg_time_ = (malloc_avg_time_*malloc_count_ + time)/(malloc_count_ + 1);
                if(time >= 100000) time = 99999;
                if(time < 1) time  = 1;
                malloc_record[(int)(time)] += 1;
                malloc_count_ += 1;
                allocated ++;
                allocate_size.fetch_add(1024*1024*4);
                // printf("%lx, %u\n", remote_addr[next_idx].addr, 1<<remote_addr[next_idx].size);
            }
            // for(int i = 0; i < rand_iter; i ++){
            //     char buffer[2][16] = {"aaa", "bbb"};
            //     char read_buffer[4];
            //     conn->remote_write(buffer[i%2], 64, remote_addr[i].addr, remote_addr[i].rkey);
            //     conn->remote_read(read_buffer, 4, remote_addr[i].addr, remote_addr[i].rkey);
            //     printf("access addr %p: %s\n", remote_addr[i].addr, read_buffer);
            //     assert(read_buffer[0] == buffer[i%2][0]);
            // } 
        } else {
            for(int i = 0; i < rand_iter; i ++){
                if(remote_addr[i].addr != -1 && remote_addr[i].rkey != -1) 
                    continue;
                gettimeofday(&start, NULL);
                remote_addr[i].size = size_class;
                if(!alloc->malloc(remote_addr[i]) || remote_addr[i].addr == 0 || remote_addr[i].addr == -1){
                    printf("alloc false\n");
                }
                gettimeofday(&end, NULL);
                time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
                time_record[i] = time;
                malloc_avg_time_ = (malloc_avg_time_*malloc_count_ + time)/(malloc_count_ + 1);
                if(time >= 100000) time = 99999;
                if(time < 1) time  = 1;
                malloc_record[(int)(time)] += 1;
                malloc_count_ += 1;
                allocated ++;
                allocate_size.fetch_add(1024*1024*4);
            }   
        }
        // printf("malloc %d\n",allocated);

        // if (thread_id == 1){
        //     uint64_t mem_used;
        //     conn->remote_print_alloc_info(mem_used);
        //     printf("%lf, %lu, %lu\n", 1.0*allocate_size.load()/mem_used, allocate_size.load(), mem_used);
        // }
            // conn->remote_print_alloc_info();
            
        //valid check
        // char buffer[2][16] = {"aaa", "bbb"};
        // uint32_t rkey_write_buffer;
        // uint32_t rkey_read_buffer;
        // char read_buffer[4];
        // for(int i = 0; i < rand_iter; i ++){
        // //     // conn->remote_write(buffer[i%2], 64, remote_addr[i].addr, remote_addr[i].rkey);
        // //     // conn->remote_read(read_buffer, 4, remote_addr[i].addr, remote_addr[i].rkey);
        // //     // assert(read_buffer[0] == buffer[i%2][0]);
        //     rkey_write_buffer = remote_addr[i].rkey;
        //     conn->remote_write(&rkey_write_buffer, sizeof(rkey_write_buffer), remote_addr[i].addr, remote_addr[i].rkey);
        //     conn->remote_read(&rkey_read_buffer, sizeof(rkey_read_buffer), remote_addr[i].addr, remote_addr[i].rkey);
        //     printf("%lu, %u\n", remote_addr[i].addr, rkey_read_buffer);
        //     assert(rkey_read_buffer == rkey_write_buffer);
        // }        
       // printf("thread %d, epoch %d, malloc time %lf\n", thread_id, j, malloc_avg_time_);
        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // free
        pthread_barrier_wait(&end_barrier);
        random_values(random_offsets, rand_val);
        gettimeofday(&start, NULL);
        int result;        
        unsigned int offset_state = 0;
        unsigned int next_idx ;
        allocated = 0;
        // if(thread_id % 2 == 0){
            for(int i = 0; i < free_num; i ++){
                next_idx = get_random_offset(random_offsets, &offset_state);
                offsets_record[i] = next_idx;
                if(remote_addr[next_idx].addr != -1){
                    if(!alloc->free(remote_addr[next_idx]))
                        printf("free error!\n");
                    remote_addr[next_idx].addr = -1;
                    remote_addr[next_idx].rkey = -1;
                    allocated++;
                    allocate_size.fetch_add(-1024*1024*4);
                }
            }
            // printf("free %d\n",allocated);
        // if (thread_id == 1){
        // getchar();
        //     uint64_t mem_used;
        //     conn->remote_print_alloc_info(mem_used);
        //     printf("%lf, %lu, %lu\n", 1.0*allocate_size.load()/mem_used, allocate_size.load(), mem_used);
        // getchar();
        // }
        // } 
        // else {
        //     for(int i = 0; i < free_num; i ++){
        //         if(remote_addr[i].addr != -1){
        //             if(!alloc->free(remote_addr[i]))
        //                 printf("free error!\n");
        //             remote_addr[i].addr = -1;
        //             remote_addr[i].rkey = -1;
        //             allocated++;
        //         }
        //     }
        // }
        gettimeofday(&end, NULL);
        time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        time = time / free_num;
        if(time < 100000)
            free_record[(int)time] += 1;
        free_avg_time_ = (free_avg_time_*free_count_ + time)/(free_count_ + 1);
        free_count_ += 1;
        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // if (thread_id == 1)
        //     conn->remote_print_alloc_info();
        if(j > 0){
            pthread_mutex_lock(&file_lock);
            for(int i = 0; i < free_num; i++){
                result_detail << time_record[i] << std::endl;
            }
            pthread_mutex_unlock(&file_lock);
        }

    }
    gettimeofday(&total_end, NULL);
    printf("%d\n", total_end.tv_usec + total_end.tv_sec*1000*1000 - total_start.tv_usec - total_start.tv_sec*1000*1000);
    for(int i=0;i<100000;i++){
        malloc_record_global[i].fetch_add(malloc_record[i]);
        free_record_global[i].fetch_add(free_record[i]);
    }
    for(int i = 0; i < rand_iter; i++) {
    	if(remote_addr[i].addr!=-1)
		    alloc->free(remote_addr[i]);
    }
    alloc->print_state();
    malloc_avg[thread_id] = malloc_avg_time_;
    cas_avg[thread_id] = alloc->get_avg_retry();
    cas_max[thread_id] = alloc->get_max_retry();
    free_avg.fetch_add(free_avg_time_);
}

void short_alloc(mralloc::ConnectionManager* conn, test_allocator* alloc, uint64_t thread_id) {
    uint64_t mem_use;
    uint64_t malloc_avg_time_ = 0, free_avg_time_ = 0;
    uint64_t malloc_count_ = 0, free_count_ = 0;
    struct timeval start, end;
    int malloc_record[100000] = {0};
    int free_record[100000] = {0};
    mralloc::mr_rdma_addr remote_addr;
    uint64_t current_index = 0;
    int rand_iter = iteration;
    for(int j = 0; j < epoch; j ++) {
        pthread_barrier_wait(&start_barrier);
        gettimeofday(&start, NULL);
        for(int i = 0; i < rand_iter; i ++){
            if(!alloc->malloc(remote_addr)|| remote_addr.addr == 0){
                printf("alloc false\n");
            }
            if(!alloc->free(remote_addr)){
                printf("alloc false\n");
            }
        }
        gettimeofday(&end, NULL);
        pthread_barrier_wait(&end_barrier);
        printf("epoch %d malloc finish\n", j);
        
        if (thread_id == 1)
            conn->remote_print_alloc_info(mem_use);
            
        uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        time = time / rand_iter;
        if(time < 100000)
            malloc_record[(int)time] += 1;
        malloc_avg_time_ = (malloc_avg_time_*malloc_count_ + time)/(malloc_count_ + 1);
        malloc_count_ += 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        
    }
    for(int i=0;i<100000;i++){
        malloc_record_global[i].fetch_add(malloc_record[i]);
    }
    alloc->print_state();
    cas_avg[thread_id] = alloc->get_avg_retry();
    cas_max[thread_id] = alloc->get_max_retry(); 
    malloc_avg[thread_id] = malloc_avg_time_;
}

redisContext *redis_conn;
redisReply *redis_reply;
    

void* worker(void* arg) {
    uint64_t thread_id = id.fetch_add(1);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int id_ = thread_id;
    CPU_SET(id_, &cpuset);
    pthread_t this_tid = pthread_self();
    uint64_t ret = pthread_setaffinity_np(this_tid, sizeof(cpuset), &cpuset);
    // assert(ret == 0);
    ret = pthread_getaffinity_np(this_tid, sizeof(cpuset), &cpuset);
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i ++) {
        if (CPU_ISSET(i, &cpuset)) {
            printf("client %d main process running on core: %d\n",id_ , i);
        }
    }
    mralloc::ConnectionManager* conn = (mralloc::ConnectionManager*)arg;
    test_allocator* alloc;
    switch (type)
    {
    // following types: One-sided(CXL-SHM), RPC-user, RPC-kernel
    case cxl_shm_alloc:
        alloc = (test_allocator*)new cxl_shm_allocator(conn, 0);
        // alloc = (test_allocator*)new cxl_shm_allocator(conn, rand());
        break;
    case fusee_alloc:
        alloc = (test_allocator*)new fusee_allocator(conn);
        break;
    case rpc_alloc:
        alloc = (test_allocator*)new rpc_allocator(conn);
        break;
    case share_alloc:
        alloc = (test_allocator*)new share_allocator(conn, 0);
        // alloc = (test_allocator*)new share_allocator(conn, rand());
        break;
    case pool_alloc:
        alloc = (test_allocator*)new pool_allocator();
        break;
    default:
        break;
    }
    pthread_barrier_wait(&start_barrier);
    warmup(alloc);
    pthread_barrier_wait(&end_barrier);
    int node_id;
    if(thread_id == 1) {
    	// getchar();
        redis_reply = (redisReply*)redisCommand(redis_conn, "DECR stage2");
        printf("INCUR: %d\n", redis_reply->integer);
        // freeReplyObject(redis_reply);
        // redis_reply = (redisReply*)redisCommand(redis_conn, "GET bench_start");
        node_id = redis_reply->integer;
        if(redis_reply->integer != 0){
            redis_reply = (redisReply*)redisCommand(redis_conn, "GET stage2");    
            while(atoi(redis_reply->str) != 0){
                freeReplyObject(redis_reply);
                redis_reply = (redisReply*)redisCommand(redis_conn, "GET stage2");    
                printf("GET: %s\n", redis_reply->str);
            }
        }
        freeReplyObject(redis_reply);
    }
    pthread_barrier_wait(&start_barrier);
    // shuffle_alloc(conn, alloc, thread_id);
    switch (test)
    {
    case stage_test:
        stage_alloc(conn, alloc, thread_id);
        break;
    case shuffle_test:
        shuffle_alloc(conn, alloc, thread_id);
        break;
    case short_test:
        short_alloc(conn, alloc, thread_id);
        break;
    case frag_test:
        frag_alloc(conn, alloc, thread_id);
        break;
    default:
        break;
    }
    pthread_barrier_wait(&end_barrier);
    // if(thread_id == 1) {
    //     redis_reply = (redisReply*)redisCommand(redis_conn, "SET bench_start 0");
    //     freeReplyObject(redis_reply);
    // }
    return NULL;
}

int main(int argc, char* argv[]) {
    allocate_size.store(0);
    if(argc < 7){
        printf("Usage: %s <ip> <port> <thread> <size> <allocator> <node_num>\n", argv[0]);
        return 0;
    }

    // init_random_values(random_offsets);
    std::string ip = argv[1];
    std::string port = argv[2];
    int thread_num = atoi(argv[3]);
    size_class = atoi(argv[4]);
    std::string allocator_type = argv[5];
    std::string trace_type = "frag";
    node_num = atoi(argv[6]);

    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    redis_conn = redisConnectWithTimeout(ip.c_str(), 2222, timeout);
    // redis_reply = (redisReply*)redisCommand(redis_conn,"SET bench_start 0");
    // freeReplyObject(redis_reply);
    if (redis_conn == NULL || redis_conn->err) {
        if (redis_conn) {
            printf("Connection error: %s\n", redis_conn->errstr);
            redisFree(redis_conn);
        } else {
            printf("Connection error: can't allocate redis context\n");
        }
        exit(1);
    }

    if (allocator_type == "cxl")
        type = cxl_shm_alloc;
    else if (allocator_type == "fusee") 
        type = fusee_alloc;
    else if (allocator_type == "rpc")
        type = rpc_alloc;
    else if (allocator_type == "share")
        type = share_alloc;
    else if (allocator_type == "exclusive")
        type = exclusive_alloc;
    else if (allocator_type == "pool")
        type = pool_alloc;
    else if (allocator_type == "bitmap")
        type = bitmap_alloc;
    else if (allocator_type == "cache")
        type = cache_alloc;
    else if (allocator_type == "thread")
        type = cache_thread;
    else {
        printf("allocator type error\n");
        return -1;
    }

    if (trace_type == "stage")
        test = stage_test;
    else if (trace_type == "shuffle")
        test = shuffle_test;
    else if (trace_type == "short")
        test = short_test;
    else if (trace_type == "frag")
        test = frag_test;
    else {
        printf("test type error\n");
        return -1;
    }

    std::ofstream result;
    time_t t; unsigned int ti = time(&t);
    result.open("result_" + std::string(argv[3]) + "_" +allocator_type + "_"  + trace_type + "_" + std::to_string(ti) + "_.csv");
    result_detail.open("detail_result_" + std::string(argv[3]) + "_" +allocator_type + "_"  + trace_type + "_" + std::to_string(ti) +"_.csv");
    for(int i=0;i<100000;i++){
        malloc_record_global[i].store(0);
        free_record_global[i].store(0);
    }
    id.store(0);
    for(int i = 0; i < 128; i++)
        malloc_avg[i] = 0;
    free_avg.store(0);
    pthread_mutex_init(&file_lock, NULL);
    pthread_barrier_init(&start_barrier, NULL, thread_num);
    pthread_barrier_init(&end_barrier, NULL, thread_num);
    pthread_t running_thread[thread_num];
    mralloc::ConnectionManager* conn[thread_num];
    for(int i = 0; i < thread_num; i++) {
        conn[i] = new mralloc::ConnectionManager();
        conn[i]->init(ip, port, 1, 1, 1);
        pthread_create(&running_thread[i], NULL, worker, conn[i]);
    }
    mralloc::ConnectionManager* listen_conn = new mralloc::ConnectionManager();
    listen_conn->init(ip, port, 1, 1, 1 );
    pthread_t listen_thread;
    // pthread_create(&listen_thread, NULL, run_woker_thread, listen_conn);
    for(int i = 0; i < thread_num; i++) {
        pthread_join(running_thread[i], NULL);
    }
    result << "malloc " << std::endl;
    int p99_num = ((free_num)*(epoch-1) + iteration ) *thread_num*0.99;
    int count = 0;
    for(int i=0;i<100000;i++) {
        if(malloc_record_global[i].load() != 0) {
            if(count < p99_num){
                count += malloc_record_global[i].load();
                if(count >= p99_num) {
                    printf("%f\n", i-((float)count-p99_num)/malloc_record_global[i].load());
                }
            }
            result << i << " " <<malloc_record_global[i].load() << std::endl;
	    }
    }
    result << "free " << std::endl;
    for(int i=0;i<100000;i++) {
        if(free_record_global[i].load() != 0)
            result << i << " " <<free_record_global[i].load() << std::endl;
    }
    volatile double malloc_avg_final = 0;
    for(int i = 0; i < 128; i++) {
        malloc_avg_final += malloc_avg[i];
    }
    volatile double cas_avg_final = 0;
    for(int i = 0; i < 128; i++) {
        cas_avg_final += cas_avg[i];
    }
    volatile int cas_max_final = 0;
    for(int i = 0; i < 128; i++) {
        if(cas_max[i] > cas_max_final)
            cas_max_final = cas_max[i];
    }
    printf("%lf\n", malloc_avg_final/thread_num);
    // printf("total malloc avg: %lfus\n", malloc_avg_final/thread_num);
    result << "total malloc avg: " << malloc_avg_final/thread_num << std::endl;
    // printf("total free avg: %luus\n", free_avg.load()/thread_num);
    result << "total free avg: " << free_avg.load()/thread_num << std::endl;
    // printf("total cas avg: %lf\n", cas_avg_final/thread_num);
    result << "total cas avg: " << cas_avg_final/thread_num << std::endl;
    // printf("max cas : %d\n", cas_max_final);
    result << "max cas :" << cas_max_final << std::endl;
    result.close();
    result_detail.close();
    redis_reply = (redisReply*)redisCommand(redis_conn, "INCRBYFLOAT avg_lat %s", std::to_string(malloc_avg_final/thread_num).c_str());
    printf("INCUR: %s\n", redis_reply->str);
    freeReplyObject(redis_reply);
    redis_reply = (redisReply*)redisCommand(redis_conn, "INCR finished");
    freeReplyObject(redis_reply);
    redis_reply = (redisReply*)redisCommand(redis_conn, "SET bench_start 0");   
    freeReplyObject(redis_reply);
}
