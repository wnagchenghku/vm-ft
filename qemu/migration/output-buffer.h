#ifndef QEMU_OUTPUT_BUFFER_H
#define QEMU_OUTPUT_BUFFER_H

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

int mc_enable_buffering(void);
int mc_start_buffer(void);
int mc_flush_oldest_buffer(void);

#endif /* QEMU_OUTPUT_BUFFER_H */
