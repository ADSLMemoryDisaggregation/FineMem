#include "rdma_conn.h"
#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include "free_block_manager.h"
#include "msg.h"

namespace mralloc {


int RDMAConnection::init(const std::string ip, const std::string port, uint8_t access_type, uint16_t pid) {
    time_t time_; time(&time_); srand(time_);
    std::random_device e;
    mt.seed(e());
    m_cm_channel_ = rdma_create_event_channel();
    if (!m_cm_channel_) {
        perror("rdma_create_event_channel fail");
        return -1;
    }

    if (rdma_create_id(m_cm_channel_, &m_cm_id_, NULL, RDMA_PS_TCP)) {
        perror("rdma_create_id fail");
        return -1;
    }

    struct addrinfo *res;
    if (getaddrinfo(ip.c_str(), port.c_str(), NULL, &res) < 0) {
        perror("getaddrinfo fail");
        return -1;
    }

    struct addrinfo *t = nullptr;
    for (t = res; t; t = t->ai_next) {
        if (!rdma_resolve_addr(m_cm_id_, NULL, t->ai_addr, RESOLVE_TIMEOUT_MS)) {
        break;
        }
    }
    if (!t) {
        perror("getaddrdma_resolve_addrrinfo fail");
        return -1;
    }

    struct rdma_cm_event *event;
    if (rdma_get_cm_event(m_cm_channel_, &event)) {
        perror("rdma_get_cm_event fail");
        return -1;
    }

    if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        perror("RDMA_CM_EVENT_ADDR_RESOLVED fail");
        return -1;
    }

    rdma_ack_cm_event(event);

    if (rdma_resolve_route(m_cm_id_, RESOLVE_TIMEOUT_MS)) {
        perror("rdma_resolve_route fail");
        return -1;
    }

    if (rdma_get_cm_event(m_cm_channel_, &event)) {
        perror("rdma_get_cm_event fail");
        return 1;
    }

    if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        printf("aaa: %d\n", event->event);
        perror("RDMA_CM_EVENT_ROUTE_RESOLVED fail");
        return -1;
    }

    rdma_ack_cm_event(event);

    m_pd_ = ibv_alloc_pd(m_cm_id_->verbs);
    if (!m_pd_) {
        perror("ibv_alloc_pd fail");
        return -1;
    }

    struct ibv_comp_channel *comp_chan;
    comp_chan = ibv_create_comp_channel(m_cm_id_->verbs);
    if (!comp_chan) {
        perror("ibv_create_comp_channel fail");
        return -1;
    }

    m_cq_ = ibv_create_cq(m_cm_id_->verbs, 1024, NULL, comp_chan, 0);
    if (!m_cq_) {
        perror("ibv_create_cq fail");
        return -1;
    }

    if (ibv_req_notify_cq(m_cq_, 0)) {
        perror("ibv_req_notify_cq fail");
        return -1;
    }

    struct ibv_qp_init_attr qp_attr = {};
    qp_attr.cap.max_send_wr = 512;
    qp_attr.cap.max_send_sge = 16;
    qp_attr.cap.max_recv_wr = 1;
    qp_attr.cap.max_recv_sge = 16;
    qp_attr.sq_sig_all = 0;


    qp_attr.send_cq = m_cq_;
    qp_attr.recv_cq = m_cq_;
    qp_attr.qp_type = IBV_QPT_RC;
    if (rdma_create_qp(m_cm_id_, m_pd_, &qp_attr)) {
        perror("rdma_create_qp fail");
        return -1;
    }

    CNodeInit init_msg = {pid, access_type};
    node_id_ = pid;
    // uint8_t access_type_ = access_type;
    struct rdma_conn_param conn_param = {};
    conn_param.responder_resources = 16;
    conn_param.private_data = &init_msg;
    conn_param.private_data_len = sizeof(CNodeInit);
    conn_param.initiator_depth = 16;
    conn_param.retry_count = 7;
    conn_param.rnr_retry_count = 7;
    if (rdma_connect(m_cm_id_, &conn_param)) {
        perror("rdma_connect fail");
        return -1;
    }

    if (rdma_get_cm_event(m_cm_channel_, &event)) {
        perror("rdma_get_cm_event fail");
        return -1;
    }

    if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
        perror("RDMA_CM_EVENT_ESTABLISHED fail");
        return -1;
    }

    struct PData server_pdata;
    memcpy(&server_pdata, event->param.conn.private_data, sizeof(server_pdata));

    rdma_ack_cm_event(event);

    m_server_cmd_msg_ = server_pdata.buf_addr;
    m_server_cmd_rkey_ = server_pdata.buf_rkey;
    m_one_side_info_ = {server_pdata.block_size_,
                        server_pdata.block_num_,
                        server_pdata.global_rkey_, 
                        server_pdata.section_header_, 
                        server_pdata.heap_start_};
    global_rkey_ = server_pdata.global_rkey_;
    conn_id_ = server_pdata.id;
    
    block_size_ = server_pdata.block_size_;
    block_num_ = server_pdata.block_num_;
    region_size_ = block_size_ * block_per_region;
    region_num_ = block_num_ / block_per_region;
    section_size_ = region_size_ * region_per_section;
    section_num_ = region_num_ / region_per_section;

    section_header_ = server_pdata.section_header_;
    region_header_ = (uint64_t)((section_e*)section_header_ + section_num_);
    block_rkey_ = (uint64_t)((region_e*)region_header_ + region_num_);
    block_header_ = (uint64_t)((rkey_table_e*)block_rkey_ + block_num_);
    public_info_ = (PublicInfo*)((uint64_t*)block_header_ + block_num_);
    heap_start_ = server_pdata.heap_start_;
    
    
    assert(server_pdata.size == sizeof(CmdMsgBlock));

    m_cmd_msg_ = new CmdMsgBlock();
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    m_msg_mr_ = rdma_register_memory((void *)m_cmd_msg_, sizeof(CmdMsgBlock));
    if (!m_msg_mr_) {
        perror("ibv_reg_mr m_msg_mr_ fail");
        return -1;
    }

    m_cmd_resp_ = new CmdMsgRespBlock();
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_resp_mr_ =
        rdma_register_memory((void *)m_cmd_resp_, sizeof(CmdMsgRespBlock));
    if (!m_resp_mr_) {
        perror("ibv_reg_mr m_resp_mr_ fail");
        return -1;
    }

    m_reg_buf_ = new char[MAX_REMOTE_SIZE];
    m_reg_buf_mr_ = rdma_register_memory((void *)m_reg_buf_, MAX_REMOTE_SIZE);
    if (!m_reg_buf_mr_) {
        perror("ibv_reg_mr m_reg_buf_mr_ fail");
        return -1;
    }

    return 0;
}

struct ibv_mr *RDMAConnection::rdma_register_memory(void *ptr, uint64_t size) {
  struct ibv_mr *mr =
      ibv_reg_mr(m_pd_, ptr, size,
                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                     IBV_ACCESS_REMOTE_WRITE);
  if (!mr) {
    perror("ibv_reg_mr fail");
    return nullptr;
  }
  return mr;
}

