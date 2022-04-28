#ifndef DPDKFLOW_IMPL_H
#define DPDKFLOW_IMPL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ip_frag.h>
#include <rte_rwlock.h>
#include <rte_lpm.h>
#include <rte_lpm6.h>

#define CORE_MAX 8
#define PORT_MAX 8
#define VLAN_MAX 8
#define LOCAL_NETS_MAX 8
extern const int8_t core_max;
extern const int8_t port_max;
extern const int8_t vlan_max;
extern const int8_t local_nets_max;

#define DIRECTION_INCOMING 1
#define DIRECTION_OUTGOING 2
#define DIRECTION_INTERNAL 3
#define DIRECTION_EXTERNAL 4
extern const int8_t direction_incoming;
extern const int8_t direction_outgoing;
extern const int8_t direction_internal;
extern const int8_t direction_external;

#define AF_IPV4 2
#define AF_IPV6 10
extern const int8_t af_ipv4;
extern const int8_t af_ipv6;

extern const uint32_t aggregate_f_iface;
extern const uint32_t aggregate_f_af;
extern const uint32_t aggregate_f_proto;
extern const uint32_t aggregate_f_vlan;
extern const uint32_t aggregate_f_src_host;
extern const uint32_t aggregate_f_dst_host;
extern const uint32_t aggregate_f_src_as;
extern const uint32_t aggregate_f_dst_as;
extern const uint32_t aggregate_f_src_port;
extern const uint32_t aggregate_f_dst_port;
extern const uint32_t aggregate_f_app;

#define APP_TABLE_HASH_SIZE 1024
#define APP_DESC_LEN 44

struct dpdkflow_app_table_entry {
	uint32_t app;
	char app_desc[APP_DESC_LEN];
	struct dpdkflow_app_table_entry *next;
};

struct dpdkflow_app_table {
	struct dpdkflow_app_table_entry *app_table_hash_table[APP_TABLE_HASH_SIZE];
};

struct dpdkflow_metric {
	int8_t iface;
	int8_t direction;
	uint8_t af;
	uint8_t proto;
	int32_t vlan;
	uint8_t src_host[16];
	uint8_t dst_host[16];
	int64_t src_as;
	int64_t dst_as;
	int src_port;
	int dst_port;
	uint32_t app;
	char app_desc[APP_DESC_LEN];
	uint64_t packets;
	uint64_t bytes;

	uint64_t start_time;
	uint64_t stop_time;
	struct dpdkflow_metric *hash_next;
	struct dpdkflow_metric *list_next;
};

struct dpdkflow_context_port {
	uint8_t index;
	int32_t port_vlan_id;
	int32_t tag_vlan_ids[VLAN_MAX];
	int tag_vlan_num;
};

struct dpdkflow_context_core {
	int index;

	struct dpdkflow_context_port ports[PORT_MAX];
	uint8_t port_num;
};

struct dpdkflow_context {
	int done;
	int running;
	uint64_t tsc1s;

	int main_core_index;
	int interval;
	uint32_t metrics_num;
	uint8_t local_nets_ipv4_pfix[LOCAL_NETS_MAX][16];
	uint8_t local_nets_ipv4_plen[LOCAL_NETS_MAX];
	uint8_t local_nets_ipv4_num;
	uint8_t local_nets_ipv6_pfix[LOCAL_NETS_MAX][16];
	uint8_t local_nets_ipv6_plen[LOCAL_NETS_MAX];
	uint8_t local_nets_ipv6_num;
	uint32_t aggregate_flags_incoming;
	uint32_t aggregate_flags_outgoing;
	uint32_t aggregate_flags_internal;
	uint32_t aggregate_flags_external;

	struct dpdkflow_context_core cores[CORE_MAX];
	uint8_t core_num;

	struct rte_mempool *mbuf_pool;
	struct rte_mempool *metric_pool;

	uint64_t metric_sent;
	uint64_t metric_alloced;
	uint64_t metric_getfailed;
	rte_rwlock_t metric_stats_lock;

	/* mrt_rib */
	char mrt_rib_path[256];
	struct rte_lpm *mrt_rib_table_ipv4;
	uint32_t mrt_rib_table_ipv4_seq;
	struct rte_lpm6 *mrt_rib_table_ipv6;
	uint32_t mrt_rib_table_ipv6_seq;
	rte_rwlock_t mrt_rib_lock;
	struct timespec mrt_rib_last_mtim;

	/* app_table */
	struct dpdkflow_app_table *app_table;
	rte_rwlock_t app_table_lock;
	struct timespec protocols_last_mtim;
	struct timespec services_last_mtim;

	/* metric */
	struct dpdkflow_metric **metric_hash_table;
	struct dpdkflow_metric *metric_list_head;
	struct dpdkflow_metric *metric_list_tail;
	rte_rwlock_t metric_lock;
};

/* dpdkflow_mrt_rib.c */
extern uint32_t mrt_rib_lookup(struct dpdkflow_context *ctx, uint8_t af, uint8_t *addr);
extern int mrt_rib_updated(struct dpdkflow_context *ctx);
extern int mrt_rib_load(struct dpdkflow_context *ctx);
extern void mrt_rib_context_init(struct dpdkflow_context *ctx);

/* dpdkflow_app_table.c */
extern void fill_app_desc(char *app_desc, uint32_t app, struct dpdkflow_context *ctx);
extern int app_table_updated(struct dpdkflow_context *ctx);
extern int app_table_load(struct dpdkflow_context *ctx);
extern void app_table_context_init(struct dpdkflow_context *ctx);

/* dpdkflow_metric.c */
extern int metric_deq(struct dpdkflow_context *ctx, struct dpdkflow_metric **mbuf, int mbuf_size);
extern void metric_update(struct dpdkflow_context *ctx, struct dpdkflow_metric *m, int *stored);
extern void metric_print(struct dpdkflow_metric *m);
extern void metric_init(struct dpdkflow_metric *m);
extern void metric_context_init(struct dpdkflow_context *ctx);

/* dpdkflow_cgo.c */
extern uint64_t now();
extern int aggregate_flag_up(struct dpdkflow_context *ctx, int8_t direction, uint32_t aggregate_f);
extern void print_stats(struct dpdkflow_context *ctx);
extern int check_and_reload_tables(struct dpdkflow_context *ctx);
extern int start(struct dpdkflow_context *ctx);

#endif /* DPDKFLOW_IMPL_H */
