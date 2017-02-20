/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include <libnl3/netlink/route/qdisc/plug.h>
#include <libnl3/netlink/route/class.h>
#include <libnl3/netlink/cli/utils.h>
#include <libnl3/netlink/cli/tc.h>
#include <libnl3/netlink/cli/qdisc.h>
#include <libnl3/netlink/cli/link.h>

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "migration/colo.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "migration/failover.h"
#include "qapi-event.h"
#include "block/block.h"
#include "replication.h"
#include "net/colo-compare.h"

#include "qemu-common.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-net.h"
#include "qemu/sockets.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "qmp-commands.h"
#include "net/tap-linux.h"
#include "trace/simple.h"
#include "sysemu/block-backend.h"
#include <sys/ioctl.h>

/* ================================================================== */
/* QEMU MC Buffering Output */

#ifndef rtnl_tc_get_ops
extern struct rtnl_tc_ops * rtnl_tc_get_ops(struct rtnl_tc *);
#endif

#define DEBUG_MC
//#define DEBUG_MC_VERBOSE

#ifdef DEBUG_MC
#define DPRINTF(fmt, ...) \
    do { printf("mc: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#ifdef DEBUG_MC_VERBOSE
#define DDPRINTF(fmt, ...) \
    do { printf("mc: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DDPRINTF(fmt, ...) \
    do { } while (0)
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
static int mc_enable_buffering(void)
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

static int mc_start_buffer(void)
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

static int mc_flush_oldest_buffer(void)
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

static bool vmstate_loading;

/* colo buffer */
#define COLO_BUFFER_BASE_SIZE (4 * 1024 * 1024)

bool colo_supported(void)
{
    return true;
}

bool migration_in_colo_state(void)
{
    MigrationState *s = migrate_get_current();

    return (s->state == MIGRATION_STATUS_COLO);
}

bool migration_incoming_in_colo_state(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    return mis && (mis->state == MIGRATION_STATUS_COLO);
}

static bool colo_runstate_is_stopped(void)
{
    return runstate_check(RUN_STATE_COLO) || !runstate_is_running();
}

static void secondary_vm_do_failover(void)
{
    int old_state;
    MigrationIncomingState *mis = migration_incoming_get_current();
    Error *local_err = NULL;

    /* Can not do failover during the process of VM's loading VMstate, Or
      * it will break the secondary VM.
      */
    if (vmstate_loading) {
        old_state = failover_set_state(FAILOVER_STATUS_HANDLING,
                                       FAILOVER_STATUS_RELAUNCH);
        if (old_state != FAILOVER_STATUS_HANDLING) {
            error_report("Unknown error while do failover for secondary VM,"
                         "old_state: %d", old_state);
        }
        return;
    }

    migrate_set_state(&mis->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    replication_stop_all(true, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    if (!autostart) {
        error_report("\"-S\" qemu option will be ignored in secondary side");
        /* recover runstate to normal migration finish state */
        autostart = true;
    }
    /*
    * Make sure colo incoming thread not block in recv or send,
    * If mis->from_src_file and mis->to_src_file use the same fd,
    * The second shutdown() will return -1, we ignore this value,
    * it is harmless.
    */
    if (mis->from_src_file) {
        qemu_file_shutdown(mis->from_src_file);
    }
    if (mis->to_src_file) {
        qemu_file_shutdown(mis->to_src_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_HANDLING,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_HANDLING) {
        error_report("Incorrect state (%d) while doing failover for "
                     "secondary VM", old_state);
        return;
    }
    /* Notify COLO incoming thread that failover work is finished */
    qemu_sem_post(&mis->colo_incoming_sem);
    /* For Secondary VM, jump to incoming co */
    if (mis->migration_incoming_co) {
        qemu_coroutine_enter(mis->migration_incoming_co, NULL);
    }
}

static void primary_vm_do_failover(void)
{
    MigrationState *s = migrate_get_current();
    int old_state;
    Error *local_err = NULL;

    migrate_set_state(&s->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    /*
    * Make sure colo thread no block in recv or send,
    * The s->rp_state.from_dst_file and s->to_dst_file may use the
    * same fd, but we still shutdown the fd for twice, it is harmless.
    */
    if (s->to_dst_file) {
        qemu_file_shutdown(s->to_dst_file);
    }
    if (s->rp_state.from_dst_file) {
        qemu_file_shutdown(s->rp_state.from_dst_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_HANDLING,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_HANDLING) {
        error_report("Incorrect state (%d) while doing failover for Primary VM",
                     old_state);
        return;
    }

    replication_stop_all(true, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    /* Notify COLO thread that failover work is finished */
    qemu_sem_post(&s->colo_sem);
}

void colo_do_failover(MigrationState *s)
{
    /* Make sure vm stopped while failover */
    if (!colo_runstate_is_stopped()) {
        vm_stop_force_state(RUN_STATE_COLO);
    }

    if (get_colo_mode() == COLO_MODE_PRIMARY) {
        primary_vm_do_failover();
    } else {
        secondary_vm_do_failover();
    }
}

static void colo_send_message(QEMUFile *f, COLOMessage msg,
                              Error **errp)
{
    int ret;

    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return;
    }
    qemu_put_be32(f, msg);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't send COLO message");
    }
    trace_colo_send_message(COLOMessage_lookup[msg]);
}

static void colo_send_message_value(QEMUFile *f, COLOMessage msg,
                                    uint64_t value, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    colo_send_message(f, msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_put_be64(f, value);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to send value for message:%s",
                         COLOMessage_lookup[msg]);
    }
}

static COLOMessage colo_receive_message(QEMUFile *f, Error **errp)
{
    COLOMessage msg;
    int ret;

    msg = qemu_get_be32(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't receive COLO message");
        return msg;
    }
    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return msg;
    }
    trace_colo_receive_message(COLOMessage_lookup[msg]);
    return msg;
}

static void colo_receive_check_message(QEMUFile *f, COLOMessage expect_msg,
                                       Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (msg != expect_msg) {
        error_setg(errp, "Unexpected COLO message %d, expected %d",
                          msg, expect_msg);
    }
}

static uint64_t colo_receive_message_value(QEMUFile *f, uint32_t expect_msg,
                                           Error **errp)
{
    Error *local_err = NULL;
    uint64_t value;
    int ret;

    colo_receive_check_message(f, expect_msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return 0;
    }

    value = qemu_get_be64(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to get value for COLO message: %s",
                         COLOMessage_lookup[expect_msg]);
    }
    return value;
}

static int colo_do_checkpoint_transaction(MigrationState *s,
                                          QEMUSizedBuffer *buffer)
{
    QEMUFile *trans = NULL;
    size_t size;
    Error *local_err = NULL;
    int ret = -1;

    colo_send_message(s->to_dst_file, COLO_MESSAGE_CHECKPOINT_REQUEST,
                      &local_err);
    if (local_err) {
        goto out;
    }

    /* Reset colo buffer and open it for write */
    qsb_set_length(buffer, 0);
    trans = qemu_bufopen("w", buffer);
    if (!trans) {
        error_report("Open colo buffer for write failed");
        goto out;
    }

    qemu_mutex_lock_iothread();
    if (failover_request_is_active()) {
        qemu_mutex_unlock_iothread();
        goto out;
    }
    vm_stop_force_state(RUN_STATE_COLO);
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("run", "stop");

    /*
     * If buffering is enabled, insert a Qdisc plug here
     * to hold packets for the *next* MC, (not this one,
     * the packets for this one have already been plugged
     * and will be released after the MC has been transmitted.
     */
    mc_start_buffer();

    /*
     * failover request bh could be called after
     * vm_stop_force_state so we check failover_request_is_active() again.
     */
    if (failover_request_is_active()) {
        goto out;
    }

    /* we call this api although this may do nothing on primary side */
    qemu_mutex_lock_iothread();
    replication_do_checkpoint_all(&local_err);
    qemu_mutex_unlock_iothread();
    if (local_err) {
        goto out;
    }

    colo_send_message(s->to_dst_file, COLO_MESSAGE_VMSTATE_SEND, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_mutex_lock_iothread();
    /*
    * Only save VM's live state, which not including device state.
    * TODO: We may need a timeout mechanism to prevent COLO process
    * to be blocked here.
    */
    qemu_savevm_live_state(s->to_dst_file);
    /* Note: device state is saved into buffer */
    ret = qemu_save_device_state(trans);
    qemu_mutex_unlock_iothread();
    if (ret < 0) {
        error_report("Save device state error");
        goto out;
    }
    qemu_fflush(trans);

    /* we send the total size of the vmstate first */
    size = qsb_get_length(buffer);
    colo_send_message_value(s->to_dst_file, COLO_MESSAGE_VMSTATE_SIZE,
                            size, &local_err);
    if (local_err) {
        goto out;
    }

    qsb_put_buffer(s->to_dst_file, buffer, size);
    qemu_fflush(s->to_dst_file);
    ret = qemu_file_get_error(s->to_dst_file);
    if (ret < 0) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_RECEIVED, &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_LOADED, &local_err);
    if (local_err) {
        goto out;
    }

    if (colo_shutdown_requested) {
        colo_send_message(s->to_dst_file, COLO_MESSAGE_GUEST_SHUTDOWN,
                          &local_err);
        if (local_err) {
            error_free(local_err);
            /* Go on the shutdown process and throw the error message */
            error_report("Failed to send shutdown message to SVM");
        }
        qemu_fflush(s->to_dst_file);
        colo_shutdown_requested = 0;
        qemu_system_shutdown_request_core();
        /* Fix me: Just let the colo thread exit ? */
        qemu_thread_exit(0);
    }

    ret = 0;

    mc_flush_oldest_buffer();

    /* Resume primary guest */
    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

    //colo_compare_do_checkpoint();

out:
    if (local_err) {
        error_report_err(local_err);
    }
    if (trans) {
        qemu_fclose(trans);
    }
    return ret;
}

static int colo_prepare_before_save(MigrationState *s)
{
    int ret;

    /* Disable block migration */
    s->params.blk = 0;
    s->params.shared = 0;
    qemu_savevm_state_begin(s->to_dst_file, &s->params);
    ret = qemu_file_get_error(s->to_dst_file);
    if (ret < 0) {
        error_report("Save vm state begin error");
    }
    return ret;
}

static void colo_process_checkpoint(MigrationState *s)
{
    QEMUSizedBuffer *buffer = NULL;
    int64_t current_time, checkpoint_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    Error *local_err = NULL;
    int ret;

    ret = mc_enable_buffering();
    if (ret > 0) {

    }

    failover_init_state();

    s->rp_state.from_dst_file = qemu_file_get_return_path(s->to_dst_file);
    if (!s->rp_state.from_dst_file) {
        error_report("Open QEMUFile from_dst_file failed");
        goto out;
    }

    ret = colo_prepare_before_save(s);
    if (ret < 0) {
        goto out;
    }

    /*
     * Wait for Secondary finish loading vm states and enter COLO
     * restore.
     */
    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_CHECKPOINT_READY, &local_err);
    if (local_err) {
        goto out;
    }

    buffer = qsb_create(NULL, COLO_BUFFER_BASE_SIZE);
    if (buffer == NULL) {
        error_report("Failed to allocate colo buffer!");
        goto out;
    }

    qemu_mutex_lock_iothread();
    /* start block replication */
    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    if (local_err) {
        qemu_mutex_unlock_iothread();
        goto out;
    }

    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

    ret = global_state_store();
    if (ret < 0) {
        goto out;
    }

    while (s->state == MIGRATION_STATUS_COLO) {
        if (failover_request_is_active()) {
            error_report("failover request");
            goto out;
        }

        if (colo_compare_result()) {
            goto checkpoint_begin;
        }
        current_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
        if ((current_time - checkpoint_time <
            s->parameters[MIGRATION_PARAMETER_X_CHECKPOINT_DELAY]) &&
            !colo_shutdown_requested) {
            g_usleep(10 * 1000); /* 10 ms */
            continue;
        }

checkpoint_begin:
        /* start a colo checkpoint */
        ret = colo_do_checkpoint_transaction(s, buffer);
        if (ret < 0) {
            goto out;
        }
        checkpoint_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    }

out:
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }
    /*
    * There are only two reasons we can go here, something error happened,
    * Or users triggered failover.
    */
    if (!failover_request_is_active()) {
        qapi_event_send_colo_exit(COLO_MODE_PRIMARY,
                                  COLO_EXIT_REASON_ERROR, NULL);
    } else {
        qapi_event_send_colo_exit(COLO_MODE_PRIMARY,
                                  COLO_EXIT_REASON_REQUEST, NULL);
    }

    qsb_free(buffer);
    buffer = NULL;

    /* Hope this not to be too long to wait here */
    qemu_sem_wait(&s->colo_sem);
    qemu_sem_destroy(&s->colo_sem);
    /*
    * Must be called after failover BH is completed,
    * Or the failover BH may shutdown the wrong fd, that
    * re-used by other thread after we release here.
    */
    if (s->rp_state.from_dst_file) {
        qemu_fclose(s->rp_state.from_dst_file);
    }
}

void migrate_start_colo_process(MigrationState *s)
{
    qemu_mutex_unlock_iothread();
    qemu_sem_init(&s->colo_sem, 0);
    migrate_set_state(&s->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);
    colo_process_checkpoint(s);
    qemu_mutex_lock_iothread();
}

static void colo_wait_handle_message(QEMUFile *f, int *checkpoint_request,
                                     Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    switch (msg) {
    case COLO_MESSAGE_CHECKPOINT_REQUEST:
        *checkpoint_request = 1;
        break;
    case COLO_MESSAGE_GUEST_SHUTDOWN:
        qemu_mutex_lock_iothread();
        vm_stop_force_state(RUN_STATE_COLO);
        replication_stop_all(false, NULL);
        qemu_system_shutdown_request_core();
        qemu_mutex_unlock_iothread();
        /* the main thread will exit and terminate the whole
        * process, do we need some cleanup?
        */
        qemu_thread_exit(0);
    default:
        *checkpoint_request = 0;
        error_setg(errp, "Got unknown COLO message: %d", msg);
        break;
    }
}

static int colo_prepare_before_load(QEMUFile *f)
{
    int ret;

    ret = qemu_loadvm_state_begin(f);
    if (ret < 0) {
        error_report("load vm state begin error, ret=%d", ret);
    }
    return ret;
}

void *colo_process_incoming_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    QEMUFile *fb = NULL;
    QEMUSizedBuffer *buffer = NULL; /* Cache incoming device state */
    uint64_t total_size;
    uint64_t value;
    Error *local_err = NULL;
    int ret;

    qemu_sem_init(&mis->colo_incoming_sem, 0);

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);

    failover_init_state();

    mis->to_src_file = qemu_file_get_return_path(mis->from_src_file);
    if (!mis->to_src_file) {
        error_report("colo incoming thread: Open QEMUFile to_src_file failed");
        goto out;
    }
    /* Note: We set the fd to unblocked in migration incoming coroutine,
    *  But here we are in the colo incoming thread, so it is ok to set the
    *  fd back to blocked.
    */
    qemu_file_set_blocking(mis->from_src_file, true);

    ret = colo_init_ram_cache();
    if (ret < 0) {
        error_report("Failed to initialize ram cache");
        goto out;
    }

    buffer = qsb_create(NULL, COLO_BUFFER_BASE_SIZE);
    if (buffer == NULL) {
        error_report("Failed to allocate colo buffer!");
        goto out;
    }

    ret = colo_prepare_before_load(mis->from_src_file);
    if (ret < 0) {
        goto out;
    }

    qemu_mutex_lock_iothread();
    bdrv_invalidate_cache_all(&local_err);
    /* start block replication */
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    qemu_mutex_unlock_iothread();
    if (local_err) {
        goto out;
    }

    colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_READY,
                      &local_err);
    if (local_err) {
        goto out;
    }

    while (mis->state == MIGRATION_STATUS_COLO) {
        int request;

        colo_wait_handle_message(mis->from_src_file, &request, &local_err);
        if (local_err) {
            goto out;
        }
        assert(request);
        if (failover_request_is_active()) {
            error_report("failover request");
            goto out;
        }

        qemu_mutex_lock_iothread();
        vm_stop_force_state(RUN_STATE_COLO);
        trace_colo_vm_state_change("run", "stop");
        qemu_mutex_unlock_iothread();

        colo_receive_check_message(mis->from_src_file,
                           COLO_MESSAGE_VMSTATE_SEND, &local_err);
        if (local_err) {
            goto out;
        }

        ret = qemu_loadvm_state_main(mis->from_src_file, mis);
        if (ret < 0) {
            error_report("Load VM's live state (ram) error");
            goto out;
        }
        /* read the VM state total size first */
        value = colo_receive_message_value(mis->from_src_file,
                                 COLO_MESSAGE_VMSTATE_SIZE, &local_err);
        if (local_err) {
            goto out;
        }

        /* read vm device state into colo buffer */
        total_size = qsb_fill_buffer(buffer, mis->from_src_file, value);
        if (total_size != value) {
            error_report("Got %lu VMState data, less than expected %lu",
                         total_size, value);
            ret = -EINVAL;
            goto out;
        }

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_RECEIVED,
                     &local_err);
        if (local_err) {
            goto out;
        }

        /* open colo buffer for read */
        fb = qemu_bufopen("r", buffer);
        if (!fb) {
            error_report("Can't open colo buffer for read");
            goto out;
        }

        qemu_mutex_lock_iothread();
        qemu_system_reset(VMRESET_SILENT);
        vmstate_loading = true;
        colo_flush_ram_cache();
        ret = qemu_load_device_state(fb);
        if (ret < 0) {
            error_report("COLO: load device state failed");
            qemu_mutex_unlock_iothread();
            goto out;
        }

        replication_get_error_all(&local_err);
        if (local_err) {
            qemu_mutex_unlock_iothread();
            goto out;
        }
        /* discard colo disk buffer */
        replication_do_checkpoint_all(&local_err);
        if (local_err) {
            qemu_mutex_unlock_iothread();
            goto out;
        }

        vmstate_loading = false;
        qemu_mutex_unlock_iothread();

        if (failover_get_state() == FAILOVER_STATUS_RELAUNCH) {
            failover_set_state(FAILOVER_STATUS_RELAUNCH, FAILOVER_STATUS_NONE);
            failover_request_active(NULL);
            goto out;
        }

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_LOADED,
                     &local_err);
        if (local_err) {
            goto out;
        }

        qemu_mutex_lock_iothread();
        vm_start();
        trace_colo_vm_state_change("stop", "run");
        qemu_mutex_unlock_iothread();
        qemu_fclose(fb);
        fb = NULL;
    }