int RDMAConnection::rdma_remote_read(uint64_t local_addr, uint32_t lkey,
                                     uint64_t length, uint64_t remote_addr,
                                     uint32_t rkey) {
 
  struct ibv_sge sge;
  sge.addr = (uintptr_t)local_addr;
  sge.length = length;
  sge.lkey = lkey;

  struct ibv_send_wr send_wr = {};
  struct ibv_send_wr *bad_send_wr;
  send_wr.wr_id = 0;
  send_wr.num_sge = 1;
  send_wr.next = NULL;
  send_wr.opcode = IBV_WR_RDMA_READ;
  send_wr.sg_list = &sge;
  send_wr.send_flags = IBV_SEND_SIGNALED;
  send_wr.wr.rdma.remote_addr = remote_addr;
  send_wr.wr.rdma.rkey = rkey;
  if (ibv_post_send(m_cm_id_->qp, &send_wr, &bad_send_wr)) {
    perror("ibv_post_send fail");
    return -1;
  }

  auto start = TIME_NOW;
  int ret = -1;
  struct ibv_wc wc;
  while (true) {
    if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
      printf("rdma_remote_read timeout\n");
      return -1;
    }

    int rc = ibv_poll_cq(m_cq_, 1, &wc);
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

int RDMAConnection::rdma_remote_write(uint64_t local_addr, uint32_t lkey,
                                      uint64_t length, uint64_t remote_addr,
                                      uint32_t rkey) {
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
  if (ibv_post_send(m_cm_id_->qp, &send_wr, &bad_send_wr)) {
    perror("ibv_post_send fail");
    return -1;
  }

  auto start = TIME_NOW;
  int ret = -1;
  struct ibv_wc wc;
  while (true) {
    if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
      printf("rdma_remote_write timeout\n");
      return -1;
    }
    int rc = ibv_poll_cq(m_cq_, 1, &wc);
    if (rc > 0) {
      if (IBV_WC_SUCCESS == wc.status) {
        // Break out as operation completed successfully
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

int RDMAConnection::remote_read(void *ptr, uint64_t size, uint64_t remote_addr,
                                uint32_t rkey) {
  int ret = rdma_remote_read((uint64_t)m_reg_buf_, m_reg_buf_mr_->lkey, size,
                             remote_addr, rkey);
  if (ret) {
    return -1;
  }
  memcpy(ptr, m_reg_buf_, size);
  return ret;
}

int RDMAConnection::remote_write(void *ptr, uint64_t size, uint64_t remote_addr,
                                 uint32_t rkey) {
  memcpy(m_reg_buf_, ptr, size);
  return rdma_remote_write((uint64_t)m_reg_buf_, m_reg_buf_mr_->lkey, size,
                           remote_addr, rkey);
}

bool RDMAConnection::remote_CAS(uint64_t swap, uint64_t *compare, uint64_t remote_addr, uint32_t rkey) {

    struct ibv_sge sge;
    sge.addr = (uintptr_t)m_reg_buf_;
    sge.length = sizeof(uint64_t);
    sge.lkey = m_reg_buf_mr_->lkey; 

    struct ibv_send_wr send_wr = {};
    struct ibv_send_wr *bad_send_wr;
    send_wr.wr_id = 0;
    send_wr.num_sge = 1;
    send_wr.next = NULL;
    send_wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    send_wr.sg_list = &sge;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.wr.atomic.remote_addr = remote_addr;
    send_wr.wr.atomic.rkey = rkey;
    send_wr.wr.atomic.compare_add = *compare;
    send_wr.wr.atomic.swap = swap;
    if (ibv_post_send(m_cm_id_->qp, &send_wr, &bad_send_wr)) {
        perror("ibv_post_send fail");
        return -1;
    }

    auto start = TIME_NOW;
    int ret = -1;
    struct ibv_wc wc;
    while (true) {
        
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
            printf("rdma_remote_write timeout\n");
            return -1;
        }
        int rc = ibv_poll_cq(m_cq_, 1, &wc);
        if (rc > 0) {
        if (IBV_WC_SUCCESS == wc.status) {
            // Break out as operation completed successfully
            ret = 0;
            break;
        } else if (IBV_WC_WR_FLUSH_ERR == wc.status) {
            perror("CAS IBV_WC_WR_FLUSH_ERR");
            break;
        } else if (IBV_WC_RNR_RETRY_EXC_ERR == wc.status) {
            perror("CAS IBV_WC_RNR_RETRY_EXC_ERR");
            break;
        } else {
            perror("CAS ibv_poll_cq status error");
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
    if(*compare != *((uint64_t*)m_reg_buf_)){
        *compare = *((uint64_t*)m_reg_buf_);
        return false;
    }
    return true;
}

int RDMAConnection::register_remote_memory(uint64_t &addr, uint32_t &rkey,
                                           uint64_t size) {
  memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
  memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
  m_cmd_resp_->notify = NOTIFY_IDLE;
  RegisterRequest *request = (RegisterRequest *)m_cmd_msg_;
  request->resp_addr = (uint64_t)m_cmd_resp_;
  request->resp_rkey = m_resp_mr_->rkey;
  request->id = conn_id_;
  request->type = MSG_REGISTER;
  request->size = size;
  m_cmd_msg_->notify = NOTIFY_WORK;

  /* send a request to sever */
  int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                              sizeof(CmdMsgBlock), m_server_cmd_msg_,
                              m_server_cmd_rkey_);
  if (ret) {
    printf("fail to send requests\n");
    return ret;
  }

  /* wait for response */
  auto start = TIME_NOW;
  while (m_cmd_resp_->notify == NOTIFY_IDLE) {
    if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
      printf("wait for request completion timeout\n");
      return -1;
    }
  }
  RegisterResponse *resp_msg = (RegisterResponse *)m_cmd_resp_;
  if (resp_msg->status != RES_OK) {
    printf("register remote memory fail\n");
    return -1;
  }
  addr = resp_msg->addr;
  rkey = resp_msg->rkey;
  return 0;
}

int RDMAConnection::unregister_remote_memory(uint64_t addr) {
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_cmd_resp_->notify = NOTIFY_IDLE;
    UnregisterRequest *request = (UnregisterRequest *)m_cmd_msg_;
    request->resp_addr = (uint64_t)m_cmd_resp_;
    request->resp_rkey = m_resp_mr_->rkey;
    request->id = conn_id_;
    request->type = MSG_UNREGISTER;
    request->addr = addr;
    m_cmd_msg_->notify = NOTIFY_WORK;

    /* send a request to sever */
    int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                                sizeof(CmdMsgBlock), m_server_cmd_msg_,
                                m_server_cmd_rkey_);
    if (ret) {
        printf("fail to send requests\n");
        return ret;
    }

    /* wait for response */
    auto start = TIME_NOW;
    while (m_cmd_resp_->notify == NOTIFY_IDLE) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("wait for request completion timeout\n");
        return -1;
        }
    }
    UnregisterResponse *resp_msg = (UnregisterResponse *)m_cmd_resp_;
    if (resp_msg->status != RES_OK) {
        printf("register remote memory fail\n");
        return -1;
    }
    return 0;
}

int RDMAConnection::remote_fetch_block(uint64_t &addr, uint32_t &rkey, uint16_t size_class){
  memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
  memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
  m_cmd_resp_->notify = NOTIFY_IDLE;
  FetchFastRequest *request = (FetchFastRequest *)m_cmd_msg_;
  request->resp_addr = (uint64_t)m_cmd_resp_;
  request->resp_rkey = m_resp_mr_->rkey;
  request->id = conn_id_;
  request->type = MSG_FETCH_FAST;
  request->size_class = size_class;
  m_cmd_msg_->notify = NOTIFY_WORK;

  /* send a request to sever */
  int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                              sizeof(CmdMsgBlock), m_server_cmd_msg_,
                              m_server_cmd_rkey_);
  if (ret) {
    printf("fail to send requests\n");
    return ret;
  }

  /* wait for response */
  auto start = TIME_NOW;
  while (m_cmd_resp_->notify == NOTIFY_IDLE) {
    if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
      printf("wait for request completion timeout\n");
      return -1;
    }
  }
  FetchBlockResponse *resp_msg = (FetchBlockResponse *)m_cmd_resp_;
  if (resp_msg->status != RES_OK) {
    printf("fetch block block fail\n");
    return -1;
  }
  addr = resp_msg->addr;
  rkey = resp_msg->rkey;
  return 0;
}

int RDMAConnection::remote_mw(uint64_t addr, uint32_t rkey, uint64_t size, uint32_t &newkey){
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_cmd_resp_->notify = NOTIFY_IDLE;
    MWbindRequest *request = (MWbindRequest *)m_cmd_msg_;
    request->resp_addr = (uint64_t)m_cmd_resp_;
    request->resp_rkey = m_resp_mr_->rkey;
    request->id = conn_id_;
    request->type = MSG_MW_BIND;
    request->size = size;
    request->rkey = rkey;
    request->addr = addr;
    request->newkey = newkey;
    m_cmd_msg_->notify = NOTIFY_WORK;

    /* send a request to sever */
    int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                                sizeof(CmdMsgBlock), m_server_cmd_msg_,
                                m_server_cmd_rkey_);
    if (ret) {
        printf("fail to send requests\n");
        return ret;
    }

    /* wait for response */
    auto start = TIME_NOW;
    while (m_cmd_resp_->notify == NOTIFY_IDLE) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("wait for request completion timeout\n");
        return -1;
        }
    }
    MWbindResponse *resp_msg = (MWbindResponse *)m_cmd_resp_;
    if (resp_msg->status != RES_OK) {
        printf("mem window bind fail\n");
        return -1;
    }
    newkey = resp_msg->rkey;

      return 0;
}

int RDMAConnection::remote_rebind(uint64_t addr, uint32_t &newkey){
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_cmd_resp_->notify = NOTIFY_IDLE;
    RebindBlockRequest *request = (RebindBlockRequest *)m_cmd_msg_;
    request->resp_addr = (uint64_t)m_cmd_resp_;
    request->resp_rkey = m_resp_mr_->rkey;
    request->id = conn_id_;
    request->type = MSG_MW_REBIND;
    request->addr = addr;
    m_cmd_msg_->notify = NOTIFY_WORK;

    /* send a request to sever */
    int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                                sizeof(CmdMsgBlock), m_server_cmd_msg_,
                                m_server_cmd_rkey_);
    if (ret) {
        printf("fail to send requests\n");
        return ret;
    }

    /* wait for response */
    auto start = TIME_NOW;
    while (m_cmd_resp_->notify == NOTIFY_IDLE) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("wait for request completion timeout\n");
        return -1;
        }
    }
    RebindBlockResponse *resp_msg = (RebindBlockResponse *)m_cmd_resp_;
    if (resp_msg->status != RES_OK) {
        printf("mem window bind fail\n");
        return -1;
    }
    newkey = resp_msg->rkey;

    return 0;
}

int RDMAConnection::remote_rebind_batch(uint64_t *addr, uint32_t *newkey){
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_cmd_resp_->notify = NOTIFY_IDLE;
    RebindBatchRequest *request = (RebindBatchRequest *)m_cmd_msg_;
    request->resp_addr = (uint64_t)m_cmd_resp_;
    request->resp_rkey = m_resp_mr_->rkey;
    request->id = conn_id_;
    request->type = MSG_MW_BATCH;
    for(int i = 0; i < 32; i++)
        request->addr[i] = addr[i];
    m_cmd_msg_->notify = NOTIFY_WORK;

    /* send a request to sever */
    int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                                sizeof(CmdMsgBlock), m_server_cmd_msg_,
                                m_server_cmd_rkey_);
    if (ret) {
        printf("fail to send requests\n");
        return ret;
    }

    /* wait for response */
    auto start = TIME_NOW;
    while (m_cmd_resp_->notify == NOTIFY_IDLE) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("wait for request completion timeout\n");
        return -1;
        }
    }
    RebindBatchResponse *resp_msg = (RebindBatchResponse *)m_cmd_resp_;
    if (resp_msg->status != RES_OK) {
        printf("mem window bind fail\n");
        return -1;
    }
    for(int i = 0; i < 32; i++) {
        newkey[i] = resp_msg->rkey[i];
    }
    return 0;
}

int RDMAConnection::remote_memzero(uint64_t addr, uint64_t size) {
    struct ibv_sge sge;
    memset(m_reg_buf_, 0, size);
    sge.addr = (uintptr_t)m_reg_buf_;
    sge.length = size;
    sge.lkey = m_reg_buf_mr_->lkey; 

    struct ibv_send_wr send_wr = {};
    struct ibv_send_wr *bad_send_wr;
    send_wr.wr_id = 0;
    send_wr.num_sge = 1;
    send_wr.next = NULL;
    send_wr.opcode = IBV_WR_RDMA_WRITE;
    send_wr.sg_list = &sge;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.wr.rdma.remote_addr = addr;
    send_wr.wr.rdma.rkey = global_rkey_;
    if (ibv_post_send(m_cm_id_->qp, &send_wr, &bad_send_wr)) {
        perror("ibv_post_send fail");
        return -1;
    }

    auto start = TIME_NOW;
    int ret = -1;
    struct ibv_wc wc;
    while (true) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("rdma_remote_write timeout\n");
        return -1;
        }
        int rc = ibv_poll_cq(m_cq_, 1, &wc);
        if (rc > 0) {
        if (IBV_WC_SUCCESS == wc.status) {
            // Break out as operation completed successfully
            ret = 0;
            break;
        } else if (IBV_WC_WR_FLUSH_ERR == wc.status) {
            perror("memzero IBV_WC_WR_FLUSH_ERR");
            break;
        } else if (IBV_WC_RNR_RETRY_EXC_ERR == wc.status) {
            perror("memzero IBV_WC_RNR_RETRY_EXC_ERR");
            break;
        } else {
            perror("memzero ibv_poll_cq status error");
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

int RDMAConnection::remote_free_block(uint64_t addr) {
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_cmd_resp_->notify = NOTIFY_IDLE;
    FreeFastRequest *request = (FreeFastRequest *)m_cmd_msg_;
    request->resp_addr = (uint64_t)m_cmd_resp_;
    request->resp_rkey = m_resp_mr_->rkey;
    request->id = conn_id_;
    request->type = MSG_FREE_FAST;
    request->addr = addr;
    m_cmd_msg_->notify = NOTIFY_WORK;

    /* send a request to sever */
    int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                                sizeof(CmdMsgBlock), m_server_cmd_msg_,
                                m_server_cmd_rkey_);
    if (ret) {
        printf("fail to send requests\n");
        return ret;
    }

    /* wait for response */
    auto start = TIME_NOW;
    while (m_cmd_resp_->notify == NOTIFY_IDLE) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("wait for request completion timeout\n");
        return -1;
        }
    }
    FetchResponse *resp_msg = (FetchResponse *)m_cmd_resp_;
    if (resp_msg->status != RES_OK) {
        printf("fetch block block fail\n");
        return -1;
    }
    return 0;
}

