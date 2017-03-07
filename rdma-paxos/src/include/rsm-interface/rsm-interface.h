#ifndef RSM_INTERFACE_H
#define RSM_INTERFACE_H
#include <unistd.h>
#include <stdint.h>

struct proxy_node_t;

#define STANDBY_BACKUP 0
#define MAJOR_BACKUP   1
#define LEADER         2

#ifdef __cplusplus
extern "C" {
#endif

	struct proxy_node_t* proxy_init(const char* proxy_log_path, uint8_t role, int *checkpoint_delay);
	void proxy_on_mirror(uint8_t *buf, int len);
	int is_leader(void);
	void proxy_stop_consensus(void);
	void proxy_resume_consensus(void);
	uint64_t proxy_get_sync_consensus(void);
	void proxy_wait_sync_consensus(uint64_t sync_consensus);
	
#ifdef __cplusplus
}
#endif

#endif
