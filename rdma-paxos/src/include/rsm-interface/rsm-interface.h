#ifndef RSM_INTERFACE_H
#define RSM_INTERFACE_H
#include <unistd.h>
#include <stdint.h>

struct proxy_node_t;

#ifdef __cplusplus
extern "C" {
#endif

	struct proxy_node_t* proxy_init(const char* proxy_log_path);
	void proxy_on_mirror(uint8_t *buf, int len);
	int is_leader(void);
	
#ifdef __cplusplus
}
#endif

#endif