int RDMAConnection::remote_print_alloc_info(uint64_t &mem_usage) {
    memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
    memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
    m_cmd_resp_->notify = NOTIFY_IDLE;
    FreeFastRequest *request = (FreeFastRequest *)m_cmd_msg_;
    request->resp_addr = (uint64_t)m_cmd_resp_;
    request->resp_rkey = m_resp_mr_->rkey;
    request->id = conn_id_;
    request->type = MSG_PRINT_INFO;
    m_cmd_msg_->notify = NOTIFY_WORK;

    /* send a request to sever */
    int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                                sizeof(CmdMsgBlock), m_server_cmd_msg_,
                                m_server_cmd_rkey_);
    if (ret) {
        printf("fail to send requests\n");
        return ret;
    }

    /* wait for response */
    auto start = TIME_NOW;
    while (m_cmd_resp_->notify == NOTIFY_IDLE) {
        if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
        printf("wait for request completion timeout\n");
        return -1;
        }
    }
    InfoResponse *resp_msg = (InfoResponse *)m_cmd_resp_;
    if (resp_msg->status != RES_OK) {
        printf("fetch block block fail\n");
        return -1;
    }
    mem_usage = resp_msg->total_mem;
    return 0;
}

int RDMAConnection::remote_fusee_alloc(uint64_t &addr, uint32_t &rkey){
  memset(m_cmd_msg_, 0, sizeof(CmdMsgBlock));
  memset(m_cmd_resp_, 0, sizeof(CmdMsgRespBlock));
  m_cmd_resp_->notify = NOTIFY_IDLE;
  RequestsMsg *request = (RequestsMsg *)m_cmd_msg_;
  request->resp_addr = (uint64_t)m_cmd_resp_;
  request->resp_rkey = m_resp_mr_->rkey;
  request->id = conn_id_;
  request->type = RPC_FUSEE_SUBTABLE;
  m_cmd_msg_->notify = NOTIFY_WORK;

  /* send a request to sever */
  int ret = rdma_remote_write((uint64_t)m_cmd_msg_, m_msg_mr_->lkey,
                              sizeof(CmdMsgBlock), m_server_cmd_msg_,
                              m_server_cmd_rkey_);
  if (ret) {
    printf("fail to send requests\n");
    return ret;
  }

  /* wait for response */
  auto start = TIME_NOW;
  while (m_cmd_resp_->notify == NOTIFY_IDLE) {
    if (TIME_DURATION_US(start, TIME_NOW) > RDMA_TIMEOUT_US) {
      printf("wait for request completion timeout\n");
      return -1;
    }
  }
  FuseeSubtableResponse *resp_msg = (FuseeSubtableResponse *)m_cmd_resp_;
  if (resp_msg->status != RES_OK) {
    printf("fetch block block fail\n");
    return -1;
  }
  addr = resp_msg->addr;
  rkey = resp_msg->rkey;
  return 0;
}

