#ifndef QEMU_CHECKPOINT_H
#define QEMU_CHECKPOINT_H

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

#define DEBUG_MC
#define DEBUG_MC_VERBOSE
#define DEBUG_MC_REALLY_VERBOSE

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

extern const char *mc_host_port;

int mc_enable_buffering(void);
int mc_start_buffer(void);
int mc_flush_oldest_buffer(void);

int mc_rdma_init(int is_client);
int mc_rdma_put_buffer(const uint8_t *buf, int size);
int mc_rdma_get_buffer(uint8_t *buf, int size);
#endif /* QEMU_CHECKPOINT_H */
