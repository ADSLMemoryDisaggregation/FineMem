
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "free_block_manager.h"
#include "memory_node.h"
#include "msg.h"
#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <netinet/in.h>
#include <rdma/rdma_cma.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/mman.h>
#include <unistd.h>

#define MEM_ALIGN_SIZE 4096

const bool use_reg = false;
const bool use_1GB = false;
const bool use_40GB = false;
const bool use_hugepage = true;

#define POOL_MEM_SIZE (uint64_t)1024*1024*1024

// #define INIT_MEM_SIZE ((uint64_t)10*1024*1024*1024)

// #define SERVER_BASE_ADDR (uint64_t)0xfe00000

// #define SERVER_BASE_ADDR (uint64_t)0x1000000

namespace mralloc {

const uint64_t base_block_size = (uint64_t)1024*4*1024;

const bool global_rkey = true;

const uint64_t SERVER_BASE_ADDR = (uint64_t)0x10000000;

void * run_mw_generator(ibv_mw** block_mw, ibv_mw** backup_mw, int start, int end, ibv_pd* pd) {
    for(int i = start; i < end; i++){
        // uint64_t block_addr_ = server_block_manager_->get_block_addr(i);
        block_mw[i] = ibv_alloc_mw(pd, IBV_MW_TYPE_1);
        backup_mw[i] = ibv_alloc_mw(pd, IBV_MW_TYPE_1);
        if((i-start) % 10240 == 0){
            printf("%d MB\n", i / 1024);
        } 
    }
};

void * run_rebinder(void* arg) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_t this_tid = pthread_self();
    uint64_t ret = pthread_setaffinity_np(this_tid, sizeof(cpuset), &cpuset);
    ret = pthread_getaffinity_np(this_tid, sizeof(cpuset), &cpuset);
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i ++) {
        if (CPU_ISSET(i, &cpuset)) {
            printf("filler running on core: %d\n" , i);
        }
    }
    MemoryNode *heap = (MemoryNode*)arg;
    heap->rebinder();
    return NULL;
} 

uint64_t MemoryNode::print_alloc_info() {
  return server_block_manager_->print_section_info(ring_cache->get_length(), reg_size_.load());
}

/**
 * @description: start remote engine service
 * @param {string} addr   empty string for MemoryNode as server
 * @param {string} port   the port the server listened
 * @return {bool} true for success
 */