inline bool RDMAConnection::check_section(section_e alloc_section, alloc_advise advise, uint32_t offset) {
    switch (advise) {
    case alloc_empty:
        return ((~alloc_section.alloc_map_ & ~alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
    case alloc_light:
        return ((alloc_section.alloc_map_ & ~alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
    case alloc_heavy:
        return ((~alloc_section.alloc_map_ & alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
    case alloc_full:
        return ((alloc_section.alloc_map_ & alloc_section.frag_map_) & (bitmap16)1<< offset) != 0;
    }
    return false;
}

// each section state update is up-to-date and overwrite the old state
// update a state of alloc_empty is forbidden, only a fetch operation can do this
bool RDMAConnection::force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise) {
    uint64_t section_offset = region_index/region_per_section;
    uint64_t region_offset = region_index%region_per_section;
    section_e section_new;
    // remote_read(&section, sizeof(section), section_metadata_addr(section_offset), global_rkey_);
    if(advise == alloc_full) {
        do{
            if(check_section(section, advise, region_offset)){
                return false;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.alloc_map_ |= (bitmap16)1 << region_offset;
            section_new.frag_map_ |= (bitmap16)1 << region_offset;
        }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        return true;
    } else if(advise == alloc_empty) {
        do{
            if(check_section(section, advise, region_offset)){
                return true;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
            section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
        }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        return true;
    } else if(advise == alloc_light) {
        do{
            if(check_section(section, advise, region_offset)){
                return true;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
            section_new.alloc_map_ |= (bitmap16)1 << region_offset;
        }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        return true;
    } else if(advise == alloc_heavy) {
        do{
            if(check_section(section, advise, region_offset)){
                return true;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.frag_map_ |= (bitmap16)1 << region_offset;
            section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
        }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        return true;
    }
    return false;
}

// each section state update is up-to-date and overwrite the old state
// update a state of alloc_empty is forbidden, only a fetch operation can do this
bool RDMAConnection::force_update_section_state(section_e &section, uint32_t region_index, alloc_advise advise, alloc_advise compare) {
    uint64_t section_offset = region_index/region_per_section;
    uint64_t region_offset = region_index%region_per_section;
    section_e section_new;
    remote_read(&section, sizeof(section), section_metadata_addr(section_offset), global_rkey_);

    if(advise == alloc_full) {
        // do{
            if(!check_section(section, compare, region_offset)){
                return false;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.alloc_map_ |= (bitmap16)1 << region_offset;
            section_new.frag_map_ |= (bitmap16)1 << region_offset;
        // }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        remote_write(&section_new, sizeof(section_new), section_metadata_addr(section_offset), global_rkey_);
        return true;
    } else if(advise == alloc_empty) {
        // do{
            if(!check_section(section, compare, region_offset)){
                return false;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.alloc_map_ &= ~((bitmap16)1 << region_offset);
            section_new.frag_map_ &= ~((bitmap16)1 << region_offset);
        // }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        remote_write(&section_new, sizeof(section_new), section_metadata_addr(section_offset), global_rkey_);
        return true;
    } else if(advise == alloc_light) {
        // do{
            if(!check_section(section, compare, region_offset)){
                return false;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.frag_map_ &= (bitmap16)~((bitmap16)1 << region_offset);
            section_new.alloc_map_ |= (bitmap16)(bitmap16)1 << region_offset;
        // }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        remote_write(&section_new, sizeof(section_new), section_metadata_addr(section_offset), global_rkey_);
        return true;
    } else if(advise == alloc_heavy) {
        // do{
            if(!check_section(section, compare, region_offset)){
                return false;
            } else if (check_section(section, alloc_empty, region_offset)){
                return false;
            }
            section_new = section;
            section_new.frag_map_ |= (bitmap16)(bitmap16)1 << region_offset;
            section_new.alloc_map_ &= (bitmap16)~((bitmap16)1 << region_offset);
        // }while(!remote_CAS(*(uint64_t*)&section_new, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        remote_write(&section_new, sizeof(section_new), section_metadata_addr(section_offset), global_rkey_);
        return true;
    }
    return false;
}

int RDMAConnection::full_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey) {
    int retry = 0, alloc_time = 0;
    do{
    if(size_class > 9){
        return section_alloc(section_offset, size_class, addr, rkey);
    } else if(size_class >= 5 ){
        return region_alloc(alloc_section, section_offset, size_class, addr, rkey);
    } else {
        alloc_time ++;
        uint16_t skip_mask = 0;
        // region_e cache_region;
        // uint32_t cache_region_index;
        // fetch_region(alloc_section, section_offset, size_class, false, cache_region, cache_region_index, skip_mask);
        int retry_time = 0, cas_time = 0, section_time = 0, region_time = 0, result;
        bool slow_path = false;
        uint16_t first_section = section_offset;
        uint16_t ring = 0;

        while((retry = chunk_alloc(alloc_section, section_offset, size_class, slow_path, addr, rkey)) <= 0){
            if(!slow_path){
                if((result = find_section(alloc_section, section_offset, size_class, mralloc::alloc_light)) < 0){
                    printf("no section!\n");
                    slow_path = true;
                }else section_time += result;
                if(section_offset == first_section){
                    slow_path = true;
                    printf("slow path\n");
                }
            } else {
                if((result = find_section(alloc_section, section_offset, size_class, mralloc::alloc_heavy)) < 0){
                    printf("no section!\n");
                    break;
                }else section_time += result;
            }
        }
        if(result >= mralloc::retry_threshold) {
            if((result = find_section(alloc_section, section_offset, size_class, mralloc::alloc_light)) < 0){
                printf("no section!\n");
                // break;
            }else section_time += result;
        }
    }
    }while(retry <= 0 && alloc_time < 10);
    return retry;
}

int RDMAConnection::full_free(uint64_t addr, uint16_t block_class) {
    uint32_t section_offset = (addr - heap_start_) / section_size_;
    uint32_t section_region_offset = (addr - heap_start_) % section_size_ / region_size_;
    uint32_t region_offset = (addr - heap_start_) / region_size_;
    uint32_t region_block_offset = (addr - heap_start_) % region_size_ / block_size_;
    int retry_time = 0;
    if(block_class > 9) {
        // [TODO] multiple section headers free
    }
    else if (block_class >=5){
        // rebind_region_block_rkey(region_offset, region_block_offset);
        // [TODO] rkey support for section blcok rkey
        // [Stage 0] flush log
        section_e section, new_section;
        remote_read(&section, sizeof(section_e), section_metadata_addr(section_offset), global_rkey_);
        
            if(section.last_modify_id_ != 0){
                    if(((section.frag_map_ & section.alloc_map_) & ((uint32_t)1<<section.last_offset_)) != 0 ){
                        // malloc
                        block_e old_block;
                        remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * section_offset * region_per_section * block_per_region + sizeof(uint64_t) * section.last_offset_ * block_per_region, global_rkey_);
                        // block_e old_block = {0, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                        block_e new_block = {node_id_, section.last_timestamp_, section.num+5};
                        do {
                            int distant = abs((long)(old_block.timestamp_ & ~(1<<7)) - ((long)new_block.timestamp_& ~(1<<7)));
                            bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                            if((old_block.timestamp_ & (1<<7)) == 0){
                                region_e detail_region;
                                remote_read(&detail_region, sizeof(region_e), region_metadata_addr(section_offset*region_per_section + section.last_offset_), global_rkey_);
                                if(detail_region.on_use_ == 1){
                                    outdate = true;
                                }
                            }
                            if(old_block.client_id_ != 0 || outdate){
                                break;
                            }
                        } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * section_offset * region_per_section * block_per_region + sizeof(uint64_t) * section.last_offset_ * block_per_region, global_rkey_));
                    } else if(((section.frag_map_ | section.alloc_map_) & ((uint32_t)1<<section.last_offset_)) == 0) {
                        //free
                        // block_e old_block = {cache_region_array[chunk_index].last_modify_id_, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                        block_e old_block;
                        remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * section_offset * region_per_section * block_per_region + sizeof(uint64_t) * section.last_offset_ * block_per_region, global_rkey_);
                        block_e new_block = {0, section.last_timestamp_, section.num};
                        do {
                            int distant = abs((long)(old_block.timestamp_ & ~(1<<7)) - ((long)new_block.timestamp_& ~(1<<7)));
                            bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                            if((old_block.timestamp_ & (1<<7)) == 0){
                                region_e detail_region;
                                remote_read(&detail_region, sizeof(region_e), region_metadata_addr(section_offset*region_per_section + section.last_offset_), global_rkey_);
                                if(detail_region.on_use_ == 1){
                                    outdate = true;
                                }
                            }
                            if(old_block.client_id_ != section.last_modify_id_ || outdate ){
                                break;
                            }
                        } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * section_offset * region_per_section * block_per_region + sizeof(uint64_t) * section.last_offset_ * block_per_region, global_rkey_));
                    }
                }
        // bool full = (section.alloc_map_ == bitmap32_filled);
        uint32_t new_rkey;
        uint16_t mask = 0;
        for (int i = 0; i<(1<<(block_class-5)); i++){
            mask += (uint16_t)1<<i;
        }
        if(((section.frag_map_|section.alloc_map_) & ((uint16_t)mask<<section_region_offset)) == 0) {
            printf("already freed\n");
            return -1;
        } 
        // region_e region, new_region;
        // remote_read(&region, sizeof(region_e), region_metadata_addr(region_offset), global_rkey_);
        // do {
        //     retry_time++;
        //     new_region = region;
        //     if(new_region.on_use_ == 0) {
        //         printf("impossible problem: on_use is already clear\n");
        //         return retry_time*(-1);
        //     }
        //     new_region.on_use_ = 0;
        // }while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&region, region_metadata_addr(region_offset), global_rkey_));
                
        do{
            retry_time++;
            new_section = section;
            new_section.alloc_map_ &= ~(uint16_t)((uint16_t)mask<<section_region_offset);
            new_section.frag_map_ &= ~(uint16_t)((uint16_t)mask<<section_region_offset);
            retry_counter_ = new_section.retry_;
            new_section.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
            if ((section.alloc_map_&section.frag_map_) & ((uint16_t)1<<section.last_offset_) == 1) 
                new_section.last_timestamp_ = (((new_section.last_timestamp_ & ~(1<<7)) + 1) % 127) | (1<<7);
            new_section.last_modify_id_ = node_id_;
            new_section.last_offset_ = section_region_offset;
        } while(!remote_CAS(*(uint64_t*)&new_section, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
        // printf("free: %x, %x\n", new_section.alloc_map_, new_section.frag_map_);

        uint16_t old_retry = retry_counter_;
        retry_counter_ = retry_time;
        return 0;
    } else {
        region_e region, new_region;
        rebind_region_block_rkey(region_offset, region_block_offset);
        // [Stage 0] flush log
        remote_read(&region, sizeof(region_e), region_metadata_addr(region_offset), global_rkey_);
                if(region.last_modify_id_ != 0){
                    if((region.base_map_ & ((uint32_t)1<<region.last_offset_)) != 0 ){
                        // malloc
                        block_e old_block;
                        remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region.last_offset_, global_rkey_);
                        // block_e old_block = {0, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                        block_e new_block = {node_id_, region.last_timestamp_, region.num};
                        do {
                            int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                            bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                            if((old_block.timestamp_ & (1<<7)) != 0){
                                if(region.on_use_ == 0){
                                    outdate = true;
                                }
                            }
                            if(old_block.client_id_ != 0 || outdate){
                                break;
                            }
                        } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region.last_offset_, global_rkey_));
                    } else {
                        //free
                        // block_e old_block = {cache_region_array[chunk_index].last_modify_id_, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                        block_e old_block;
                        remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region.last_offset_, global_rkey_);
                        block_e new_block = {0, region.last_timestamp_, region.num};
                        do {
                            int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                            bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                            if((old_block.timestamp_ & (1<<7)) != 0){
                                if(region.on_use_ == 0){
                                    outdate = true;
                                }
                            }
                            if(old_block.client_id_ != region.last_modify_id_ || outdate ){
                                break;
                            }
                        } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region.last_offset_, global_rkey_));
                    }
                }
        bool full = (region.base_map_ == bitmap32_filled);
        uint32_t new_rkey;
        uint32_t mask = 0;
        for (int i = 0; i<(1<<block_class); i++){
            mask += (uint32_t)1<<i;
        }
        if((region.base_map_ & ((uint32_t)mask<<region_block_offset)) == 0) {
            printf("already freed\n");
            return -1;
        } 
        do{
            full = (region.base_map_ == bitmap32_filled);
            retry_time++;
            new_region = region;
            new_region.base_map_ &= ~(uint32_t)((uint32_t)mask<<region_block_offset);
            if ( new_region.base_map_ == (bitmap32)0 ) {
                new_region.on_use_ = 0;
                new_region.exclusive_ = 0;
            }
            retry_counter_ = new_region.retry_;
            new_region.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
            if (region.base_map_ & ((uint32_t)1<<region.last_offset_) == 1) 
                new_region.last_timestamp_ = (new_region.last_timestamp_ + 1) % 127 + 1;
            new_region.last_modify_id_ = node_id_;
            new_region.last_offset_ = region_block_offset;
        } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&region, region_metadata_addr(region_offset), global_rkey_));

        uint16_t old_retry = retry_counter_;
        retry_counter_ = retry_time;
        
        // concurrency state update, will async it in the future
        section_e alloc_section;
        if(new_region.base_map_ == (bitmap32)0 ){
            force_update_section_state(alloc_section, region_offset, alloc_empty); 
        } 
        else if(full) {
            force_update_section_state(alloc_section, region_offset, alloc_light, alloc_full);
        }
        else if(old_retry > 0 && retry_time < low_threshold) {
        // else if(old_retry < low_threshold && retry_time >= low_threshold) {
            force_update_section_state(alloc_section, region_offset, alloc_light, alloc_heavy);
            // printf("make region %d heavy\n", region_offset);
        } 
        else if(old_retry < 2 && retry_time >= retry_threshold) {
        // else if(old_retry >= retry_threshold && retry_time < retry_threshold) {
            force_update_section_state(alloc_section, region_offset, alloc_heavy, alloc_light);
            // printf("make region %d light\n", region_offset);
        } 
        else {
            force_update_section_state(alloc_section, region_offset, alloc_light, alloc_full);
        }

        region = new_region;
        return 0;
    }
}


int RDMAConnection::section_alloc(uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey) {
    if(size_class < 9){
        printf("use find section!\n");
        return -1;
    }
    int section_num = 1 << (size_class - 9);
    // section_e section[8] = {0,0};
    // section_offset += 16;
    int offset = (section_offset)%section_num_, retry_time = 0;
    int remain = section_num_, fetch = (offset + 16) > section_num_ ? (section_num_ - offset):16, index = offset;
    int total_section_num = 0;
    uint64_t start_addr;
    while(remain > 0) {
        // empty region exists
        retry_time++;
        if(cache_section_array_index != index){
            cache_section_array_index = index;
            remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
        }
        for(int j = 0; j < fetch; j ++) {
            if((cache_section_array[j].frag_map_ | cache_section_array[j].alloc_map_) == (uint16_t)0){
                if(total_section_num == 0){
                    start_addr = get_region_block_addr((index+j)*region_per_section, 0);
                }
                total_section_num ++;
                section_e new_section = cache_section_array[j];
                do{
                    new_section = cache_section_array[j];
                    if((cache_section_array[j].frag_map_ | cache_section_array[j].alloc_map_) != (uint16_t)0){
                        remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
                        total_section_num = 0;
                        if(total_section_num > 1){
                            // [TODO] roll back when larger than 2MiB
                        }
                        break;
                    }
                    new_section.alloc_map_ = bitmap16_filled;
                    new_section.frag_map_ = bitmap16_filled;
                    retry_counter_ = new_section.retry_;
                    new_section.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
                    new_section.last_offset_ = index;
                    new_section.last_timestamp_ = (new_section.last_timestamp_ + 1) % 127 + 1;
                    new_section.last_modify_id_ = node_id_;
                    new_section.num = (size_class-5);
                    // [TODO] check right?
                }while(!remote_CAS(*(uint64_t*)&new_section, (uint64_t*)&cache_section_array[j], section_metadata_addr(index+j), global_rkey_));
                if(total_section_num==section_num){
                    section_offset = index+j;
                    addr = start_addr;
                    rkey = 0;
                    // [TODO] fetch rkey in section size
                    // [TODO] flush redo log
                    return retry_time;
                }
            } else {
                total_section_num = 0;
            }
        }
        index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 16) > section_num_ ? (section_num_ - index):16;
    }
    return -1;
}

