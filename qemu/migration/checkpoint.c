#include <libnl3/netlink/route/qdisc/plug.h>
#include <libnl3/netlink/route/class.h>
#include <libnl3/netlink/cli/utils.h>
#include <libnl3/netlink/cli/tc.h>
#include <libnl3/netlink/cli/qdisc.h>
#include <libnl3/netlink/cli/link.h>

#include "checkpoint.h"

#ifndef rtnl_tc_get_ops
extern struct rtnl_tc_ops * rtnl_tc_get_ops(struct rtnl_tc *);
#endif

#define MC_DEV_NAME_MAX_SIZE    256

static struct rtnl_qdisc        *qdisc      = NULL;
static struct nl_sock           *sock       = NULL;
static struct rtnl_tc           *tc         = NULL;
static struct nl_cache          *link_cache = NULL;
static struct rtnl_tc_ops       *ops        = NULL;
static struct nl_cli_tc_module  *tm         = NULL;
static int first_nic_chosen = 0;

#define START_BUFFER (1000*1000*1000 / 8)
static int buffer_size = START_BUFFER, new_buffer_size = START_BUFFER;
static const char * parent = "root";
static bool buffering_enabled = false;
static const char * BUFFER_NIC_PREFIX = "ifb";

static int mc_deliver(int update)
{
    int err, flags = NLM_F_CREATE | NLM_F_REPLACE;

    if (!buffering_enabled) {
        return 1;
    }

    if (!update)
        flags |= NLM_F_EXCL;

    if ((err = rtnl_qdisc_add(sock, qdisc, flags)) < 0) {
        fprintf(stderr, "Unable to control qdisc: %s! %p %p %d\n",
            nl_geterror(err), sock, qdisc, flags);
        return -EINVAL;
    }

    return 0;
}

static int mc_set_buffer_size(int size)
{
    int err;

    if (!buffering_enabled) {
        return 1;
    }

    buffer_size = size;
    new_buffer_size = size;

    if ((err = rtnl_qdisc_plug_set_limit((void *) qdisc, size)) < 0) {
       fprintf(stderr, "MC: Unable to change buffer size: %s\n",
			nl_geterror(err));
       return -EINVAL;
    }

    DPRINTF("Set buffer size to %d bytes\n", size);

    return mc_deliver(1);
}

/*
 * Micro-checkpointing may require buffering network packets.
 * Set that up for the first NIC only.... We'll worry about
 * multiple NICs later.
 */
static void init_mc_nic_buffering(NICState *nic, void *opaque)
{
    char * device = opaque;
    NetClientState * nc = &nic->ncs[0];
    const char * key = "ifname=";
    int keylen = strlen(key);
    char * name;
    int end = 0;
    bool use_fd = false;

    if (first_nic_chosen) {
         fprintf(stderr, "Micro-Checkpointing with multiple NICs not yet supported!\n");
         return;
    }

    if (!nc->peer) {
        fprintf(stderr, "Micro-Checkpoint nic %s does not have peer host device for buffering. VM will not be consistent.\n", nc->name);
        return;
    }

    name = nc->peer->info_str;

    DPRINTF("Checking contents of device [%s] (%s)\n", name, nc->name);

    if (strncmp(name, key, keylen)) {
        fprintf(stderr, "Micro-Checkpoint nic %s does not have 'ifname' "
                        "in its description (%s, %s). Trying workaround...\n",
                        nc->name, name, nc->peer->name);
        key = "fd=";
        keylen = strlen(key);
        if (strncmp(name, key, keylen)) {
            fprintf(stderr, "Still cannot find 'fd=' either. Failure.\n");
            return;
        }

        use_fd = true;
    }

    name += keylen;

    while (name[end++] != (use_fd ? '\0' : ','));

    strncpy(device, name, end - 1);
    memset(&device[end - 1], 0, MC_DEV_NAME_MAX_SIZE - (end - 1));

    if (use_fd) {
        struct ifreq r;
        DPRINTF("Want to retreive name from fd: %d\n", atoi(device));

        if (ioctl(atoi(device), TUNGETIFF, &r) == -1) {
            fprintf(stderr, "Failed to convert fd %s to name.\n", device);
            return;
        }

        DPRINTF("Got name %s!\n", r.ifr_name);
        strcpy(device, r.ifr_name);
    }

    first_nic_chosen = 1;
}

