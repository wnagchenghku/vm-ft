#ifndef QEMU_MC_RDMA_H
#define QEMU_MC_RDMA_H

extern const char *mc_host_port;

int mc_rdma_init(int is_client);
ssize_t mc_rdma_get_buffer(uint8_t *buf, int64_t pos, size_t size);
ssize_t mc_rdma_put_buffer(const uint8_t *buf, int64_t pos, size_t size);

#endif /* QEMU_MC_RDMA_H */