// find a new section avaliable for an allocation with advise(usually alloc_full)
int RDMAConnection::find_section(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, alloc_advise advise) {
    int retry_time = 0;
    // section_e section[8] = {0,0};
    int random_offset = 0;
    // int random_offset = mt()%16;
    section_offset += 1;
    int offset = (section_offset)%section_num_;
    // each epoch fetch 8 sections, 8*8B = 64Byte
    if(size_class >= 9) {
        printf("use section alloc!\n");
        return -1;
    }
    if (advise == alloc_heavy) {
        int remain = section_num_, fetch = (offset + 16) > section_num_ ? (section_num_ - offset):16, index = offset;
        // if there are not fully alloc_full, this section can be used
        while(remain > 0) {
            retry_time++;
            if(cache_section_array_index != index){
                cache_section_array_index = index;
                remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
            }
            for(int j = 0; j < fetch; j ++) {
                int section_index = (j+random_offset)%fetch; 
                if(!(cache_section_array[section_index].frag_map_ == 65535 && cache_section_array[section_index].alloc_map_ == 65535)){
                    printf("%u, %u\n", cache_section_array[section_index].frag_map_, cache_section_array[section_index].alloc_map_);
                    alloc_section = cache_section_array[section_index];
                    section_offset = index + section_index;
                    return retry_time;
                }
            }
            index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 16) > section_num_ ? (section_num_ - index):16;
        }
    } 
    else if (advise == alloc_light) {
        int remain = section_num_, fetch = (offset + 16) > section_num_ ? (section_num_ - offset):16, index = offset;
        // if there are not fully alloc_full, this section can be used
        while(remain > 0) {
            retry_time++;
            if(cache_section_array_index != index){
                cache_section_array_index = index;
                remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
            }
            for(int j = 0; j < fetch; j ++) {
                int section_index = (j+random_offset)%fetch; 
                if((cache_section_array[section_index].frag_map_ ) != 65535){
                    alloc_section = cache_section_array[section_index];
                    section_offset = index + section_index;
                    return retry_time;
                }
            }
            index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 16) > section_num_ ? (section_num_ - index):16;
        }
    } 
    else {
        int remain = section_num_, fetch = (offset + 16) > section_num_ ? (section_num_ - offset):16, index = offset;
        while(remain > 0) {
            // empty region exists
            retry_time++;
            if(cache_section_array_index != index){
                cache_section_array_index = index;
                remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
            }
            for(int j = 0; j < fetch; j ++) {
                int section_index = (j+random_offset)%fetch; 
                if((cache_section_array[section_index].frag_map_ | cache_section_array[section_index].alloc_map_) != bitmap16_filled){
                    alloc_section = cache_section_array[section_index];
                    section_offset = index + section_index;
                    return retry_time;
                }
            }
            index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + 16) > section_num_ ? (section_num_ - index):16;
        }
    }
    printf("find no section!\n");
    return retry_time*(-1);
}

int RDMAConnection::region_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, uint64_t &addr, uint32_t &rkey){
    int retry_time = 0;
    
    // [TODO] fetch variable rkey
    if(size_class > 9 || size_class < 5){
        printf("use find section or chunk alloc!\n");
        return -1;
    }
    // section_e section[8] = {0,0};
    int cache_size = 64;
    section_offset -= section_offset%cache_size;
    int region_num = 1 << (size_class - 5);
    int offset = (section_offset)%section_num_;
    int random_offset = mt()%cache_size;
    // int random_offset = 0;
    int out_date_counter = 0;
    int remain = section_num_, fetch = (offset + cache_size) > section_num_ ? (section_num_ - offset):cache_size, index = offset;
    int total_section_num = 0;
    uint64_t start_addr;
    while(remain > 0) {
        // remote_read(section, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
        if(cache_section_array_index != index || *(uint64_t*)&alloc_section != *(uint64_t*)&cache_section_array[0]){
            skip_section = -1;
            cache_section_array_index = index;
            remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
        }
        section_e new_section;
        for(int j = 0; j < fetch; j ++) {
            retry_time = 0;
            int retry_temp = 0;
            bool not_suitable = false;
            int section_index = (j+random_offset)%fetch;
            if(section_index == skip_section){
                skip_section = -1;
                continue;
            }
            uint16_t search_index = 0;
            do {
                not_suitable = false;
                search_index = 0;
                retry_time++;
                retry_temp++;
                 //if(retry_time > retry_threshold){
                   //  not_suitable = true;
                   //  break;
                 //}
                new_section = cache_section_array[section_index];
                // int block_num = 1<<(block_class);
                int size = 1<<(region_num+1);
                uint16_t search_map = new_section.frag_map_ | new_section.alloc_map_;
                while(search_map % size != 0 && search_index < 16){
                    search_map >>= region_num;
                    search_index += region_num;
                }
                if(search_index >= 16){
                    if(retry_temp > 1 ){
                        out_date_counter ++;
                        if(out_date_counter > retry_threshold) {
                            remote_read(cache_section_array, fetch*sizeof(section_e), section_metadata_addr(index), global_rkey_);
                            // remote_read(cache_region_array, 16*sizeof(region_e), region_metadata_addr(section_offset*region_per_section), global_rkey_);
                            out_date_counter = 0;
                        }
                    }
                    not_suitable = true;
                    break;
                }
                for(int i = 0; i < region_num; i++){
                    new_section.alloc_map_ |= (uint16_t)1<<(search_index+i);
                    new_section.frag_map_ |= (uint16_t)1<<(search_index+i);
                } 
                if(new_section.alloc_map_ == cache_section_array[section_index].alloc_map_){
                    printf("error!\n");
                }
                // [TODO] fix retry counter for section/region alloc
                retry_counter_ = new_section.retry_;
                new_section.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
                new_section.last_offset_ = search_index;
                if ((cache_section_array[section_index].alloc_map_|cache_section_array[section_index].frag_map_) & ((uint16_t)1<<cache_section_array[section_index].last_offset_) == 0) 
                    new_section.last_timestamp_ = (((new_section.last_timestamp_ & ~(1<<7)) + 1) % 127) | (1<<7);
                new_section.last_modify_id_ = node_id_;
                new_section.num = (size_class-5);
                if(cache_section_array[section_index].last_modify_id_ != 0){
                    if(((cache_section_array[section_index].frag_map_ & cache_section_array[section_index].alloc_map_) & ((uint32_t)1<<cache_section_array[section_index].last_offset_)) != 0 ){
                        // malloc
                        block_e old_block;
                        remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * section_index * region_per_section * block_per_region + sizeof(uint64_t) * cache_section_array[section_index].last_offset_ * block_per_region, global_rkey_);
                        // block_e old_block = {0, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                        block_e new_block = {node_id_, cache_section_array[section_index].last_timestamp_, cache_section_array[section_index].num+5};
                        do {
                            int distant = abs((long)(old_block.timestamp_ & ~(1<<7)) - ((long)new_block.timestamp_& ~(1<<7)));
                            bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                            if((old_block.timestamp_ & (1<<7)) == 0){
                                region_e detail_region;
                                remote_read(&detail_region, sizeof(region_e), region_metadata_addr(section_offset*region_per_section + cache_section_array[section_index].last_offset_), global_rkey_);
                                if(detail_region.on_use_ == 1){
                                    outdate = true;
                                }
                            }
                            if(old_block.client_id_ != 0 || outdate){
                                break;
                            }
                        } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * section_index * region_per_section * block_per_region + sizeof(uint64_t) * cache_section_array[section_index].last_offset_ * block_per_region, global_rkey_));
                    } else if(((cache_section_array[section_index].frag_map_ | cache_section_array[section_index].alloc_map_) & ((uint32_t)1<<cache_section_array[section_index].last_offset_)) == 0) {
                        //free
                        // block_e old_block = {cache_region_array[chunk_index].last_modify_id_, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                        block_e old_block;
                        remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * section_index * region_per_section * block_per_region + sizeof(uint64_t) * cache_section_array[section_index].last_offset_ * block_per_region, global_rkey_);
                        block_e new_block = {0, cache_section_array[section_index].last_timestamp_, cache_section_array[section_index].num};
                        do {
                            int distant = abs((long)(old_block.timestamp_ & ~(1<<7)) - ((long)new_block.timestamp_& ~(1<<7)));
                            bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                            if((old_block.timestamp_ & (1<<7)) == 0){
                                region_e detail_region;
                                remote_read(&detail_region, sizeof(region_e), region_metadata_addr(section_offset*region_per_section + cache_section_array[section_index].last_offset_), global_rkey_);
                                if(detail_region.on_use_ == 1){
                                    outdate = true;
                                }
                            }
                            if(old_block.client_id_ != cache_section_array[section_index].last_modify_id_ || outdate ){
                                break;
                            }
                        } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * section_index * region_per_section * block_per_region + sizeof(uint64_t) * cache_section_array[section_index].last_offset_ * block_per_region, global_rkey_));
                    }
                }
            }while(!remote_CAS(*(uint64_t*)&new_section, (uint64_t*)&cache_section_array[section_index], section_metadata_addr(index+section_index), global_rkey_));

            if(!not_suitable){
                // printf("alloc: %x, %x\n", new_section.alloc_map_, new_section.frag_map_);
                alloc_section = cache_section_array[section_index];
                section_offset = index+section_index;
                region_e region_new;
                region_e alloc_region;
                alloc_section = new_section;
                uint64_t region_index = section_offset*region_per_section + search_index;
                // read region info
                // [TODO] multiple region, and multiple update
                // [TODO] flush refo log
                addr = get_region_block_addr(region_index, 0);
                rkey = get_region_block_rkey(region_index, 0); 
                if(new_section.retry_ == 2){
                    skip_section = section_index;
                    section_offset = index+cache_size;
                } else {
                    alloc_section = cache_section_array[0];
                    section_offset = index;
                }
                return retry_time;
            }
        }
        index = (index + fetch)%section_num_; remain -= fetch; fetch = (index + cache_size) > section_num_ ? (section_num_ - index):cache_size;
    }
    return (-1)*retry_time;
}