bool MemoryNode::start(const std::string addr, const std::string port, const std::string device) {

    m_stop_ = false;

    m_worker_info_ = new volatile WorkerInfo *[MAX_SERVER_WORKER*MAX_SERVER_CLIENT];
    m_worker_threads_ = new std::thread *[MAX_SERVER_WORKER];
    for (uint32_t i = 0; i < MAX_SERVER_WORKER; i++) {
      m_worker_info_[i] = nullptr;
      m_worker_threads_[i] = nullptr;
    }
    m_worker_num_ = 0;

    // get rdma device, alloc protect domain and init memory heap

    struct ibv_context **ibv_ctxs;
    int nr_devices_;
    ibv_ctxs = rdma_get_devices(&nr_devices_);
    if (!ibv_ctxs) {
      perror("get device list fail");
      return false;
    }
    for(int i = 0; i< nr_devices_; i++) {
        if(device.compare(ibv_ctxs[i]->device->name) == 0){
            m_context_ = ibv_ctxs[i];
            break;
        }
    }
    m_pd_ = ibv_alloc_pd(m_context_);
    if (!m_pd_) {
      perror("ibv_alloc_pd fail");
      return false;
    }

    struct ibv_device_attr device_attr;
    struct ibv_device_attr_ex device_attr_ex;
    struct ibv_query_device_ex_input input;
    ibv_query_device(m_context_, & device_attr);
    if (!(device_attr.device_cap_flags & IBV_DEVICE_MEM_WINDOW)) {
        printf("do not support memory window\n");
    } else {
        printf("support %d memory window and %d qp total\n", device_attr.max_mw, device_attr.max_qp);
    }
    ibv_query_device_ex(m_context_, NULL, &device_attr_ex);
    if(!(device_attr_ex.odp_caps.general_caps & IBV_ODP_SUPPORT)){
        printf("Not support odp!\n");
    }
    mw_queue_ = new MWPool(m_pd_);

    // create connection manager and listen

    m_cm_channel_ = rdma_create_event_channel();
    if (!m_cm_channel_) {
      perror("rdma_create_event_channel fail");
      return false;
    }

    if (rdma_create_id(m_cm_channel_, &m_listen_id_, NULL, RDMA_PS_TCP)) {
      perror("rdma_create_id fail");
      return false;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(stoi(port));
    sin.sin_addr.s_addr = INADDR_ANY;

    if (rdma_bind_addr(m_listen_id_, (struct sockaddr *)&sin)) {
      perror("rdma_bind_addr fail");
      return false;
    }

    if (rdma_listen(m_listen_id_, 2048)) {
      perror("rdma_listen fail");
      return false;
    }

    if(!init_memory_heap(INIT_MEM_SIZE)) {
      perror("init memory heap fail");
      return false;
    }

    m_conn_handler_ = new std::thread(&MemoryNode::handle_connection, this);

    return true;

}

void MemoryNode::rebinder() {
    uint64_t block_num = server_block_manager_->get_block_num();
    ibv_mw* swap;
    while(1) {
        // struct timeval start, end;
        // gettimeofday(&start, NULL);
        // int counter = 0;
        for(int i = 0; i < block_num; i ++) {
            if(server_block_manager_->get_backup_rkey(i)== (uint32_t)-1){
                if(use_global_rkey){
                    server_block_manager_->set_backup_rkey(i, get_global_rkey());
                } else {
                    bind_mw(block_mw[i], 0, server_block_manager_->get_block_size(), rebinder_qp, rebinder_cq);
                    bind_mw(block_mw[i], server_block_manager_->get_block_addr(i), server_block_manager_->get_block_size(), rebinder_qp, rebinder_cq);
                    server_block_manager_->set_backup_rkey(i, block_mw[i]->rkey);
                    swap = block_mw[i]; block_mw[i] = backup_mw[i]; backup_mw[i] = swap;
                }
                // counter++;
            }
        }
        // gettimeofday(&end, NULL);
        // uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        // if(counter > 0)
        //     printf("mw_bind cost on %d is %lu\n", counter,time);
    }
}

bool MemoryNode::new_cache_section(){
    alloc_advise advise = alloc_light;
    if(!server_block_manager_->find_section(current_section_, current_section_index_, 0, advise) ) {
        printf("cannot find avaliable section\n");
        return false;
    }
    return true;
}

bool MemoryNode::new_cache_region() {
    while(!server_block_manager_->fetch_region(current_section_, current_section_index_, 0, true, current_region_, current_region_index_, 0) ) {
        if(!new_cache_section())
            return false;
    }
    return true;
}

bool MemoryNode::fill_cache_block(){
    uint32_t length = ring_cache->get_length();
    for(int i = 0; i<simple_cache_watermark - length; i++){
        mr_rdma_addr addr;
        while(!server_block_manager_->fetch_region_block(current_section_, current_region_, addr.addr, addr.rkey, false, current_region_index_, 0)) {
            // fetch new region
            new_cache_region();
        }
        ring_cache->add_cache(addr);
    }
    return true;
}

bool MemoryNode::fetch_mem_block(uint64_t &addr, uint32_t &rkey){
    mr_rdma_addr result;
    std::unique_lock<std::mutex> lock(m_mutex_2);
    uint64_t alloc_addr=0; uint32_t alloc_rkey;
    mralloc::mr_rdma_addr new_addr; 
    if(!free_queue_manager_-> fetch_block(new_addr)){
        // int old_val = 1; int new_val = 0; 
        // if(!cas_lock.compare_exchange_weak(old_val, new_val)){
        //     while(!queue->fetch_block(new_addr)){
        //         *addr = new_addr.addr;
        //         *rkey = new_addr.rkey;
        //         *node = new_addr.node;
        //         return;
        //     }
        // }
        allocate_and_register_memory(alloc_addr, alloc_rkey, (uint64_t)1024*1024*1024);
        free_queue_manager_->fill_block({alloc_addr, alloc_rkey, 0}, (uint64_t)1024*1024*1024);
        free_queue_manager_->fetch_block(new_addr);
        // new_val = 1; old_val = 0;
        // cas_lock.compare_exchange_weak(old_val, new_val);
    }
    addr = new_addr.addr;
    rkey = new_addr.rkey;
    // // *node = new_addr.node;
    // if(ring_cache->try_fetch_cache(result)){
    //     addr = result.addr; 
    //     rkey = result.rkey;
    //     return true;
    // }
    // if(fill_cache_block()){
    //     return fetch_mem_block(addr, rkey);
    // }
    return true;
}

bool MemoryNode::free_mem_block(uint64_t addr) {
    std::unique_lock<std::mutex> lock(m_mutex_2);
    bool freed;
    mralloc::mr_rdma_addr new_addr; 
    // queue->return_block_no_free({(uint64_t)addr,0,node}, freed);
    free_queue_manager_->return_block_no_free({(uint64_t)addr,mr_recorder[addr-addr%((uint64_t)1024*1024*1024)]->rkey,0}, freed);
    if(freed){
        deallocate_and_unregister_memory((uint64_t)addr-(uint64_t)addr%((uint64_t)1024*1024*1024));
    }
    
    // mr_rdma_addr new_addr;
    // uint32_t block_id = (addr - server_block_manager_->get_heap_start())/ server_block_manager_->get_block_size();
    // new_addr.addr = addr; 
    // new_addr.rkey = block_mw[block_id]->rkey;
    // ring_cache->add_cache(new_addr);
    return true;
}

/**
 * @description: init memory heap, malloc a huge memory region and register it, then init free queue manager
 * @param {uint64_t} size: memory heap size
 * @return {bool} true for success
 */
bool MemoryNode::init_memory_heap(uint64_t size) {
    uint64_t init_addr_raw = server_base_addr;
    uint64_t init_size_raw = size;
    // Fusee need external memory, at a fixed start addr
    if(fusee_enable){
        uint64_t fusee_addr, fusee_size; uint32_t fusee_rkey;
        fusee_size = round_up(META_AREA_LEN + HASH_AREA_LEN + GC_AREA_LEN, REMOTE_MEM_SIZE);
        fusee_addr = (uint64_t)mmap((void*)server_base_addr, fusee_size, PROT_READ | PROT_WRITE, 
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_HUGETLB, -1, 0);
        ibv_mr* fusee_mr = rdma_register_memory((void*)fusee_addr, fusee_size);
        init_addr_raw += fusee_size;
    }
    server_block_manager_ = new ServerBlockManager(REMOTE_MEM_SIZE);
    uint64_t init_addr_, init_size_;
    server_block_manager_->init_align_hint(init_addr_raw, init_size_raw, init_addr_, init_size_);
    void* init_addr = NULL;
    if(use_hugepage)
        init_addr = mmap((void*)init_addr_ , init_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_HUGETLB, -1, 0);
    else
        init_addr = mmap((void*)init_addr_ , init_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    printf("init_addr: %p, heap start: %lx\n", init_addr, init_addr_raw);
    if (init_addr == MAP_FAILED || (uint64_t)init_addr != init_addr_) {
        perror("mmap fail");
        return false;
    }
    // assert(size == init_size_ - SERVER_BASE_ADDR + init_addr_); 
    heap_total_size_ = init_size_raw; heap_start_addr_ = init_addr_raw;
    memkind_create_fixed((void*)heap_start_addr_, heap_total_size_, &memkind_);
    // ibv_mr* heap_mr_ = rdma_register_memory((void*)server_base_addr, init_addr_raw - server_base_addr);
    ibv_mr* heap_mr_ = rdma_register_memory((void*)server_base_addr, init_addr_ - server_base_addr + init_size_);
    global_mr_ = heap_mr_;
    // global_mr_ =
    //     ibv_reg_mr(m_pd_, (void*)init_addr_raw, init_size_raw,
    //                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
    //                     IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_MW_BIND);
    if(fusee_enable)
        rpc_fusee_ = new RPC_Fusee(server_base_addr, server_base_addr + META_AREA_LEN, heap_mr_->rkey);
    set_global_rkey(heap_mr_->rkey);
    heap_pointer_.store((init_addr_raw + 1024*1024*1024 - 1) - (init_addr_raw + 1024*1024*1024 - 1) % (1024*1024*1024));
    // heap_pointer_.store(init_addr_raw + init_size_raw + 1024*1024*1024 - 1 - (init_addr_raw + init_size_raw + 1024*1024*1024 - 1) % (1024*1024*1024));
    m_mw_handler = (ibv_mw**)malloc(size / base_block_size * sizeof(ibv_mw*));

    mw_binded = false;

    if(!server_block_manager_->init((uint64_t)init_addr, init_addr_raw, init_size_raw, heap_mr_->rkey)) {
        perror("init free queue manager fail"); 
        return false;
    }

    
    bool ret;

    ret = new_cache_section();

    ret &= new_cache_region();

    simple_cache_watermark = 36;

    free_queue_manager_ = new FreeQueueManager(REMOTE_MEM_SIZE, POOL_MEM_SIZE);
    mr_rdma_addr addr;
    addr.node = 0;
    if(use_1GB || use_40GB || use_reg){
        allocate_and_register_memory(addr.addr, addr.rkey, POOL_MEM_SIZE);
        free_queue_manager_->init(addr, POOL_MEM_SIZE);
    }
    if(use_40GB){
        for(int i = 0; i < 79;i ++){
            allocate_and_register_memory(addr.addr, addr.rkey, POOL_MEM_SIZE);
            free_queue_manager_->fill_block(addr, POOL_MEM_SIZE);
        }
    }
    if(!ret) {
        printf("init cache failed\n");
        return false;
    }
    uint64_t block_num_ = server_block_manager_->get_block_num() ;

    block_mw = (ibv_mw**)malloc(block_num_ * sizeof(ibv_mw*));

    backup_mw = (ibv_mw**)malloc(block_num_ * sizeof(ibv_mw*));
    if(!use_global_rkey){


        std::thread* mw_thread[accelerate_thread];
        struct timeval start, end;
        gettimeofday(&start, NULL);
        uint64_t interval = block_num_ / accelerate_thread;
        for(int i = 0; i < accelerate_thread; i++){
            mw_thread[i] = new std::thread(&run_mw_generator, block_mw, backup_mw, i*interval, (i+1)*interval, m_pd_);
        }
        for(int i = 0; i < accelerate_thread; i++){
            mw_thread[i]->join();
        }

        gettimeofday(&end, NULL);
        uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        printf("mw_reg bost on %d is %lu: %lf\n", block_num_, time, 1.0*time/block_num_);
    }
    return true;
}

/**
 * @description: get engine alive state
 * @return {bool}  true for alive
 */
bool MemoryNode::alive() {  
    return true;
}

/**
 * @description: stop local engine service
 * @return {void}
 */
void MemoryNode::stop() {
    m_stop_ = true;
    if (m_conn_handler_ != nullptr) {
        m_conn_handler_->join();
        delete m_conn_handler_;
        m_conn_handler_ = nullptr;
    }
    for (uint32_t i = 0; i < MAX_SERVER_WORKER; i++) {
        if (m_worker_threads_[i] != nullptr) {
        m_worker_threads_[i]->join();
        delete m_worker_threads_[i];
        m_worker_threads_[i] = nullptr;
        }
    }
    munmap((void*)SERVER_BASE_ADDR, INIT_MEM_SIZE);
    delete server_block_manager_;
}

void MemoryNode::handle_connection() {
    printf("start handle_connection\n");
    struct rdma_cm_event *event;
    while (true) {
        if (m_stop_) break;
        if (rdma_get_cm_event(m_cm_channel_, &event)) {
            perror("rdma_get_cm_event fail");
            return;
        }
        
        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            struct rdma_cm_id *cm_id = event->id;
            CNodeInit msg = *(CNodeInit*)event->param.conn.private_data;
            rdma_ack_cm_event(event);
            create_connection(cm_id, msg.access_type, msg.node_id);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            rdma_ack_cm_event(event);
        } else {
            rdma_ack_cm_event(event);
        }
    }
    printf("exit handle_connection\n");
}

int MemoryNode::create_connection(struct rdma_cm_id *cm_id, uint8_t connect_type, uint16_t node_id) {

    if (!m_pd_) {
        perror("ibv_pibv_alloc_pdoll_cq fail");
        return -1;
    }

    struct ibv_comp_channel *comp_chan = ibv_create_comp_channel(m_context_);
    if (!comp_chan) {
        perror("ibv_create_comp_channel fail");
        return -1;
    }

    struct ibv_cq *cq = ibv_create_cq(m_context_, 1, NULL, comp_chan, 0);
    if (!cq) {
        perror("ibv_create_cq fail");
        return -1;
    }

    if (ibv_req_notify_cq(cq, 0)) {
        perror("ibv_req_notify_cq fail");
        return -1;
    }

    struct ibv_qp_init_attr qp_attr = {};
    qp_attr.cap.max_send_wr = 1;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_wr = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.cap.max_inline_data = 256;
    qp_attr.sq_sig_all = 0;

    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;

    if (rdma_create_qp(cm_id, m_pd_, &qp_attr)) {
        perror("rdma_create_qp fail");
        return -1;
    }
    
    struct PData rep_pdata;
    CmdMsgBlock *cmd_msg = nullptr;
    CmdMsgRespBlock *cmd_resp = nullptr;
    struct ibv_mr *msg_mr = nullptr;
    struct ibv_mr *resp_mr = nullptr;
    cmd_msg = new CmdMsgBlock();
    memset(cmd_msg, 0, sizeof(CmdMsgBlock));
    msg_mr = rdma_register_memory((void *)cmd_msg, sizeof(CmdMsgBlock));
    if (!msg_mr) {
        perror("ibv_reg_mr cmd_msg fail");
        return -1;
    }

    cmd_resp = new CmdMsgRespBlock();
    memset(cmd_resp, 0, sizeof(CmdMsgRespBlock));
    resp_mr = rdma_register_memory((void *)cmd_resp, sizeof(CmdMsgRespBlock));
    if (!msg_mr) {
        perror("ibv_reg_mr cmd_resp fail");
        return -1;
    }
    rep_pdata.id = -1;
    if(connect_type == CONN_RPC){
        int num = m_worker_num_;
        if (num < MAX_SERVER_WORKER) {
        assert(m_worker_info_[num] == nullptr);
        m_worker_info_[num] = new WorkerInfo();
        m_worker_info_[num]->cmd_msg = cmd_msg;
        m_worker_info_[num]->cmd_resp_msg = cmd_resp;
        m_worker_info_[num]->msg_mr = msg_mr;
        m_worker_info_[num]->resp_mr = resp_mr;
        m_worker_info_[num]->cm_id = cm_id;
        m_worker_info_[num]->cq = cq;

        assert(m_worker_threads_[num] == nullptr);
        m_worker_threads_[num] =
            new std::thread(&MemoryNode::worker, this, m_worker_info_[num], num);
        } else {
            assert(m_worker_info_[num] == nullptr);
            m_worker_info_[num] = new WorkerInfo();
            m_worker_info_[num]->cmd_msg = cmd_msg;
            m_worker_info_[num]->cmd_resp_msg = cmd_resp;
            m_worker_info_[num]->msg_mr = msg_mr;
            m_worker_info_[num]->resp_mr = resp_mr;
            m_worker_info_[num]->cm_id = cm_id;
            m_worker_info_[num]->cq = cq;
        } 
        
        rep_pdata.id = num;
        server_block_manager_->public_info_->id_node_map[num] = node_id;
        m_worker_num_ += 1;
        one_side_qp_[num] = cm_id->qp;
        one_side_cq_[num] = cq;
    } 
    rep_pdata.buf_addr = (uintptr_t)cmd_msg;
    rep_pdata.buf_rkey = msg_mr->rkey;
    rep_pdata.size = sizeof(CmdMsgRespBlock);

    if(one_sided_enabled_) {
        rep_pdata.block_num_ = (uint64_t)server_block_manager_->get_block_num();
        rep_pdata.block_size_ = (uint64_t)server_block_manager_->get_block_size();
        rep_pdata.section_header_ = (uint64_t)server_block_manager_->get_metadata();
        rep_pdata.heap_start_ = (uint64_t)server_block_manager_->get_heap_start();
    }

    rep_pdata.global_rkey_ = get_global_rkey();

    struct rdma_conn_param conn_param;
    conn_param.responder_resources = 16;
    conn_param.initiator_depth = 16;
    conn_param.private_data = &rep_pdata;
    conn_param.retry_count = 7;
    conn_param.rnr_retry_count = 7;
    conn_param.private_data_len = sizeof(rep_pdata);

    if (rdma_accept(cm_id, &conn_param)) {
        perror("rdma_accept fail");
        return -1;
    }
    
    if(!mw_binded) {
        init_mw(cm_id->qp, cq);
        rebinder_qp = cm_id->qp;
        rebinder_cq = cq;
        pthread_t rebind_thread;
        pthread_create(&rebind_thread, NULL, run_rebinder, this);
        mw_binded = true;
    }
    return 0;
}

bool MemoryNode::init_mw(ibv_qp *qp, ibv_cq *cq) {
    uint64_t block_num_ = server_block_manager_->get_block_num() ;

    // When use global rkey: application not support multiple rkey or evaluation the side-effect of memory window
    if(use_global_rkey){
        block_mw[0] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
        if(!block_mw[0]){
            perror("mw failed!\n");
        }
        for(int i = 0; i < block_num_; i++){
            server_block_manager_->set_block_rkey(i, get_global_rkey());
            server_block_manager_->set_backup_rkey(i, get_global_rkey());
        }
    } else {
        // for(int i = 0; i < block_num_; i++){
        //     uint64_t block_addr_ = server_block_manager_->get_block_addr(i);
        //     block_mw[i] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
        //     backup_mw[i] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
        // }
        // struct timeval start, end;
        // // for(int i = 0; i < block_num_; i++){
        // //     uint64_t block_addr_ = server_block_manager_->get_block_addr(i);
        // //     // bind_mw(block_mw[i], 0, server_block_manager_->get_block_size(), qp, cq);
        // //     bind_mw(block_mw[i], block_addr_, server_block_manager_->get_block_size(), qp, cq);
        // //     server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
        // // }
        // gettimeofday(&start, NULL);
        // for(int i = 0; i < 25600; i++){
        //     uint64_t block_addr_ = server_block_manager_->get_block_addr(0);
        //     // bind_mw(block_mw[i], 0, server_block_manager_->get_block_size(), qp, cq);
        //     bind_mw(block_mw[0], block_addr_, 1024*1024*1024*100, qp, cq);
        //     // server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
        // }
        // gettimeofday(&end, NULL);
        // uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        // printf("mw_bind cost on %d is %lu\n", block_num_, time);
        // uint64_t addr_record[25600];
        // ibv_mr* mr_record[25600];
        // gettimeofday(&start, NULL);
        // for(int i = 0; i < 1; i++){
        //     addr_record[i] = (uint64_t)mmap(NULL, (uint64_t)1024*1024*1024*100, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        //     if(addr_record[i]==0 || addr_record[i] ==-1){
        //         printf("error\n");
        //     }
        //     uint32_t rkey;
        //     // allocate_and_register_memory(addr_record[i], rkey, 1024*1024*4);
        //     mr_record[i] = rdma_register_memory((void *)addr_record[i], (uint64_t)1024*1024*1024*100);
        //     // server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
        // }
        // // for(int i = 0; i < 1; i++){
        // //     addr_record[i] = (uint64_t)mmap(NULL, (uint64_t)1024*1024*1024*100, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        // //     if(addr_record[i]==0 || addr_record[i] ==-1){
        // //         printf("error\n");
        // //     }
        // //     uint32_t rkey;
        // //     // allocate_and_register_memory(addr_record[i], rkey, 1024*1024*4);
        // //     mr_record[i] = rdma_register_memory((void *)addr_record[i], (uint64_t)1024*1024*1024*100);
        // //     // server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
        // // }
        // // for(int i = 0; i < 25600; i++){
        // //     // deallocate_and_unregister_memory((addr_record[i]));
        // //     ibv_dereg_mr(mr_record[i]);
        // //     munmap((void*)addr_record[i], 1024*1024*4);
        // //     // mr_record[i] = rdma_register_memory((void *)addr_record[i], 1024*1024*4);
        // //     // server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
        // // }
        // gettimeofday(&end, NULL);
        // time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        // printf("mw_reg bost on %d is %lu\n", block_num_, time);
        // for(int i = 0; i < block_num_; i++){
        //     uint64_t block_addr_ = server_block_manager_->get_block_addr(i);
        //     bind_mw(backup_mw[i], block_addr_, server_block_manager_->get_block_size(), qp, cq);
        //     server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
        // }

        struct timeval start, end;
        gettimeofday(&start, NULL);
        for(int i = 0; i < block_num_; i++){
            uint64_t block_addr_ = server_block_manager_->get_block_addr(i);
            // block_mw[i] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
            // backup_mw[i] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
            bind_mw(block_mw[i], block_addr_, server_block_manager_->get_block_size(), qp, cq);
            bind_mw(backup_mw[i], block_addr_, server_block_manager_->get_block_size(), qp, cq);
            server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
            server_block_manager_->set_backup_rkey(i, backup_mw[i]->rkey); 
            if(i % 10240 == 0){
                printf("%d MB\n", i / 1024);
            } 
        }
        gettimeofday(&end, NULL);
        uint64_t time =  end.tv_usec + end.tv_sec*1000*1000 - start.tv_usec - start.tv_sec*1000*1000;
        printf("mw_reg bost on %d is %lu: %lf\n", block_num_, time, 1.0*time/block_num_);

    }

    printf("bind finished\n");

    return true;
}


bool MemoryNode::bind_mw_async(ibv_mw* mw, uint64_t addr, uint64_t id, uint64_t size, ibv_qp* qp, ibv_cq* cq){
    if(mw == NULL){
        int index = (addr - heap_start_addr_)/base_block_size;
        m_mw_handler[index] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
        printf("addr:%lx rkey_old: %u",  addr, m_mw_handler[index]->rkey);
        mw = m_mw_handler[index];
    }
    struct ibv_mw_bind_info bind_info_ = {.mr = global_mr_, 
                                            .addr = addr, 
                                            .length = size,
                                            .mw_access_flags = IBV_ACCESS_REMOTE_READ | 
                                                IBV_ACCESS_REMOTE_WRITE } ;
    struct ibv_mw_bind bind_ = {.wr_id = id, .send_flags = IBV_SEND_SIGNALED, .bind_info = bind_info_};
    if(ibv_bind_mw(qp, mw, &bind_)){
        perror("ibv_post_send mw_bind fail");
    } 
    return true;
}

bool MemoryNode::bind_mw_async_poll(ibv_cq* cq){
    while (true) {
        ibv_wc wc;
        int rc = ibv_poll_cq(cq, 1, &wc);
        if (rc == 1) {
            if (wc.opcode != IBV_WC_BIND_MW) {
                printf("bind failed\n");
            }
            if (IBV_WC_SUCCESS == wc.status) {
                int i = wc.wr_id>>1;
                if(wc.wr_id%2 == 0){
                    server_block_manager_->set_block_rkey(i, block_mw[i]->rkey);
                } else {
                    server_block_manager_->set_backup_rkey(i, backup_mw[i]->rkey);  
                }
                break;
            } else if (IBV_WC_WR_FLUSH_ERR == wc.status) {
                perror("cmd_send IBV_WC_WR_FLUSH_ERR");
                break;
            } else if (IBV_WC_RNR_RETRY_EXC_ERR == wc.status) {
                perror("cmd_send IBV_WC_RNR_RETRY_EXC_ERR");
                break;
            } else {
                perror("cmd_send ibv_poll_cq status error");
                break;
            }
        } else if (0 == rc) {
            continue;
        } else {
            perror("ibv_poll_cq fail");
            break;
        }
    }
    return true;
}

bool MemoryNode::bind_mw(ibv_mw* mw, uint64_t addr, uint64_t size, ibv_qp* qp, ibv_cq* cq){
    if(mw == NULL){
        int index = (addr - heap_start_addr_)/base_block_size;
        m_mw_handler[index] = ibv_alloc_mw(m_pd_, IBV_MW_TYPE_1);
        printf("addr:%lx rkey_old: %u",  addr, m_mw_handler[index]->rkey);
        mw = m_mw_handler[index];
    }
    struct ibv_mw_bind_info bind_info_ = {.mr = global_mr_, 
                                            .addr = addr, 
                                            .length = size,
                                            .mw_access_flags = IBV_ACCESS_REMOTE_READ | 
                                                IBV_ACCESS_REMOTE_WRITE } ;
    struct ibv_mw_bind bind_ = {.wr_id = 0, .send_flags = IBV_SEND_SIGNALED, .bind_info = bind_info_};
    if(ibv_bind_mw(qp, mw, &bind_)){
        perror("ibv_post_send mw_bind fail");
    } 
    else {
        while (true) {
            ibv_wc wc;
            int rc = ibv_poll_cq(cq, 1, &wc);
            if (rc == 1) {
                if (wc.opcode != IBV_WC_BIND_MW) {
                    printf("bind failed\n");
                }
                if (IBV_WC_SUCCESS == wc.status) {
                    break;
                } else if (IBV_WC_WR_FLUSH_ERR == wc.status) {
                    perror("cmd_send IBV_WC_WR_FLUSH_ERR");
                    break;
                } else if (IBV_WC_RNR_RETRY_EXC_ERR == wc.status) {
                    perror("cmd_send IBV_WC_RNR_RETRY_EXC_ERR");
                    break;
                } else {
                    perror("cmd_send ibv_poll_cq status error");
                    break;
                }
            } else if (0 == rc) {
                continue;
            } else {
                perror("ibv_poll_cq fail");
                break;
            }
        }
    }
    return true;
}

struct ibv_mr *MemoryNode::rdma_register_memory(void *ptr, uint64_t size) {
    struct ibv_mr *mr =
        ibv_reg_mr(m_pd_, ptr, size,
                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_MW_BIND);
    if (!mr) {
        perror("ibv_reg_mr fail");
        return nullptr;
    }
    return mr;
}
    uint64_t mr_counter = 0;

int MemoryNode::allocate_and_register_memory(uint64_t &addr, uint32_t &rkey,
                                               uint64_t size) {
    /* align mem */
    // uint64_t mem = (uint64_t)malloc(size);
    // addr = mem;
    uint64_t p;
    if(free_addr_.empty()){
        p = heap_pointer_.load();
        addr = p;
        uint64_t new_p;
        // new_p = new_p - new_p%(1024*1024*1024);
        do{
            new_p = p +size;
            // new_p = new_p - new_p%(1024*1024*1024);
        }while(!heap_pointer_.compare_exchange_weak(p, new_p));
    } else {
        p = free_addr_.front();
        free_addr_.pop();
        addr = p;
    }
    // addr = (uint64_t)mmap((void*)p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED |MAP_ANONYMOUS, -1, 0);
    // if(!use_reg)
    if(use_hugepage)
        addr = (uint64_t)mmap((void*)p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED |MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    else
        addr = (uint64_t)mmap((void*)p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED |MAP_ANONYMOUS , -1, 0);
    // printf("%lx\n", addr);
    assert(addr == p);
    struct ibv_mr *mr = rdma_register_memory((void *)p, size);
    reg_size_.fetch_add(size);
    // printf("%lx\n", addr);
    if (!mr) {
        perror("ibv_reg_mr fail");
        printf("%d\n", mr_counter);
        return -1;
    }
    mr_counter++;
    std::unique_lock<std::mutex> lock(m_mutex_);
    mr_recorder[addr] = mr;
    rkey = mr->rkey;
    return 0;
}

int MemoryNode::deallocate_and_unregister_memory(uint64_t addr) {
    std::unique_lock<std::mutex> lock(m_mutex_);
    if(mr_recorder[addr] == NULL) {
        printf("no free!\n");
        return 0;
    }
    reg_size_.fetch_sub(mr_recorder[addr]->length);
    // printf("%d\n", reg_size_.load());
    ibv_dereg_mr(mr_recorder[addr]);
    munmap((void*)addr, mr_recorder[addr]->length);
    free_addr_.push(addr);
    mr_recorder[addr]=NULL;
    // free((void*)addr);
    return 0;
}

int MemoryNode::remote_write(volatile WorkerInfo *work_info, uint64_t local_addr,
                               uint32_t lkey, uint32_t length,
                               uint64_t remote_addr, uint32_t rkey) {
    struct ibv_sge sge;
    sge.addr = (uintptr_t)local_addr;
    sge.length = length;
    sge.lkey = lkey;

    struct ibv_send_wr send_wr = {};
    struct ibv_send_wr *bad_send_wr;
    send_wr.wr_id = 0;
    send_wr.num_sge = 1;
    send_wr.next = NULL;
    send_wr.opcode = IBV_WR_RDMA_WRITE;
    send_wr.sg_list = &sge;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.wr.rdma.remote_addr = remote_addr;
    send_wr.wr.rdma.rkey = rkey;
    int error_code;
    if (error_code = ibv_post_send(work_info->cm_id->qp, &send_wr, &bad_send_wr)) {
        perror("ibv_post_send write fail");
        printf("error code %d\n", error_code);
        return -1;
    }

    auto start = TIME_NOW;
    struct ibv_wc wc;
    int ret = -1;
    while (true) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        perror("remote write timeout");
        return -1;
        }
        int rc = ibv_poll_cq(work_info->cq, 1, &wc);
        if (rc > 0) {
        if (IBV_WC_SUCCESS == wc.status) {
            ret = 0;
            break;
        } else if (IBV_WC_WR_FLUSH_ERR == wc.status) {
            perror("cmd_send IBV_WC_WR_FLUSH_ERR");
            break;
        } else if (IBV_WC_RNR_RETRY_EXC_ERR == wc.status) {
            perror("cmd_send IBV_WC_RNR_RETRY_EXC_ERR");
            break;
        } else {
            perror("cmd_send ibv_poll_cq status error");
            printf("%d\n", wc.status);
            break;
        }
        } else if (0 == rc) {
        continue;
        } else {
        perror("ibv_poll_cq fail");
        break;
        }
    }
    return ret;
}