out:
     vmstate_loading = false;
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }
    if (!failover_request_is_active()) {
        qapi_event_send_colo_exit(COLO_MODE_SECONDARY,
                                  COLO_EXIT_REASON_ERROR, NULL);
    } else {
        qapi_event_send_colo_exit(COLO_MODE_SECONDARY,
                                  COLO_EXIT_REASON_REQUEST, NULL);
    }

    if (fb) {
        qemu_fclose(fb);
    }
    qsb_free(buffer);
    /* Here, we can ensure BH is hold the global lock, and will join colo
    * incoming thread, so here it is not necessary to lock here again,
    * or there will be a deadlock error.
    */
    colo_release_ram_cache();

    /* Hope this not to be too long to loop here */
    qemu_sem_wait(&mis->colo_incoming_sem);
    qemu_sem_destroy(&mis->colo_incoming_sem);
    /* Must be called after failover BH is completed */
    if (mis->to_src_file) {
        qemu_fclose(mis->to_src_file);
    }
    migration_incoming_exit_colo();

    return NULL;
}

bool colo_shutdown(void)
{
    /*
    * if in colo mode, we need do some significant work before respond
    * to the shutdown request.
    */
    if (migration_incoming_in_colo_state()) {
        return true; /* primary's responsibility */
    }
    if (migration_in_colo_state()) {
        colo_shutdown_requested = 1;
        return true;
    }
    return false;
}