int RDMAConnection::chunk_alloc(section_e &alloc_section, uint32_t &section_offset, uint16_t size_class, bool use_chance, uint64_t &addr, uint32_t &rkey){
    int retry_time = 0;
    // [TODO] fetch variable rkey
    // printf("chunk alloc?\n");
    if(size_class >= 5){
        printf("use find section or region alloc!\n");
        return -1;
    }
    // region_e region[16] = {0,0};
    int region_num = 1 << (size_class);
    int cache_size = 16;
    int offset = mt()%cache_size;
    // int offset = 0;
    int index = offset;
    // int out_date_threshold = 3;
    uint64_t start_addr;
    // retry_time++;
    section_e section = alloc_section;
    if(section_offset != cache_region_index){
        skip_region = -1;
        cache_region_index = section_offset;
        remote_read(cache_region_array, cache_size*sizeof(region_e), region_metadata_addr(section_offset*region_per_section), global_rkey_);
    }
    // remote_read(&section, sizeof(section_e), section_metadata_addr(section_offset), global_rkey_);
    region_e new_region;
    for(int iter = 0; iter<2; iter++){
        int out_date_counter = 0;
        for(int j = 0; j < cache_size; j ++) {

            //retry_time = 0;
            int retry_temp = 0;
            bool not_suitable = false;
            int chunk_index = (j+offset)%cache_size;
            // if(!use_chance && skip_region == chunk_index){
            //     skip_region = -1;
            //     continue;
            // }
            uint32_t region_index = section_offset*region_per_section+chunk_index;
            // [TODO]: redo log?
            // if(!use_chance && cache_region_array[chunk_index].retry_ == 2){
            //     continue;
            // }
            if(!use_chance && ((~section.alloc_map_ & section.frag_map_) & (uint16_t)1<<(chunk_index)) != 0){
                continue;
            }
            // if(!use_chance && iter == 0 && cache_region_array[chunk_index].on_use_ == 0){
            //     continue;
            // }
            // if(!use_chance && iter == 1 && ((section.frag_map_ & 1 << (chunk_index)) != 0)){
            //     continue;
            // }
            bool empty = false;
            if(cache_region_array[chunk_index].on_use_ == 0){
                // remote_read(&section, sizeof(section_e), section_metadata_addr(section_offset), global_rkey_);
                section_e new_section = section;
                empty = true;
                do{
                    index = chunk_index;
                    new_section = section;
                    if(((section.frag_map_|section.alloc_map_) & (uint16_t)1<<(chunk_index)) != 0){
                        empty = false;
                        break;
                    }
                    raise_bit(new_section.alloc_map_, new_section.frag_map_, index);
                }while(!remote_CAS(*(uint64_t*)&new_section, (uint64_t*)&section, section_metadata_addr(section_offset), global_rkey_));
                if(!empty && ((section.frag_map_&section.alloc_map_) & (uint16_t)1<<(chunk_index)) != 0){
                    continue;
                }
            }
	        retry_time = 0;
            do {
                retry_temp ++;
                retry_time++;
                new_region = cache_region_array[chunk_index];
                // int block_num = 1<<(block_class);
                int size = 1<<(region_num+1);
                uint32_t search_map = new_region.base_map_;
                index = 0;
                if(size_class == 0){
                    if((index = find_free_index_from_bitmap32_tail(new_region.base_map_)) == -1) {
                        if(retry_temp > 1 ){
                            out_date_counter ++;
                            if(out_date_counter > retry_threshold) {
                                remote_read(cache_region_array, cache_size*sizeof(region_e), region_metadata_addr(section_offset*region_per_section), global_rkey_);
                                out_date_counter = 0;
                            }
                        }
                        new_region = cache_region_array[chunk_index];
                        size = 1<<(region_num+1);
                        search_map = new_region.base_map_;
                        if((index = find_free_index_from_bitmap32_tail(new_region.base_map_)) == -1) {
                            force_update_section_state(alloc_section, region_index, alloc_full);
                            not_suitable  = true;
                            break;
                        }
                        // return retry_time*(-1);
                    }
                    new_region.base_map_ |= (uint32_t)1<<index;
                } else {
                    while(search_map % size != 0 && index < 32){
                        search_map >>= region_num;
                        index += region_num;
                    }
                    if(index >= 32){
                        if(retry_temp > 1 ){
                            out_date_counter ++;
                            if(out_date_counter > retry_threshold) {
                                remote_read(cache_region_array, cache_size*sizeof(region_e), region_metadata_addr(section_offset*region_per_section), global_rkey_);
                                out_date_counter = 0;
                            }
                        }
                        new_region = cache_region_array[chunk_index];
                        size = 1<<(region_num+1);
                        search_map = new_region.base_map_;
                        not_suitable = true;
                        break;
                    }
                    for(int i = 0; i < region_num; i++){
                        new_region.base_map_ |= (uint32_t)1<<(index+i);
                    } 
                }
                retry_counter_ = new_region.retry_;
                new_region.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
                new_region.last_offset_ = index;
                if ((cache_region_array[chunk_index].base_map_) & ((uint32_t)1<<cache_region_array[chunk_index].last_offset_) == 0) 
                    new_region.last_timestamp_ = (new_region.last_timestamp_ + 1) % 127 ;
                new_region.last_modify_id_ = node_id_;
                new_region.num = size_class;
                if(empty)
                    new_region.on_use_ = 1;
                // flush log
                // if(cache_region_array[chunk_index].last_modify_id_ != 0){
                //     if((cache_region_array[chunk_index].base_map_ & ((uint32_t)1<<cache_region_array[chunk_index].last_offset_)) != 0 ){
                //         // malloc
                //         block_e old_block;
                //         remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * cache_region_array[chunk_index].last_offset_, global_rkey_);
                //         // block_e old_block = {0, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                //         block_e new_block = {node_id_, cache_region_array[chunk_index].last_timestamp_, cache_region_array[chunk_index].num};
                //         do {
                //             int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                //             bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                //             if((old_block.timestamp_ & (1<<7)) != 0){
                //                 if(cache_region_array[chunk_index].on_use_ == 0){
                //                     outdate = true;
                //                 }
                //             }
                //             if(old_block.client_id_ != 0 || outdate){
                //                 break;
                //             }
                //         } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * cache_region_array[chunk_index].last_offset_, global_rkey_));
                //     } else {
                //         //free
                //         // block_e old_block = {cache_region_array[chunk_index].last_modify_id_, (cache_region_array[chunk_index].last_timestamp_ +126) % 127 + 1, 0};
                //         block_e old_block;
                //         remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * cache_region_array[chunk_index].last_offset_, global_rkey_);
                //         block_e new_block = {0, cache_region_array[chunk_index].last_timestamp_, cache_region_array[chunk_index].num};
                //         do {
                //             int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                //             bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                //             if((old_block.timestamp_ & (1<<7)) != 0){
                //                 if(cache_region_array[chunk_index].on_use_ == 0){
                //                     outdate = true;
                //                 }
                //             }
                //             if(old_block.client_id_ != cache_region_array[chunk_index].last_modify_id_ || outdate ){
                //                 break;
                //             }
                //         } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * cache_region_array[chunk_index].last_offset_, global_rkey_));
                //     }
                // }

            }while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&cache_region_array[chunk_index], region_metadata_addr(region_index), global_rkey_));
            cache_region_array[chunk_index] = new_region;
            if(!not_suitable){
                // out_date_counter -= 1;
                addr = get_region_block_addr(region_index, index);
                rkey = get_global_rkey();
                // rkey = get_region_block_rkey(region_index, index); 

                // retry counter for least 3 time allocation
                uint64_t old_retry = retry_counter_;
                retry_counter_ = retry_time;
                if(new_region.retry_ == 2){
                    skip_region = chunk_index;
                }
                // [Stage 2] state update, will async it in the future
                if(new_region.base_map_ == bitmap32_filled) {
                    force_update_section_state(alloc_section, region_index, alloc_full);
                } 
                else if(old_retry > 0 && retry_time < low_threshold) {
                     force_update_section_state(alloc_section, region_index, alloc_light, alloc_heavy);
                 } 
                 else if(old_retry < 2 && retry_time >= retry_threshold) {
                     force_update_section_state(alloc_section, region_index, alloc_heavy, alloc_light);
                 } 
                // [Stage 3] Flush log, will async it in the future
                // block_e old_block;
                // remote_read(&old_block, sizeof(block_e), block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * index, global_rkey_);
                //     // block_e old_block = {0, (new_region.last_timestamp_ + 126 ) % 127 + 1, size_class};
                // block_e new_block = {node_id_, new_region.last_timestamp_, size_class};
                // bool out_date = false;
                // do {
                //     int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                //     bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                //     if(old_block.client_id_ != 0 || out_date){
                //         // out-of-date update, skip
                //         // printf("other people done this: %d instead of %d\n", old_block.client_id_, node_id_);
                //         out_date = true;
                //         break;
                //     }
                // } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * index, global_rkey_));
                // if(!out_date){
                //     new_region = cache_region_array[chunk_index];
                //     new_region.last_modify_id_ = 0;
                //     // no matter true or false, no retry
                //     // if false, someone other must have done persistence, then do nothing 
                //     remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&cache_region_array[chunk_index], region_metadata_addr(region_index), global_rkey_);
                // }
                return retry_time;
            }
        }
    }
    return retry_time*(-1);
}

