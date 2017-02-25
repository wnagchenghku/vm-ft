#ifndef QEMU_CHECKPOINT_H
#define QEMU_CHECKPOINT_H

int mc_enable_buffering(void);
int mc_start_buffer(void);
int mc_flush_oldest_buffer(void);

#endif /* QEMU_CHECKPOINT_H */