static int mc_suspend_buffering(void)
{
    int err;

    if (!buffering_enabled) {
        return 1;
    }

    if ((err = rtnl_qdisc_plug_release_indefinite((void *) qdisc)) < 0) {
        fprintf(stderr, "MC: Unable to release indefinite: %s\n",
            nl_geterror(err));
        return -EINVAL;
    }

    DPRINTF("Buffering suspended\n");

    return mc_deliver(1);
}

static int mc_disable_buffering(void)
{
    int err;

    if (!buffering_enabled) {
        goto out;
    }

    mc_suspend_buffering();

    if (qdisc && sock && (err = rtnl_qdisc_delete(sock, (void *) qdisc)) < 0) {
        fprintf(stderr, "Unable to release indefinite: %s\n", nl_geterror(err));
    }

out:
    buffering_enabled = false;
    qdisc = NULL;
    sock = NULL;
    tc = NULL;
    link_cache = NULL;
    ops = NULL;
    tm = NULL;

    DPRINTF("Buffering disabled\n");

    return 0;
}

/*
 * Install a Qdisc plug for micro-checkpointing.
 * If it exists already (say, from a previous dead VM or debugging
 * session) then just open all the netlink data structures pointing
 * to the existing plug and replace it.
 *
 * Also, if there is no network device to begin with, then just
 * silently return with buffering_enabled = false.
 */
int mc_enable_buffering(void)
{
    char dev[MC_DEV_NAME_MAX_SIZE], buffer_dev[MC_DEV_NAME_MAX_SIZE];
    int prefix_len = 0;
    int buffer_prefix_len = strlen(BUFFER_NIC_PREFIX);

    if (buffering_enabled) {
        fprintf(stderr, "Buffering already enable Skipping.\n");
        return 0;
    }

    first_nic_chosen = 0;

    qemu_foreach_nic(init_mc_nic_buffering, dev);

    if (!first_nic_chosen) {
        fprintf(stderr, "MC ERROR: No network devices available."
                " Disabling buffering.\n");
        return 1;
    }

    while ((dev[prefix_len] < '0') || (dev[prefix_len] > '9'))
        prefix_len++;

    strcpy(buffer_dev, BUFFER_NIC_PREFIX);
    strncpy(buffer_dev + buffer_prefix_len,
                dev + prefix_len, strlen(dev) - prefix_len + 1);

    fprintf(stderr, "Initializing buffering for nic %s => %s\n", dev, buffer_dev);

    if (sock == NULL) {
        sock = (struct nl_sock *) nl_cli_alloc_socket();
        if (!sock) {
            fprintf(stderr, "MC: failed to allocate netlink socket\n");
            goto failed;
        }
		nl_cli_connect(sock, NETLINK_ROUTE);
    }

    if (qdisc == NULL) {
        qdisc = nl_cli_qdisc_alloc();
        if (!qdisc) {
            fprintf(stderr, "MC: failed to allocate netlink qdisc\n");
            goto failed;
        }
        tc = (struct rtnl_tc *) qdisc;
    }

    if (link_cache == NULL) {
		link_cache = nl_cli_link_alloc_cache(sock);
        if (!link_cache) {
            fprintf(stderr, "MC: failed to allocate netlink link_cache\n");
            goto failed;
        }
    }

    nl_cli_tc_parse_dev(tc, link_cache, (char *) buffer_dev);
    nl_cli_tc_parse_parent(tc, (char *) parent);

    if (!rtnl_tc_get_ifindex(tc)) {
        fprintf(stderr, "Qdisc device '%s' does not exist!\n", buffer_dev);
        goto failed;
    }

    if (!rtnl_tc_get_parent(tc)) {
        fprintf(stderr, "Qdisc parent '%s' is not valid!\n", parent);
        goto failed;
    }

    if (rtnl_tc_set_kind(tc, "plug") < 0) {
        fprintf(stderr, "Could not open qdisc plug!\n");
        goto failed;
    }

    if (!(ops = rtnl_tc_get_ops(tc))) {
        fprintf(stderr, "Could not open qdisc plug!\n");
        goto failed;
    }

    if (!(tm = nl_cli_tc_lookup(ops))) {
        fprintf(stderr, "Qdisc plug not supported!\n");
        goto failed;
    }

    buffering_enabled = true;

    if (mc_deliver(0) < 0) {
		fprintf(stderr, "First time qdisc create failed\n");
		goto failed;
    }

    DPRINTF("Buffering enabled, size: %d MB.\n", buffer_size / 1024 / 1024);

    if (mc_set_buffer_size(buffer_size) < 0) {
		goto failed;
	}

    if (mc_suspend_buffering() < 0) {
		goto failed;
	}


    return 0;

failed:
    mc_disable_buffering();
    return -EINVAL;
}