// find an avalible region, exclusive, single
int RDMAConnection::fetch_region(section_e &alloc_section, uint32_t section_offset, uint16_t size_class, bool use_chance, region_e &alloc_region, uint32_t &region_index, uint32_t skip_mask) {
    int retry_time = 0;
    bool on_empty = false;
    section_e new_section;
    uint16_t empty_map, chance_map, normal_map;
    int index;
    // skip a read, only read at CAS failed
    // remote_read(&alloc_section, sizeof(section_e), section_metadata_addr(section_offset), global_rkey_);
    // int region_require_num, region_require_size;
    if(size_class < 5) {
        // fetch single region
        // scan whole chunks
        do {
            retry_time++;
            int rand_val = mt()%16;
            uint16_t random_frag = ((alloc_section.frag_map_|skip_mask) >> (16 - rand_val) | ((alloc_section.frag_map_|skip_mask) << rand_val));
            uint16_t random_alloc = ((alloc_section.alloc_map_|skip_mask) >> (16 - rand_val) | ((alloc_section.alloc_map_|skip_mask) << rand_val));
            empty_map = random_frag | random_alloc;
            chance_map = ~random_frag | random_alloc;
            normal_map = random_frag | ~random_alloc;
            // if( (index = find_free_index_from_bitmap16_tail(normal_map)) != -1 ){
            //     // no modify on map status
            //     index = (index - rand_val + 16) % 16;
            //     new_section = alloc_section;
            //     // raise_bit(new_section.alloc_map_, new_section.frag_map_, index);    
            //     on_empty = false;
            // } else 
            if( (index = find_free_index_from_bitmap16_tail(empty_map)) != -1 ){
                // mark the empty map to allocated
                index = (index - rand_val + 16) % 16;
                new_section = alloc_section;
                raise_bit(new_section.alloc_map_, new_section.frag_map_, index);    
                on_empty = true;
            } 
            else if( use_chance && (index = find_free_index_from_bitmap16_tail(chance_map)) != -1 ){
                // mark the chance map to full
                index = (index - rand_val + 16) % 16;
                new_section = alloc_section;
                on_empty = false;
                // raise_bit(new_section.alloc_map_, new_section.frag_map_, index);
            } 
            else {
                return retry_time*(-1);
            }
        }while(!remote_CAS(*(uint64_t*)&new_section, (uint64_t*)&alloc_section, section_metadata_addr(section_offset), global_rkey_));
        region_e region_new;
        alloc_section = new_section;
        region_index = section_offset*region_per_section+index;
        // read region info
        remote_read(&alloc_region, sizeof(region_e), region_metadata_addr(region_index), global_rkey_);
        if (on_empty) {
            do {
                retry_time++;
                region_new = alloc_region;
                if(region_new.on_use_ == 1) {
                    printf("impossible problem: on_use is already set\n");
                    return retry_time*(-1);
                }
                region_new.on_use_ = 1;
            }while(!remote_CAS(*(uint64_t*)&region_new, (uint64_t*)&alloc_region, region_metadata_addr(region_index), global_rkey_));
        }
        return retry_time;
    } else {
        printf("use region alloc!\n");
    } 
    return 0;
}

// the core function of fetch a single block from a region
// the user must check the state if CAS failed, and must update state if some condition occurs
// I know this is ugly to convert client id as conn_id_+1 (why +1? because server give the number from 0, and I don't want to modify the RPC framework anymore :( 
int RDMAConnection::fetch_region_block(section_e &alloc_section, region_e &alloc_region, uint64_t &addr, uint32_t &rkey, bool is_exclusive, uint32_t region_index, uint16_t block_class) {
    int index, retry_time = 0; region_e new_region;
    
    // [Stage 0] flush log
    remote_read(&alloc_region, sizeof(region_e), region_metadata_addr(region_index), global_rkey_);
    if(alloc_region.last_modify_id_ != 0){
        if((alloc_region.base_map_ & ((uint32_t)1<<alloc_region.last_offset_)) == 1 ){
            // malloc
            block_e old_block = {0, (alloc_region.last_timestamp_ +126) % 127 + 1, 1<<alloc_region.num};
            block_e new_block = {node_id_, alloc_region.last_timestamp_, 1<<alloc_region.num};
            do {
                int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                if(old_block.client_id_ != 0 || outdate){
                    break;
                }
            } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * alloc_region.last_offset_, global_rkey_));
        } else {
            //free
            block_e old_block = {alloc_region.last_modify_id_, (alloc_region.last_timestamp_ +126) % 127 + 1, 1<<alloc_region.num};
            block_e new_block = {0, alloc_region.last_timestamp_, 1<<alloc_region.num};
            do {
                int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                if(old_block.client_id_ != alloc_region.last_modify_id_ || outdate ){
                    break;
                }
            } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * alloc_region.last_offset_, global_rkey_));
        }
    }
    // [Stage 1] region allocation
    do{
        retry_time++;
        if(alloc_region.exclusive_ != is_exclusive || alloc_region.on_use_ != 1) {
            // printf("Region not avaliable, addr = %lx, exclusive = %d, free_length = %u\n", get_region_addr(region_index), alloc_region.exclusive_, alloc_region.max_length_);
            // this fail will return to fetch a new section
            return retry_time*(-1);
        } 
        new_region = alloc_region;
        if(block_class == 0){
            if((index = find_free_index_from_bitmap32_tail(alloc_region.base_map_)) == -1) {
                force_update_section_state(alloc_section, region_index, alloc_full);
                return retry_time*(-1);
            }
            new_region.base_map_ |= (uint32_t)1<<index;
        }
        else if(block_class > 0){
            int block_num = 1<<(block_class);
            int size = 1<<(block_num+1);
            uint32_t search_map = alloc_region.base_map_;
            index = 0;
            while(search_map % size != 0 && index < 64){
                search_map >>= block_num;
                index += block_num;
            }
            if(index >= 32){
                if((index = find_free_index_from_bitmap32_tail(alloc_region.base_map_)) == -1) {
                   force_update_section_state(alloc_section, region_index, alloc_full);
                }
                return retry_time*(-1);
            }
            for(int i = 0; i < block_num; i++){
                new_region.base_map_ |= (uint32_t)1<<(index+i);
            }
        }
        retry_counter_ = new_region.retry_;
        new_region.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
        new_region.last_offset_ = index;
        if (alloc_region.base_map_ & ((uint32_t)1<<alloc_region.last_offset_) == 0) 
            new_region.last_timestamp_ = (new_region.last_timestamp_ + 1) % 127 + 1;
        new_region.last_modify_id_ = node_id_;
        new_region.num = block_class;
    } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&alloc_region, region_metadata_addr(region_index), global_rkey_));
    // if(mt()%1000 == 1){
    //     printf("stall happend\n");
    //     usleep(1000);
    // }
    alloc_region = new_region;
    addr = get_region_block_addr(region_index, index);
    rkey = get_region_block_rkey(region_index, index); 

    // retry counter for least 3 time allocation
    uint64_t old_retry = retry_counter_;
    retry_counter_ = retry_time;
    
    // [Stage 2] state update, will async it in the future
    if(alloc_region.base_map_ == bitmap32_filled) {
        force_update_section_state(alloc_section, region_index, alloc_full);
    } 

    else if(old_retry > 0 && retry_time < low_threshold) {
        force_update_section_state(alloc_section, region_index, alloc_light, alloc_heavy);
    } 
    else if(old_retry < 2 && retry_time >= retry_threshold) {
        force_update_section_state(alloc_section, region_index, alloc_heavy, alloc_light);
    } 
    // if(mt()%1000 == 1){
    //     printf("stall happend\n");
    //     usleep(1000);
    // }
    // [Stage 3] Flush log, will async it in the future
    block_e old_block = {0, (alloc_region.last_timestamp_ + 126 ) % 127 + 1};
    block_e new_block = {node_id_, alloc_region.last_timestamp_};
    bool out_date = false;
    do {
        int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
        bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
        if(old_block.client_id_ != 0 || out_date){
            // out-of-date update, skip
            // printf("other people done this: %d instead of %d\n", old_block.client_id_, node_id_);
            out_date = true;
            break;
        }
    } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_index * block_per_region + sizeof(uint64_t) * index, global_rkey_));
    // if(mt()%1000 == 1){
    //     printf("stall happend\n");
    //     usleep(1000);
    // }
    if(!out_date){
        new_region = alloc_region;
        new_region.last_modify_id_ = 0;
        // no matter true or false, no retry
        // if false, someone other must have done persistence, then do nothing 
        remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&alloc_region, region_metadata_addr(region_index), global_rkey_);
    }

    return retry_time;
}

int RDMAConnection::fetch_region_batch(section_e &alloc_section, region_e &alloc_region, mr_rdma_addr* addr, uint64_t num, bool is_exclusive, uint32_t region_index) {
    int index; region_e new_region;
    int free_item = 0;
    printf("?????????\n");
    do{
        if(alloc_region.exclusive_ != is_exclusive || alloc_region.on_use_ != 1) {
            printf("Region not avaliable, addr = %lx, exclusive = %d\n", get_region_addr(region_index), alloc_region.exclusive_);
            return 0;
        } 
        new_region = alloc_region;
        if((free_item = free_bit_in_bitmap32(alloc_region.base_map_)) == 0) {
            return 0;
        }
        free_item = num < free_item ? num : free_item;
        for(int i = 0; i < free_item; i ++) {
            index = find_free_index_from_bitmap32_tail(new_region.base_map_);    
            new_region.base_map_ |= (uint32_t)1<<index;
            addr[i].addr = index;
        }
        
        // update the max length info
        // old_length = new_region.max_length_;
        // new_length = max_longbit(new_region.base_map_);
        // new_region.max_length_ = new_length;

    } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&alloc_region, region_metadata_addr(region_index), global_rkey_));
    alloc_region = new_region;
    rkey_table_e rkey_list[block_per_region];
    fetch_exclusive_region_rkey(region_index, rkey_list);
    for(int i = 0; i < free_item; i++){
        addr[i].rkey = rkey_list[addr[i].addr].main_rkey_; 
        addr[i].addr = get_region_block_addr(region_index, addr[i].addr);    
    }

    // batch not see CAS competition as a critical problem
    if(alloc_region.base_map_ == bitmap32_filled) {
        force_update_section_state(alloc_section, region_index, alloc_full);
    }
    return free_item;
}

int RDMAConnection::free_region_batch(uint32_t region_offset, uint32_t free_bitmap, bool is_exclusive) {
    region_e region, new_region;
    int retry_time = 0;
    remote_read(&region, sizeof(region_e), region_metadata_addr(region_offset), global_rkey_);
    printf("?????????\n");
    if(!region.exclusive_ && is_exclusive) {
        printf("exclusive error, the actual block is shared\n");
        return -1;
    }
    uint32_t new_rkey;
    if((region.base_map_ & ~free_bitmap) == 0) {
        printf("already freed\n");
        return -1;
    } 
    do{
        retry_time++;
        new_region = region;
        new_region.base_map_ &= free_bitmap;
        if ( new_region.base_map_ == (bitmap32)0 ) {
            new_region.on_use_ = 0;
            new_region.exclusive_ = 0;
        }
    } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&region, region_metadata_addr(region_offset), global_rkey_));

    // retry counter for least 3 time allocation
    uint16_t old_retry = retry_counter_;
    retry_counter_ = retry_time;
    
    // concurrency state update, will async it in the future
    section_e alloc_section;
    if(!is_exclusive && new_region.base_map_ == (bitmap32)0 ){
        force_update_section_state(alloc_section, region_offset, alloc_empty); 
    } 
    // else if(old_retry < 10 && avg_retry >= 10) {
    //     force_update_section_state(alloc_section, region_offset, alloc_full);
    // } 
    // else if(old_retry < 5 && retry_time >= 5) {
    //     force_update_section_state(alloc_section, region_offset, alloc_heavy, alloc_light);
    // } 
    // else if(old_retry >= 5 && retry_time < 5) {
    //     force_update_section_state(alloc_section, region_offset, alloc_light, alloc_heavy);
    //     // printf("make region %d light\n", region_offset);
    // } 
    else {
        force_update_section_state(alloc_section, region_offset, alloc_light, alloc_full);
    }
    // else if(old_retry >= 10 && avg_retry < 10) {
    //     force_update_section_state(alloc_section, region_offset, alloc_heavy);
    // }

    region = new_region;
    return 0;
}

