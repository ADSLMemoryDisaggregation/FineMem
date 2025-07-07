
#include "rdma_conn_manager.h"
#include "atomic"
#include "msg.h"

namespace mralloc {

int ConnectionManager::init(const std::string ip, const std::string port,
                            uint32_t rpc_conn_num,
                            uint32_t one_sided_conn_num, uint16_t node_id) {
  m_rpc_conn_queue_ = new ConnQue();
  m_one_sided_conn_queue_ = new ConnQue();
  if (rpc_conn_num > MAX_SERVER_WORKER * MAX_SERVER_CLIENT) {
    printf(
        "max server worker is %d, rpc_conn_num is: %d, reset rpc_conn_num to "
        "%d\n",
        MAX_SERVER_WORKER, rpc_conn_num, MAX_SERVER_WORKER);
    rpc_conn_num = MAX_SERVER_WORKER;
  }

  for (uint32_t i = 0; i < rpc_conn_num; i++) {
    RDMAConnection *conn = new RDMAConnection();
    if (conn->init(ip, port, CONN_RPC, node_id)) {
      return -1;
    }
    m_one_side_info_ = conn->get_one_side_info();
    global_rkey_ = conn->get_global_rkey();
    m_rpc_conn_queue_->enqueue(conn);
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

  for (uint32_t i = 0; i < one_sided_conn_num; i++) {
    RDMAConnection *conn = new RDMAConnection();
    if (conn->init(ip, port, CONN_ONESIDE, node_id)) {
      return -1;
    }
    m_one_sided_conn_queue_->enqueue(conn);
  }
  return 0;
}

int ConnectionManager::register_remote_memory(uint64_t &addr, uint32_t &rkey,
                                              uint64_t size) {
  RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
  assert(conn != nullptr);
  int ret = conn->register_remote_memory(addr, rkey, size);
  m_rpc_conn_queue_->enqueue(conn);
  return ret;
}

int ConnectionManager::unregister_remote_memory(uint64_t addr) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->unregister_remote_memory(addr);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::remote_read(void *ptr, uint64_t size,
                                   uint64_t remote_addr, uint32_t rkey) {
  RDMAConnection *conn = m_one_sided_conn_queue_->dequeue();
  assert(conn != nullptr);
  int ret = conn->remote_read(ptr, size, remote_addr, rkey);
  m_one_sided_conn_queue_->enqueue(conn);
  return ret;
}

int ConnectionManager::remote_write(void *ptr, uint64_t size,
                                    uint64_t remote_addr, uint32_t rkey) {
  RDMAConnection *conn = m_one_sided_conn_queue_->dequeue();
  assert(conn != nullptr);
  int ret = conn->remote_write(ptr, size, remote_addr, rkey);
  m_one_sided_conn_queue_->enqueue(conn);
  return ret;
}

bool ConnectionManager::remote_CAS(uint64_t swap, uint64_t* compare, uint64_t remote_addr, uint32_t rkey) {
  RDMAConnection *conn = m_one_sided_conn_queue_->dequeue();
  assert(conn != nullptr);
  bool ret = conn->remote_CAS(swap, compare, remote_addr, rkey);
  m_one_sided_conn_queue_->enqueue(conn);
  return ret;
}


int ConnectionManager::remote_fetch_block(uint64_t &addr, uint32_t &rkey, uint16_t size_class) {
  RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
  assert(conn != nullptr);
  int ret = conn->remote_fetch_block(addr, rkey, size_class);
  m_rpc_conn_queue_->enqueue(conn);
  return ret;
}

int ConnectionManager::fetch_region_batch(section_e &alloc_section, region_e &alloc_region, mr_rdma_addr* addr, uint64_t num, bool is_exclusive, uint32_t region_index) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->fetch_region_batch(alloc_section, alloc_region, addr, num, is_exclusive, region_index);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::remote_free_block(uint64_t addr) {
  RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
  assert(conn != nullptr);
  int ret = conn->remote_free_block(addr);
  m_rpc_conn_queue_->enqueue(conn);
  return ret;
}

int ConnectionManager::remote_mw(uint64_t addr, uint32_t rkey, uint64_t size, uint32_t &newkey){
  RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
  assert(conn != nullptr);
  int ret = conn->remote_mw(addr, rkey, size, newkey);
  m_rpc_conn_queue_->enqueue(conn);
  return ret;
}

bool ConnectionManager::force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    bool ret = conn->force_update_section_state(section, region_index, advise);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::full_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey){
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->full_alloc(alloc_section, section_offset, size_class, addr, rkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::full_free(uint64_t addr, uint16_t block_class){
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->full_free(addr, block_class);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}


int ConnectionManager::section_alloc(uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->section_alloc(section_offset, size_class, addr, rkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::find_section(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, alloc_advise advise) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->find_section(alloc_section, section_offset, size_class, advise);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::region_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey){
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->region_alloc(alloc_section, section_offset, size_class, addr, rkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::chunk_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, bool use_chance, uint64_t &addr, uint32_t &rkey){
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->chunk_alloc(alloc_section, section_offset, size_class, use_chance, addr, rkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;  
}

int ConnectionManager::fetch_region(section_e &alloc_section, uint32_t section_offset, uint16_t size_class, bool use_chance, region_e &alloc_region, uint32_t &region_index, uint32_t skip_mask) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->fetch_region(alloc_section, section_offset, size_class, use_chance, alloc_region, region_index, skip_mask);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::fetch_region_block(section_e &alloc_section, region_e &alloc_region, uint64_t &addr, uint32_t &rkey, bool is_exclusive, uint32_t region_index, uint16_t block_class) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->fetch_region_block(alloc_section, alloc_region, addr, rkey, is_exclusive, region_index, block_class);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;  
}

int ConnectionManager::fetch_block(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey, uint16_t size_class) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->fetch_block(block_hint, addr, rkey, size_class);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;  
}

int ConnectionManager::free_block(uint64_t addr, uint16_t size_class) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->free_block(addr, size_class);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;  
}

int ConnectionManager::fetch_block_bitmap(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->fetch_block_bitmap(block_hint, addr, rkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;  
}

int ConnectionManager::free_block_bitmap(uint64_t addr) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->free_block_bitmap(addr);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;  
}

bool ConnectionManager::fetch_exclusive_region_rkey(uint32_t region_index, rkey_table_e* rkey_list) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    bool ret = conn->fetch_exclusive_region_rkey(region_index, rkey_list);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::remote_rebind(uint64_t addr, uint32_t &newkey) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->remote_rebind(addr, newkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::remote_rebind_batch(uint64_t *addr, uint32_t *newkey) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->remote_rebind_batch(addr, newkey);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::remote_memzero(uint64_t addr, uint64_t size) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->remote_memzero(addr, size);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::remote_print_alloc_info(uint64_t &mem_usage) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->remote_print_alloc_info(mem_usage);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::free_region_block(uint64_t addr, bool is_exclusive, uint16_t block_class) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->free_region_block(addr, is_exclusive, block_class);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

int ConnectionManager::free_region_batch(uint32_t region_offset, uint32_t free_bitmap, bool is_exclusive) {
    RDMAConnection *conn = m_rpc_conn_queue_->dequeue();
    assert(conn != nullptr);
    int ret = conn->free_region_batch(region_offset, free_bitmap, is_exclusive);
    m_rpc_conn_queue_->enqueue(conn);
    return ret;
}

}  // namespace kv