int mc_start_buffer(void)
{
    int err;

    if (!buffering_enabled) {
        return 0;
    }

    if (new_buffer_size != buffer_size) {
        buffer_size = new_buffer_size;
        DPRINTF("MC setting new buffer size to %d\n", buffer_size);
        if (mc_set_buffer_size(buffer_size) < 0)
            return -EINVAL;
    }

    if ((err = rtnl_qdisc_plug_buffer((void *) qdisc)) < 0) {
        fprintf(stderr, "Unable to flush oldest checkpoint: %s\n", nl_geterror(err));
        return -EINVAL;
    }

    DDPRINTF("Inserted checkpoint barrier\n");

    return mc_deliver(1);
}

int mc_flush_oldest_buffer(void)
{
    int err;

    if (!buffering_enabled) {
        return 0;
    }

    if ((err = rtnl_qdisc_plug_release_one((void *) qdisc)) < 0) {
        fprintf(stderr, "Unable to flush oldest checkpoint: %s\n", nl_geterror(err));
        return -EINVAL;
    }

    DDPRINTF("Flushed oldest checkpoint barrier\n");

    return mc_deliver(1);
}

/* ================================================================== */
/* RDMA init */
#include <infiniband/verbs.h>
#include <byteswap.h>

#define RDMA_MERGE_MAX (2 * 1024 * 1024)
#define RDMA_SIGNALED_SEND_MAX (RDMA_MERGE_MAX / 4096)

#define RDMA_CONTROL_MAX_BUFFER (512 * 1024)

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x)
{
    return bswap_64(x);
}

static inline uint64_t ntohll(uint64_t x)
{
    return bswap_64(x);
}
#elif __BYTE_ORDER == __BIG_ENDIAN

static inline uint64_t htonll(uint64_t x)
{
    return x;
}

static inline uint64_t ntohll(uint64_t x)
{
    return x;
}
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

struct config_t
{
    const char *dev_name;
    char *server_name; 
    u_int32_t tcp_port;
    int ib_port;
    int gid_idx;
};

static struct config_t config = {
    NULL,
    NULL,
    19875,
    1,
    -1
};

struct cm_con_data_t
{
    uint32_t qp_num;
    uint16_t lid;
    uint8_t gid[16];
} __attribute__ ((packed));

#define RDMA_WRID_BLOCK_SHIFT 16UL

#define RDMA_WRID_TYPE_MASK \
    ((1UL << RDMA_WRID_BLOCK_SHIFT) - 1UL)

enum {
    RDMA_WRID_NONE = 0,
    RDMA_WRID_SEND_CONTROL = 2000,
    RDMA_WRID_RECV_CONTROL = 4000,
};

enum {
    RDMA_CONTROL_NONE = 0,
    RDMA_CONTROL_ERROR,
    RDMA_CONTROL_READY,
    RDMA_CONTROL_QEMU_FILE,
};

static const char *control_desc[] = {
    [RDMA_CONTROL_NONE] = "NONE",
    [RDMA_CONTROL_ERROR] = "ERROR",
    [RDMA_CONTROL_READY] = "READY",
    [RDMA_CONTROL_QEMU_FILE] = "QEMU FILE",
};

typedef struct QEMU_PACKED {
    uint32_t len;     /* Total length of data portion */
    uint32_t type;    /* which control command to perform */
} MC_RDMAControlHeader;

typedef struct {
    uint8_t  control[RDMA_CONTROL_MAX_BUFFER];
    struct   ibv_mr *control_mr;              
    size_t   control_len;                    
} MC_RDMAWorkRequestData;

enum {
    RDMA_WRID_READY = 0,
    RDMA_WRID_DATA,
    RDMA_WRID_CONTROL,
    RDMA_WRID_MAX,
};

