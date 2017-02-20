#ifndef RSM_INTERFACE_H
#define RSM_INTERFACE_H
#include <unistd.h>
#include <stdint.h>

struct proxy_node_t;

#ifdef __cplusplus
extern "C" {
#endif

	struct proxy_node_t* proxy_init(const char* config_path, const char* proxy_log_path);
	void proxy_on_mirror(uint8_t *buf, int len);
	void mc_flush_oldest_buffer(void);
	void proxy_on_buffer(int fd, const struct iovec *iov, int iovcnt);
	void mc_start_buffer(void);
	int is_leader(void);
	
#ifdef __cplusplus
}
#endif

#endif
