#ifndef QEMU_MC_RDMA_H
#define QEMU_MC_RDMA_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/bitmap.h"

extern char mc_host_port[65];

int mc_start_incoming_migration(void);
int mc_start_outgoing_migration(void);

int mc_rdma_put_colo_ctrl_buffer(void *buf, uint32_t size);
ssize_t mc_rdma_get_colo_ctrl_buffer(void *buf, size_t size);

int mc_rdma_load_hook(QEMUFile *f, void *opaque, uint64_t flags, void *data);
int mc_rdma_registration_start(QEMUFile *f, void *opaque, uint64_t flags, void *data);
int mc_rdma_registration_stop(QEMUFile *f, void *opaque, uint64_t flags, void *data);

#endif /* QEMU_MC_RDMA_H */