typedef struct MC_RDMAContext {
    struct ibv_context *ib_ctx;
    struct ibv_port_attr port_attr;
    struct ibv_device_attr device_attr;
    struct cm_con_data_t remote_props;
    int sock;

    MC_RDMAWorkRequestData wr_data[RDMA_WRID_MAX];

    /*
     * This is used by *_exchange_send() to figure out whether or not
     * the initial "READY" message has already been received or not.
     */
    int control_ready_expected;

    int total_registrations;

    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp;

} MC_RDMAContext;

static MC_RDMAContext *rdma;

static int sock_connect(const char *servername, int port)
{
    struct addrinfo *resolved_addr = NULL;
    struct addrinfo *iterator;
    char service[6];
    int sockfd = -1;
    int listenfd = 0;
    int tmp;

    struct addrinfo hints =
    {
        .ai_flags = AI_PASSIVE,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    if (sprintf(service, "%d", port) < 0)
        goto sock_connect_exit;

    sockfd = getaddrinfo(servername, service, &hints, &resolved_addr);

    if (sockfd < 0)
    {
        fprintf(stderr, "%s for %s:%d\n", gai_strerror (sockfd), servername, port);
        goto sock_connect_exit;
    }


    /* Search through results and find the one we want */
    for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
    {
        sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        if (sockfd >= 0)
        {
            if (servername)
            {
                if ((tmp = connect (sockfd, iterator->ai_addr, iterator->ai_addrlen)))
                {
                    fprintf (stdout, "failed connect \n");
                    close (sockfd);
                    sockfd = -1;
                }
            }
            else
            {
                listenfd = sockfd;
                sockfd = -1;
                if (bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
                    goto sock_connect_exit;
                listen(listenfd, 1);
                sockfd = accept(listenfd, NULL, 0);
            }
        }
    }

sock_connect_exit:

    if (listenfd)
        close(listenfd);

    if (resolved_addr)
        freeaddrinfo(resolved_addr);

    if (sockfd < 0)
    {
        if (servername)
            fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
        else
        {
            perror("server accept");
            fprintf(stderr, "accept() failed\n");
        }
    }
    return sockfd;
}

static int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
{
    int rc;
    int read_bytes = 0;
    int total_read_bytes = 0;
    rc = write(sock, local_data, xfer_size);
    if (rc < xfer_size)
        fprintf (stderr, "Failed writing data during sock_sync_data\n");
    else
        rc = 0;
    while(!rc && total_read_bytes < xfer_size)
    {
        read_bytes = read(sock, remote_data, xfer_size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            rc = read_bytes;
    }
    return rc;
}


static void resources_init(void)
{
    memset(rdma, 0, sizeof(MC_RDMAContext));
    rdma->sock = -1;
}

static int mc_rdma_reg_control(int idx)
{
    rdma->wr_data[idx].control_mr = ibv_reg_mr(rdma->pd, rdma->wr_data[idx].control, RDMA_CONTROL_MAX_BUFFER, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (rdma->wr_data[idx].control_mr) {
        rdma->total_registrations++;
        return 0;
    }
    fprintf(stderr, "qemu_rdma_reg_control failed");
    return -1;
}

static int resources_create(void)
{
    struct ibv_device **dev_list = NULL;
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_device *ib_dev = NULL;
    int i;
    int num_devices;
    int rc = 0;

    if (config.server_name)
    {
        rdma->sock = sock_connect(config.server_name, config.tcp_port);
        if (rdma->sock < 0)
        {
            fprintf(stderr, "failed to establish TCP connection to server %s, port %d\n", config.server_name, config.tcp_port);
            rc = -1;
            goto resources_create_exit;
        }
        rdma->control_ready_expected = 1;
    }
    else
    {
        fprintf(stdout, "waiting on port %d for TCP connection\n", config.tcp_port);
        rdma->sock = sock_connect(NULL, config.tcp_port);
        if (rdma->sock < 0)
        {
            fprintf(stderr, "failed to establish TCP connection with client on port %d\n", config.tcp_port);
            rc = -1;
            goto resources_create_exit;
        }
    }
    fprintf(stdout, "TCP connection was established\n");
    fprintf(stdout, "searching for IB devices in host\n");

    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list)
    {
        fprintf(stderr, "failed to get IB devices list\n");
        rc = 1;
        goto resources_create_exit;
    }

    if (!num_devices)
    {
        fprintf(stderr, "found %d device(s)\n", num_devices);
        rc = 1;
        goto resources_create_exit;
    }
    fprintf (stdout, "found %d device(s)\n", num_devices);

    for (i = 0; i < num_devices; i++)
    {
        if (!config.dev_name)
        {
            config.dev_name = strdup(ibv_get_device_name(dev_list[i]));
            fprintf(stdout, "device not specified, using first one found: %s\n", config.dev_name);
        }
        if (!strcmp(ibv_get_device_name(dev_list[i]), config.dev_name))
        {
            ib_dev = dev_list[i];
            break;
        }
    }

    if (!ib_dev)
    {
        fprintf(stderr, "IB device %s wasn't found\n", config.dev_name);
        rc = 1;
        goto resources_create_exit;
    }

    rdma->ib_ctx = ibv_open_device(ib_dev);
    if (!rdma->ib_ctx)
    {
        fprintf(stderr, "failed to open device %s\n", config.dev_name);
        rc = 1;
        goto resources_create_exit;
    }

    ibv_free_device_list(dev_list);
    dev_list = NULL;
    ib_dev = NULL;

    if (ibv_query_port(rdma->ib_ctx, config.ib_port, &rdma->port_attr))
    {
        fprintf (stderr, "ibv_query_port on port %u failed\n", config.ib_port);
        rc = 1;
        goto resources_create_exit;
    }

    rdma->pd = ibv_alloc_pd(rdma->ib_ctx);
    if (!rdma->pd)
    {
        fprintf (stderr, "ibv_alloc_pd failed\n");
        rc = 1;
        goto resources_create_exit;
    }

    rdma->cq = ibv_create_cq(rdma->ib_ctx, (RDMA_SIGNALED_SEND_MAX * 3), NULL, NULL, 0);
    if (!rdma->cq)
    {
        fprintf (stderr, "failed to create CQ with %u entries\n", (RDMA_SIGNALED_SEND_MAX * 3));
        rc = 1;
        goto resources_create_exit;
    }

    int ret, idx;
    for (idx = 0; idx < RDMA_WRID_MAX; idx++) {
        ret = mc_rdma_reg_control(idx);
        if (ret) {
            fprintf(stderr, "registering %d control!", idx);
        }
    }

    /* create the Queue Pair */
    memset(&qp_init_attr, 0, sizeof (qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.send_cq = rdma->cq;
    qp_init_attr.recv_cq = rdma->cq;
    qp_init_attr.cap.max_send_wr = RDMA_SIGNALED_SEND_MAX;
    qp_init_attr.cap.max_recv_wr = 3;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    rdma->qp = ibv_create_qp(rdma->pd, &qp_init_attr);
    if (!rdma->qp)
    {
        fprintf(stderr, "failed to create QP\n");
        rc = 1;
        goto resources_create_exit;
    }
    fprintf(stdout, "QP was created, QP number=0x%x\n", rdma->qp->qp_num);

resources_create_exit:
    if (rc)
    {
        /* Error encountered, cleanup */
        if (rdma->qp)
        {
            ibv_destroy_qp(rdma->qp);
            rdma->qp = NULL;
        }
        if (rdma->cq)
        {
            ibv_destroy_cq(rdma->cq);
            rdma->cq = NULL;
        }
        if (rdma->pd)
        {
            ibv_dealloc_pd(rdma->pd);
            rdma->pd = NULL;
        }
        if (rdma->ib_ctx)
        {
            ibv_close_device(rdma->ib_ctx);
            rdma->ib_ctx = NULL;
        }
        if (dev_list)
        {
            ibv_free_device_list(dev_list);
            dev_list = NULL;
        }
        if (rdma->sock >= 0)
        {
            if (close(rdma->sock))
                fprintf(stderr, "failed to close socket\n");
            rdma->sock = -1;
        }
    }
    return rc;
}

static int modify_qp_to_init(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = config.ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE;
    flags =
        IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
        fprintf(stderr, "failed to modify QP state to INIT\n");
    return rc;
}

static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t * dgid)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset (&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_RTR;
    //attr.path_mtu = IBV_MTU_256;
    attr.path_mtu = rdma->port_attr.active_mtu;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 0x12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = config.ib_port;
    if (config.gid_idx >= 0)
    {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = 1;
        memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
        attr.ah_attr.grh.flow_label = 0;
        attr.ah_attr.grh.hop_limit = 0xFF;
        attr.ah_attr.grh.sgid_index = config.gid_idx;
        attr.ah_attr.grh.traffic_class = 0;
    }
    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
        fprintf(stderr, "failed to modify QP state to RTR\n");
    return rc;
}

static int modify_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 0x12;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
        IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
        fprintf (stderr, "failed to modify QP state to RTS\n");
    return rc;
}

static int connect_qp(void)
{
    struct cm_con_data_t local_con_data;
    struct cm_con_data_t remote_con_data;
    struct cm_con_data_t tmp_con_data;
    int rc = 0;

    union ibv_gid my_gid;
    if (config.gid_idx >= 0)
    {
        rc = ibv_query_gid(rdma->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
        if (rc)
        {
            fprintf (stderr, "could not get gid for port %d, index %d\n", config.ib_port, config.gid_idx);
            return rc;
        }
    }
    else
        memset(&my_gid, 0, sizeof my_gid);

    local_con_data.qp_num = htonl(rdma->qp->qp_num);
    local_con_data.lid = htons(rdma->port_attr.lid);
    memcpy(local_con_data.gid, &my_gid, 16);
    fprintf(stdout, "\nLocal LID = 0x%x\n", rdma->port_attr.lid);
    if (sock_sync_data(rdma->sock, sizeof (struct cm_con_data_t), (char*)&local_con_data, (char*)&tmp_con_data) < 0)
    {
        fprintf(stderr, "failed to exchange connection data between sides\n");
        rc = 1;
        goto connect_qp_exit;
    }
    remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
    remote_con_data.lid = ntohs(tmp_con_data.lid);
    memcpy(remote_con_data.gid, tmp_con_data.gid, 16);

    rdma->remote_props = remote_con_data;
    fprintf(stdout, "Remote QP number = 0x%x\n", remote_con_data.qp_num);
    fprintf(stdout, "Remote LID = 0x%x\n", remote_con_data.lid);
    if (config.gid_idx >= 0)
    {
        uint8_t *p = remote_con_data.gid;
        fprintf (stdout, "Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
                p[10], p[11], p[12], p[13], p[14], p[15]);
    }
    /* modify the QP to init */
    rc = modify_qp_to_init(rdma->qp);
    if (rc)
    {
        fprintf(stderr, "change QP state to INIT failed\n");
        goto connect_qp_exit;
    }

    rc = modify_qp_to_rtr(rdma->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid);
    if (rc)
    {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        goto connect_qp_exit;
    }
    fprintf(stderr, "Modified QP state to RTR\n");
    rc = modify_qp_to_rts(rdma->qp);
    if (rc)
    {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        goto connect_qp_exit;
    }
    fprintf(stdout, "QP state was change to RTS\n");

connect_qp_exit:
    return rc;
}

/* ================================================================== */
/* RDMA */

static void mc_control_to_network(MC_RDMAControlHeader *control)
{
    control->type = htonl(control->type);
    control->len = htonl(control->len);
}

static void mc_network_to_control(MC_RDMAControlHeader *control)
{
    control->type = ntohl(control->type);
    control->len = ntohl(control->len);
}

static uint64_t mc_rdma_poll(uint64_t *wr_id_out, uint32_t *byte_len)
{
    int ret;
    struct ibv_wc wc;
    uint64_t wr_id;

    ret = ibv_poll_cq(rdma->cq, 1, &wc);

    if (!ret) {
        *wr_id_out = RDMA_WRID_NONE;
        return 0;
    }

    if (ret < 0) {
        fprintf(stderr, "ibv_poll_cq return %d", ret);
        return ret;
    }

    wr_id = wc.wr_id & RDMA_WRID_TYPE_MASK;

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "ibv_poll_cq wc.status=%d %s!\n", wc.status, ibv_wc_status_str(wc.status));
        return -1;
    }

    if (rdma->control_ready_expected && (wr_id >= RDMA_WRID_RECV_CONTROL)) {
        rdma->control_ready_expected = 0;
    }

    *wr_id_out = wc.wr_id;
    if (byte_len) {
        *byte_len = wc.byte_len;
    }

    return 0;
}

/*
 * First poll to see if a work request has already completed,
 * otherwise block.
 *
 */
static int mc_rdma_block_for_wrid(int wrid_requested, uint32_t *byte_len)
{
    int ret = 0;
    uint64_t wr_id = RDMA_WRID_NONE, wr_id_in;
    /* poll cq first */
    while (wr_id != wrid_requested) {
        ret = mc_rdma_poll(&wr_id_in, byte_len);
        if (ret < 0) {
            return ret;
        }

        wr_id = wr_id_in & RDMA_WRID_TYPE_MASK;

        if (wr_id == RDMA_WRID_NONE) {
            break;
        }
        if (wr_id != wrid_requested) {
        }
    }

    if (wr_id == wrid_requested) {
        return 0;
    }

    while (1) {
        // ibv_get_cq_event() - blocks until gets "event"

        while (wr_id != wrid_requested) {
            ret = mc_rdma_poll(&wr_id_in, byte_len);
            if (ret < 0) {
            }

            wr_id = wr_id_in & RDMA_WRID_TYPE_MASK;

            if (wr_id == RDMA_WRID_NONE) {
                break;
            }
        }

        if (wr_id == wrid_requested) {
            goto success_block_for_wrid;
        }
    }

success_block_for_wrid:
    return 0;
}


static int mc_rdma_post_send_control(uint8_t *buf, MC_RDMAControlHeader *head)
{
    int ret = 0;
    MC_RDMAWorkRequestData *wr = &rdma->wr_data[RDMA_WRID_CONTROL];
    struct ibv_send_wr *bad_wr;
    struct ibv_sge sge = {
                           .addr = (uintptr_t)(wr->control),
                           .length = head->len + sizeof(MC_RDMAControlHeader),
                           .lkey = wr->control_mr->lkey,
                         };
    struct ibv_send_wr send_wr = {
                                   .wr_id = RDMA_WRID_SEND_CONTROL,
                                   .opcode = IBV_WR_SEND,
                                   .send_flags = IBV_SEND_SIGNALED,
                                   .sg_list = &sge,
                                   .num_sge = 1,
                                };

    memcpy(wr->control, head, sizeof(MC_RDMAControlHeader));
    mc_control_to_network((void *) wr->control);

    if (buf) {
        memcpy(wr->control + sizeof(MC_RDMAControlHeader), buf, head->len);
    }

    ret = ibv_post_send(rdma->qp, &send_wr, &bad_wr);

    if (ret > 0) {
        fprintf(stderr, "Failed to use post IB SEND for control!");
        return -ret;
    }

    ret = mc_rdma_block_for_wrid(RDMA_WRID_SEND_CONTROL, NULL);
    if (ret < 0) {
        fprintf(stderr, "send polling control!");
    }

    return ret;
}

static int mc_rdma_post_recv_control(int idx)
{
    struct ibv_recv_wr *bad_wr;
    struct ibv_sge sge = {
                            .addr = (uintptr_t)(rdma->wr_data[idx].control),
                            .length = RDMA_CONTROL_MAX_BUFFER,
                            .lkey = rdma->wr_data[idx].control_mr->lkey,
                         };

    struct ibv_recv_wr recv_wr = {
                                    .wr_id = RDMA_WRID_RECV_CONTROL + idx,
                                    .sg_list = &sge,
                                    .num_sge = 1,
                                 };


    if (ibv_post_recv(rdma->qp, &recv_wr, &bad_wr)) {
        return -1;
    }

    return 0;
}

static int mc_rdma_exchange_get_response(MC_RDMAControlHeader *head, int expecting, int idx)
{
    uint32_t byte_len;
    int ret = mc_rdma_block_for_wrid(RDMA_WRID_RECV_CONTROL + idx, &byte_len);

    if (ret < 0) {
        fprintf(stderr, "recv polling control!");
        return ret;
    }

    mc_network_to_control((void *) rdma->wr_data[idx].control);
    memcpy(head, rdma->wr_data[idx].control, sizeof(MC_RDMAControlHeader));

    if (expecting == RDMA_CONTROL_NONE) {
    } else if (head->type != expecting || head->type == RDMA_CONTROL_ERROR) {
        fprintf(stderr, "Was expecting a %s (%d) control message, but got: %s (%d), length: %d", control_desc[expecting], expecting, control_desc[head->type], head->type, head->len);
        return -EIO;
    }
    if (head->len > RDMA_CONTROL_MAX_BUFFER - sizeof(*head)) {
        fprintf(stderr, "too long length: %d", head->len);
        return -EINVAL;
    }
    if (sizeof(*head) + head->len != byte_len) {
        fprintf(stderr, "Malformed length: %d byte_len %d", head->len, byte_len);
        return -EINVAL;
    }

    return 0;
}


/*
 * When a RECV work request has completed, the work request's
 * buffer is pointed at the header.
 *
 * This will advance the pointer to the data portion
 * of the control message of the work request's buffer that
 * was populated after the work request finished.
 */
static void mc_rdma_move_header(int idx, RDMAControlHeader *head)
{
    rdma->wr_data[idx].control_len = head->len;
    rdma->wr_data[idx].control_curr = rdma->wr_data[idx].control + sizeof(RDMAControlHeader);
}

static int mc_rdma_exchange_send(MC_RDMAControlHeader *head, uint8_t *data)
{
    int ret = 0;

    /*
     * Wait until the dest is ready before attempting to deliver the message
     * by waiting for a READY message.
     */
    if (rdma->control_ready_expected) {
        MC_RDMAControlHeader resp;
        ret = mc_rdma_exchange_get_response(&resp, RDMA_CONTROL_READY, RDMA_WRID_READY);
        if (ret < 0) {
            return ret;
        }
    }

    /*
     * Post a WR to replace the one we just consumed for the READY message.
     */
    ret = mc_rdma_post_recv_control(RDMA_WRID_READY);
    if (ret) {
        fprintf(stderr, "rdma migration: error posting first control recv!");
        return ret;
    }

    /*
     * Deliver the control message that was requested.
     */
    ret = mc_rdma_post_send_control(data, head);


    rdma->control_ready_expected = 1;

    return 0;

}

static int mc_rdma_exchange_recv(MC_RDMAControlHeader *head, int expecting)
{
    MC_RDMAControlHeader ready = {
                                .len = 0,
                                .type = RDMA_CONTROL_READY,
                              };
    int ret;

    /*
     * Inform the source that we're ready to receive a message.
     */
    ret = mc_rdma_post_send_control(NULL, &ready);

    if (ret < 0) {
        fprintf(stderr, "Failed to send control buffer!");
        return ret;
    }

    /*
     * Block and wait for the message.
     */
    ret = mc_rdma_exchange_get_response(head, expecting, RDMA_WRID_READY);

    if (ret < 0) {
        return ret;
    }

    mc_rdma_move_header(rdma, RDMA_WRID_READY, head);

    /*
     * Post a new RECV work request to replace the one we just consumed.
     */
    ret = mc_rdma_post_recv_control(rdma, RDMA_WRID_READY);
    if (ret) {
        fprintf(stderr, "rdma migration: error posting second control recv!");
        return ret;
    }

    return 0;  
}

static size_t mc_rdma_fill(uint8_t *buf, size_t size, int idx)
{
    size_t len = 0;

    if (rdma->wr_data[idx].control_len) {

        len = MIN(size, rdma->wr_data[idx].control_len);
        memcpy(buf, rdma->wr_data[idx].control_curr, len);
        rdma->wr_data[idx].control_curr += len;
        rdma->wr_data[idx].control_len -= len;
    }

    return len;
}

int mc_rdma_put_buffer(const uint8_t *buf, int size)
{
    uint8_t * data = (void *) buf;

    MC_RDMAControlHeader head;
    head.len = size;
    mc_rdma_exchange_send(&head, data);

    return size;
}

int mc_rdma_get_buffer(uint8_t *buf, int size)
{
    MC_RDMAControlHeader head;
    int ret = 0;

    ret = mc_rdma_exchange_recv(&head, RDMA_CONTROL_QEMU_FILE);

    return mc_rdma_fill(buf, size, 0);
}


int mc_rdma_init(int is_client)
{
    rdma = (MC_RDMAContext*)malloc(sizeof(MC_RDMAContext));
    if (is_client)
        config.server_name = "10.22.1.3";

    config.gid_idx = 0;
    config.ib_port = 2;
    resources_init();

    if (resources_create())
    {
        fprintf (stderr, "failed to create resources\n");
    }

    /* connect the QPs */
    if (connect_qp())
    {
        fprintf (stderr, "failed to connect QPs\n");
    }

    mc_rdma_post_recv_control(RDMA_WRID_READY);

    return 0;
}