int RDMAConnection::free_region_block(uint64_t addr, bool is_exclusive, uint16_t block_class) {
    uint32_t region_offset = (addr - heap_start_) / region_size_;
    uint32_t region_block_offset = (addr - heap_start_) % region_size_ / block_size_;
    region_e region, new_region;
    int retry_time = 0;
    rebind_region_block_rkey(region_offset, region_block_offset);
    // [Stage 0] flush log
    remote_read(&region, sizeof(region_e), region_metadata_addr(region_offset), global_rkey_);
    if(!region.exclusive_ && is_exclusive) {
        printf("exclusive error, the actual block is shared\n");
        return -1;
    }
    if(region.last_modify_id_ != 0){
        if((region.base_map_ & ((uint32_t)1<<region.last_offset_)) == 1 ){
            // malloc
            block_e old_block = {0, (region.last_timestamp_ +126) % 127 + 1};
            block_e new_block = {node_id_, region.last_timestamp_};
            do {
                int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                if(old_block.client_id_ != 0 || outdate){
                    break;
                }
            } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region_block_offset, global_rkey_));
        } else {
            //free
            block_e old_block = {region.last_modify_id_, (region.last_timestamp_ +126) % 127 + 1};
            block_e new_block = {0, region.last_timestamp_};
            do {
                int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
                bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
                if(old_block.client_id_ != region.last_modify_id_ || outdate){
                    break;
                }
            } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region_block_offset, global_rkey_));
        }
    }

    bool full = (region.base_map_ == bitmap32_filled);
    uint32_t new_rkey;
    uint32_t mask = 0;
    for (int i = 0; i<(1<<block_class); i++){
        mask += (uint32_t)1<<i;
    }
    if((region.base_map_ & ((uint32_t)mask<<region_block_offset)) == 0) {
        printf("already freed\n");
        return -1;
    } 
    do{
        full = (region.base_map_ == bitmap32_filled);
        retry_time++;
        new_region = region;
        new_region.base_map_ &= ~(uint32_t)((uint32_t)mask<<region_block_offset);
        if ( new_region.base_map_ == (bitmap32)0 ) {
            new_region.on_use_ = 0;
            new_region.exclusive_ = 0;
        }
        retry_counter_ = new_region.retry_;
        new_region.retry_ = (retry_time>=retry_threshold)? 2: ((retry_time >= low_threshold)? 1:0);
        if (region.base_map_ & ((uint32_t)1<<region.last_offset_) == 1) 
            new_region.last_timestamp_ = (new_region.last_timestamp_ + 1) % 127 + 1;
        new_region.last_modify_id_ = node_id_;
        new_region.last_offset_ = region_block_offset;
    } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&region, region_metadata_addr(region_offset), global_rkey_));

    uint16_t old_retry = retry_counter_;
    retry_counter_ = retry_time;
    
    // concurrency state update, will async it in the future
    section_e alloc_section;
    if(!is_exclusive && new_region.base_map_ == (bitmap32)0 ){
        force_update_section_state(alloc_section, region_offset, alloc_empty); 
    } 
    else if(full) {
        force_update_section_state(alloc_section, region_offset, alloc_light, alloc_full);
    }
    else if(old_retry > 0 && retry_time < low_threshold) {
    // else if(old_retry < low_threshold && retry_time >= low_threshold) {
        force_update_section_state(alloc_section, region_offset, alloc_light, alloc_heavy);
        // printf("make region %d heavy\n", region_offset);
    } 
    else if(old_retry < 2 && retry_time >= retry_threshold) {
    // else if(old_retry >= retry_threshold && retry_time < retry_threshold) {
        force_update_section_state(alloc_section, region_offset, alloc_heavy, alloc_light);
        // printf("make region %d light\n", region_offset);
    } 
    else {
        force_update_section_state(alloc_section, region_offset, alloc_light, alloc_full);
    }

    block_e old_block = {node_id_, (region.last_timestamp_ + 126 ) % 127 + 1};
    block_e new_block = {0, region.last_timestamp_};
    bool out_date = false;
    do {
        int distant = abs((long)old_block.timestamp_ - (long)new_block.timestamp_);
        bool outdate = distant > 64 ? (old_block.timestamp_ <= new_block.timestamp_) : (old_block.timestamp_ >= new_block.timestamp_);
        if(old_block.client_id_ != region.last_modify_id_ || outdate){
            // out-of-date update, skip
            out_date = true;
            // printf("other people done this\n");
            break;
        }
    } while(!remote_CAS(*(uint64_t*)&new_block, (uint64_t*)&old_block, block_header_ + sizeof(uint64_t) * region_offset * block_per_region + sizeof(uint64_t) * region_block_offset, global_rkey_));
    // if(mt()%1000 == 1){
    //     printf("stall happend\n");
    //     usleep(1000);
    // }
    if(!out_date){
        new_region = region;
        new_region.last_modify_id_ = 0;
        // no matter true or false, no retry
        // if false, someone other must have done persistence, then do nothing 
        remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&region, region_metadata_addr(region_offset), global_rkey_);
    }

    region = new_region;
    return 0;
}

// CXL-SHM: using CAS to fetch memory block directly
int RDMAConnection::fetch_block(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey, uint16_t size_class) {
    int length = 1 << size_class;
    block_hint = block_hint % (block_num_/length);
    uint64_t old_header = 0, new_header = 1, hint = block_hint;
    uint16_t counter = 0;
    int retry_time = 1;
    uint64_t header[length];
    while(true){
        remote_read(header, length*sizeof(uint64_t), block_header_ + length*hint * sizeof(uint64_t), global_rkey_);
        for(int offset = 0; offset < 1; offset++) {
            bool find = true;
            for(int i = offset*length; i < offset*length+length; i++){
                if(header[i]!=0) {
                    find = false;
                    break;
                }
            }
            if(find){
                bool success = true;
                int stop = 0;
                new_header = 1; old_header = 0;
                for(int i = offset*length; i < offset*length + length; i++){
                retry_time ++;
                    if(!remote_CAS(*(uint64_t*)&new_header, (uint64_t*)&old_header, block_header_ + (length*hint+i) * sizeof(uint64_t), global_rkey_)) {
                        success = false;
                        stop = i;
                        break;
                    }
                }
                if(!success){
                    new_header = 0; old_header = 1;
                    for(int i = offset*length; i < stop; i++){
                        retry_time ++;
                        if(!remote_CAS(*(uint64_t*)&new_header, (uint64_t*)&old_header, block_header_ + (length*hint+i) * sizeof(uint64_t), global_rkey_)){
                            printf("impossbile!\n");
                        }
                    }
                }else {
                    addr = get_block_addr(length*hint + offset*length);
                    rkey = get_block_rkey(length*hint + offset*length);
                    block_hint = (hint + 1) % (block_num_/length);
                    return retry_time;
                }            
            }
        }
        hint = (hint + 1) % (block_num_/length);
        if(hint == block_hint) {
            counter ++;
            if(counter >2) {
                return 0;
            }
        }
    }
    // addr = get_block_addr(hint);
    // rkey = get_block_rkey(hint);
    // block_hint = (hint + 1) % block_num_;
    return 0;
}

int RDMAConnection::free_block(uint64_t addr, uint16_t size_class) {
    uint64_t old_header = 1, new_header = 0;
    uint64_t index = (addr - heap_start_) / block_size_;
    for(int i = 0; i < (1<<size_class); i++ ){
        if(!remote_CAS(*(uint64_t*)&new_header, (uint64_t*)&old_header, block_header_ + (index+i) * sizeof(uint64_t), global_rkey_)){
            return false;
        }
    }
    return true;
}


int RDMAConnection::fetch_block_bitmap(uint64_t &block_hint, uint64_t &addr, uint32_t &rkey) {
    uint64_t old_header = 0, new_header = 1, hint = block_hint % region_num_;
    uint16_t counter = 0;
    int retry_time = 0;
    region_e alloc_region, new_region;
    remote_read(&alloc_region, sizeof(region_e), region_metadata_addr(hint), global_rkey_);
    int index;
    do{
        retry_time++;
        while((index = find_free_index_from_bitmap32_tail(alloc_region.base_map_)) == -1) {
            hint = (hint + 1) % region_num_;
            retry_time++;
            remote_read(&alloc_region, sizeof(region_e), region_metadata_addr(hint), global_rkey_);
        }
        new_region = alloc_region;
        new_region.base_map_ |= (uint32_t)1<<index;
    } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&alloc_region, region_metadata_addr(hint), global_rkey_));
    // if(mt()%1000 == 1){
    //     printf("stall happend\n");
    //     usleep(1000);
    // }
    alloc_region = new_region;
    addr = get_region_block_addr(hint, index);
    rkey = get_region_block_rkey(hint, index);
    block_hint = hint; 
    return retry_time;
}

int RDMAConnection::free_block_bitmap(uint64_t addr) {
    uint32_t region_offset = (addr - heap_start_) / region_size_;
    uint32_t region_block_offset = (addr - heap_start_) % region_size_ / block_size_;
    region_e region, new_region;
    remote_read(&region, sizeof(region_e), region_metadata_addr(region_offset), global_rkey_);
    bool full;
    uint64_t old_header = 1, new_header = 0;
    if((region.base_map_ & ((uint32_t)1<<region_block_offset)) == 0) {
        printf("already freed\n");
        return true;
    } 
    do{
        new_region = region;
        new_region.base_map_ &= ~(uint32_t)((uint32_t)1<<region_block_offset);
    } while(!remote_CAS(*(uint64_t*)&new_region, (uint64_t*)&region, region_metadata_addr(region_offset), global_rkey_));
    return true;
}

}  // namespace kv
