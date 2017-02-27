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

enum {
    MC_TRANSACTION_NACK = 300,
    MC_TRANSACTION_START,
    MC_TRANSACTION_COMMIT,
    MC_TRANSACTION_ABORT,
    MC_TRANSACTION_ACK,
    MC_TRANSACTION_END,
    MC_TRANSACTION_DISK_ENABLE,
    MC_TRANSACTION_DISK_DISABLE,
    MC_TRANSACTION_ANY,
};

int mc_enable_buffering(void);
int mc_start_buffer(void);
int mc_flush_oldest_buffer(void);

int mc_send(QEMUFile *f, uint64_t request);
int mc_recv(QEMUFile *f, uint64_t request, uint64_t *action);

#endif /* QEMU_CHECKPOINT_H */