// RPC worker

void MemoryNode::worker(volatile WorkerInfo *work_info, uint32_t num) {
    printf("start worker %d\n", num);
    CmdMsgBlock *cmd_msg = work_info->cmd_msg;
    CmdMsgRespBlock *cmd_resp = work_info->cmd_resp_msg;
    struct ibv_mr *resp_mr = work_info->resp_mr;
    cmd_resp->notify = NOTIFY_WORK;
    int active_id = -1;
    int record = num;
    while (true) {
        if (m_stop_) break;
        for (int i = record; i < m_worker_num_; i+=MAX_SERVER_WORKER) {
            if (m_worker_info_[i]->cmd_msg->notify != NOTIFY_IDLE){
                active_id = i;
                cmd_msg = m_worker_info_[i]->cmd_msg;
                record = i + MAX_SERVER_WORKER;
                break;
            }
        }
        if (active_id == -1) {
            record = num;
            continue;
        }
        cmd_msg->notify = NOTIFY_IDLE;
        RequestsMsg *request = (RequestsMsg *)cmd_msg;
        if(active_id != request->id) {
            printf("find %d, receive from id:%d\n", active_id, request->id);
        }
        assert(active_id == request->id);
        work_info = m_worker_info_[request->id];
        cmd_resp = work_info->cmd_resp_msg;
        memset(cmd_resp, 0, sizeof(CmdMsgRespBlock));
        resp_mr = work_info->resp_mr;
        cmd_resp->notify = NOTIFY_WORK;
        active_id = -1;
        if (request->type == MSG_REGISTER) {
            /* handle memory register requests */
            RegisterRequest *reg_req = (RegisterRequest *)request;
            RegisterResponse *resp_msg = (RegisterResponse *)cmd_resp;
            if (allocate_and_register_memory(resp_msg->addr, resp_msg->rkey,
                                            reg_req->size)) {
                resp_msg->status = RES_FAIL;
            } else {
                resp_msg->status = RES_OK;
            }
            /* write response */
            // printf("%lx %u\n", resp_msg->addr, resp_msg->rkey);
            // resp_msg->addr = resp_msg->addr; resp_msg->rkey = 0;
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), reg_req->resp_addr,
                        reg_req->resp_rkey);
        } else if (request->type == MSG_FETCH_FAST) {
            /* handle memory fetch requests */
            FetchBlockResponse *resp_msg = (FetchBlockResponse *)cmd_resp;
            // uint64_t addr;
            // uint32_t rkey;
            while(!mw_binded) ;

            // if (fetch_mem_block(addr, rkey)) {
            resp_msg->status = RES_OK;
            resp_msg->size = REMOTE_MEM_SIZE*(1<<((FetchFastRequest*)request)->size_class);
            // printf("%d\n", resp_msg->size);
            if(resp_msg->size == 4096)
            if(resp_msg->size == 4096)
                resp_msg->addr = (uint64_t)memkind_malloc(memkind_, resp_msg->size);
            else
                 memkind_posix_memalign(memkind_, (void**)&resp_msg->addr, resp_msg->size, resp_msg->size);
            if(resp_msg->addr == 0){
                printf("error!%d\n", resp_msg->size);
            }
            reg_size_.fetch_add(resp_msg->size);
            bind_mw(block_mw[0], resp_msg->addr, resp_msg->size, rebinder_qp, rebinder_cq);
            // resp_msg->addr = (uint64_t)memkind_malloc(memkind_, resp_msg->size);
            resp_msg->rkey = global_rkey_;
            // } else {
            //     resp_msg->status = RES_FAIL;
            // }
            /* write response */
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), request->resp_addr,
                        request->resp_rkey);
        } else if (request->type == MSG_FREE_FAST) {
            ResponseMsg *resp_msg = (ResponseMsg *)cmd_resp;
            uint64_t addr = ((FreeFastRequest*)request)->addr;
            // if (free_mem_block(addr)) {
            reg_size_.fetch_sub(REMOTE_MEM_SIZE);

                memkind_free(memkind_, (void*)addr);
                resp_msg->status = RES_OK;
            // } else {
            //     resp_msg->status = RES_FAIL;
            // }
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), request->resp_addr,
                        request->resp_rkey);
            // Attension: no actual used at the critical path
        } else if (request->type == MSG_MW_REBIND) {
            RebindBlockRequest *reg_req = (RebindBlockRequest *)request;
            RebindBlockResponse *resp_msg = (RebindBlockResponse *)cmd_resp;
            uint32_t block_id = (reg_req->addr - server_block_manager_->get_heap_start())/ server_block_manager_->get_block_size();
            uint32_t rkey = server_block_manager_->get_backup_rkey(block_id);
            if(rkey != -1){
                resp_msg->rkey = rkey;
                server_block_manager_->set_backup_rkey(block_id, -1);
                resp_msg->status = RES_OK;
            } else {
                server_block_manager_->set_backup_rkey(block_id, 0);
                bind_mw(block_mw[block_id], server_block_manager_->get_block_addr(block_id), server_block_manager_->get_block_size(), rebinder_qp, rebinder_cq);
                resp_msg->rkey = server_block_manager_->get_backup_rkey(block_id);
                server_block_manager_->set_block_rkey(block_id, backup_mw[block_id]->rkey);
                ibv_mw* swap = block_mw[block_id]; block_mw[block_id] = backup_mw[block_id]; backup_mw[block_id] = swap;
                server_block_manager_->set_backup_rkey(block_id, -1);
                resp_msg->status = RES_OK;
            }
            /* write response */
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), reg_req->resp_addr,
                        reg_req->resp_rkey);
        } else if (request->type == RPC_FUSEE_SUBTABLE){
            uint64_t addr = rpc_fusee_->mm_alloc_subtable();
            uint32_t rkey = get_global_rkey();
            FuseeSubtableResponse* resp_msg = (FuseeSubtableResponse*)cmd_resp;
            if(addr != 0){
                resp_msg->addr = addr;
                resp_msg->rkey = rkey;
                resp_msg->status = RES_OK;
            } else {
                resp_msg->status = RES_FAIL;
            }
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                sizeof(CmdMsgRespBlock), request->resp_addr,
                request->resp_rkey);
        } else if (request->type == MSG_UNREGISTER) {
            /* handle memory unregister requests */
            UnregisterRequest *unreg_req = (UnregisterRequest *)request;
            UnregisterResponse *resp_msg = (UnregisterResponse *)cmd_resp;
            if (deallocate_and_unregister_memory(unreg_req->addr)) {
                resp_msg->status = RES_FAIL;
            } else {
                resp_msg->status = RES_OK;
            }
            /* write response */
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), unreg_req->resp_addr,
                        unreg_req->resp_rkey);
        } else if(request->type == MSG_PRINT_INFO){
            InfoResponse *resp_msg = (InfoResponse *)cmd_resp;
            resp_msg->total_mem = print_alloc_info();
            resp_msg->status = RES_OK;
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), request->resp_addr,
                        request->resp_rkey);
        } else if(request->type == MSG_MW_BATCH){
            RebindBatchRequest *reg_req = (RebindBatchRequest *)request;
            RebindBatchResponse *resp_msg = (RebindBatchResponse *)cmd_resp;
            for(int i = 0; i < 32; i++) {
                uint32_t block_id = (reg_req->addr[i] - server_block_manager_->get_heap_start())/ server_block_manager_->get_block_size();
                uint32_t rkey = server_block_manager_->get_backup_rkey(block_id);
                if(rkey != -1) {
                    resp_msg->rkey[i] = rkey;
                    resp_msg->status = RES_OK;
                } else {
                    server_block_manager_->set_backup_rkey(block_id, 0);
                    bind_mw(block_mw[block_id], server_block_manager_->get_block_addr(block_id), server_block_manager_->get_block_size(), rebinder_qp, rebinder_cq);
                    resp_msg->rkey[i] = server_block_manager_->get_backup_rkey(block_id);
                    server_block_manager_->set_block_rkey(block_id, backup_mw[block_id]->rkey);
                    ibv_mw* swap = block_mw[block_id]; block_mw[block_id] = backup_mw[block_id]; backup_mw[block_id] = swap;
                    server_block_manager_->set_backup_rkey(block_id, -1);
                }
            }
            /* write response */
            remote_write(work_info, (uint64_t)cmd_resp, resp_mr->lkey,
                        sizeof(CmdMsgRespBlock), reg_req->resp_addr,
                        reg_req->resp_rkey);
        } else {
            printf("wrong request type\n");
        }
    }
}

void MemoryNode::recovery(int id) {
    printf("recovery node %d\n", id);
    server_block_manager_->recovery(id);
}

}  // namespace kv
