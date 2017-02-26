#include "rdma_common.h"

static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_server_id = NULL, *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp = NULL;

static int setup_client_resources(void)
{
    int ret = -1;
    if(!cm_client_id){
        rdma_error("Client id is still NULL \n");
        return -EINVAL;
    }

    pd = ibv_alloc_pd(cm_client_id->verbs);
    if (!pd) {
        rdma_error("Failed to allocate a protection domain errno: %d\n",
                -errno);
        return -errno;
    }
    rdma_debug("A new protection domain is allocated at %p \n", pd);

    io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
    if (!io_completion_channel) {
        rdma_error("Failed to create an I/O completion event channel, %d\n", -errno);
        return -errno;
    }
    rdma_debug("An I/O completion event channel is created at %p \n", io_completion_channel);

    cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, NULL, io_completion_channel, 0);
    if (!cq) {
        rdma_error("Failed to create a completion queue (cq), errno: %d\n",
                -errno);
        return -errno;
    }
    rdma_debug("Completion queue (CQ) is created at %p with %d elements \n", cq, cq->cqe);

    ret = ibv_req_notify_cq(cq, 0);
    if (ret) {
        rdma_error("Failed to request notifications on CQ errno: %d \n", -errno);
        return -errno;
    }

    bzero(&qp_init_attr, sizeof qp_init_attr);
    qp_init_attr.cap.max_recv_sge = MAX_SGE;
    qp_init_attr.cap.max_recv_wr = MAX_WR;
    qp_init_attr.cap.max_send_sge = MAX_SGE;
    qp_init_attr.cap.max_send_wr = MAX_WR;
    qp_init_attr.qp_type = IBV_QPT_RC;

    qp_init_attr.recv_cq = cq;
    qp_init_attr.send_cq = cq;
    /*Lets create a QP */
    ret = rdma_create_qp(cm_client_id,pd,&qp_init_attr);
    if (ret) {
        rdma_error("Failed to create QP due to errno: %d\n", -errno);
        return -errno;
    }

    client_qp = cm_client_id->qp;
    rdma_debug("Client QP created at %p\n", client_qp);
    return ret;
}


static int start_rdma_server(struct sockaddr_in *server_addr) 
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /*  Open a channel used to report asynchronous communication event */
    cm_event_channel = rdma_create_event_channel();
    if (!cm_event_channel) {
        rdma_error("Creating cm event channel failed with errno : (%d)", -errno);
        return -errno;
    }
    rdma_debug("RDMA CM event channel is created successfully at %p \n", 
            cm_event_channel);

    ret = rdma_create_id(cm_event_channel, &cm_server_id, NULL, RDMA_PS_TCP);
    if (ret) {
        rdma_error("Creating server cm id failed with errno: %d ", -errno);
        return -errno;
    }
    rdma_debug("A RDMA connection id for the server is created \n");

    ret = rdma_bind_addr(cm_server_id, (struct sockaddr*) server_addr);
    if (ret) {
        rdma_error("Failed to bind server address, errno: %d \n", -errno);
        return -errno;
    }
    rdma_debug("Server RDMA CM id is successfully binded \n");

    ret = rdma_listen(cm_server_id, 8);
    if (ret) {
        rdma_error("rdma_listen failed to listen on server address, errno: %d ", -errno);
        return -errno;
    }
    printf("Server is listening successfully at: %s , port: %d \n", inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port));

    ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_CONNECT_REQUEST, &cm_event);
    if (ret) {
        rdma_error("Failed to get cm event, ret = %d \n" , ret);
        return ret;
    }

    cm_client_id = cm_event->id;

    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
        return -errno;
    }
    rdma_debug("A new RDMA client connection id is stored at %p\n", cm_client_id);
    return ret;
}

/* Pre-posts a receive buffer and accepts an RDMA client connection */
static int accept_client_connection(void)
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr; 
    int ret = -1;
    if(!cm_client_id || !client_qp) {
        rdma_error("Client resources are not properly setup\n");
        return -EINVAL;
    }

    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth = 3;

    conn_param.responder_resources = 3;
    ret = rdma_accept(cm_client_id, &conn_param);
    if (ret) {
        rdma_error("Failed to accept the connection, errno: %d \n", -errno);
        return -errno;
    }

    rdma_debug("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");
    ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
    if (ret) {
        rdma_error("Failed to get the cm event, errnp: %d \n", -errno);
        return -errno;
    }
    /* We acknowledge the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the cm event %d\n", -errno);
        return -errno;
    }

    memcpy(&remote_sockaddr, rdma_get_peer_addr(cm_client_id), sizeof(struct sockaddr_in));
    printf("A new connection is accepted from %s \n", inet_ntoa(remote_sockaddr.sin_addr));
    return ret;
}


static int disconnect_and_cleanup(void)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;

    rdma_debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");
    ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);

    if (ret) {
        rdma_error("Failed to get disconnect event, ret = %d \n", ret);
        return ret;
    }

    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the cm event %d\n", -errno);
        return -errno;
    }
    printf("A disconnect event is received from the client...\n");

    rdma_destroy_qp(cm_client_id);

    ret = rdma_destroy_id(cm_client_id);
    if (ret) {
        rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
    }

    ret = ibv_destroy_cq(cq);
    if (ret) {
        rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
    }

    ret = ibv_destroy_comp_channel(io_completion_channel);
    if (ret) {
        rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
    }

    ret = ibv_dealloc_pd(pd);
    if (ret) {
        rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
    }

    ret = rdma_destroy_id(cm_server_id);
    if (ret) {
        rdma_error("Failed to destroy server id cleanly, %d \n", -errno);
    }
    rdma_destroy_event_channel(cm_event_channel);
    printf("Server shut-down is complete \n");
    return 0;
}

int mc_start_incoming_migration(void)
{
    int ret;
    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof server_sockaddr);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(!server_sockaddr.sin_port) {
        server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
     }
    ret = start_rdma_server(&server_sockaddr);
    if (ret) {
        rdma_error("RDMA server failed to start cleanly, ret = %d \n", ret);
        return ret;
    }
    ret = setup_client_resources();
    if (ret) { 
        rdma_error("Failed to setup client resources, ret = %d \n", ret);
        return ret;
    }
    ret = accept_client_connection();
    if (ret) {
        rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
        return ret;
    }
    return 0;
}
