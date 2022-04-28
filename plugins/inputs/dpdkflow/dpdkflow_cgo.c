#include "dpdkflow_cgo.h"

const int8_t core_max = CORE_MAX;
const int8_t port_max = PORT_MAX;
const int8_t vlan_max = VLAN_MAX;
const int8_t local_nets_max = LOCAL_NETS_MAX;

const int8_t direction_incoming = DIRECTION_INCOMING;
const int8_t direction_outgoing = DIRECTION_OUTGOING;
const int8_t direction_internal = DIRECTION_INTERNAL;
const int8_t direction_external = DIRECTION_EXTERNAL;

const int8_t af_ipv4 = AF_IPV4;
const int8_t af_ipv6 = AF_IPV6;

const uint32_t aggregate_f_iface     = 0x0001;
const uint32_t aggregate_f_af        = 0x0004;
const uint32_t aggregate_f_proto     = 0x0008;
const uint32_t aggregate_f_vlan      = 0x0010;
const uint32_t aggregate_f_src_host  = 0x0020;
const uint32_t aggregate_f_dst_host  = 0x0040;
const uint32_t aggregate_f_src_as    = 0x0080;
const uint32_t aggregate_f_dst_as    = 0x0100;
const uint32_t aggregate_f_src_port  = 0x0200;
const uint32_t aggregate_f_dst_port  = 0x0400;
const uint32_t aggregate_f_app       = 0x0800;

#define RX_RING_SIZE 2048
#define TX_RING_SIZE 512

//#define NUM_METRICS 65536
#define NUM_METRICS 262144
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

void
debug_print_aggregate_flags(uint32_t aggregate_flags)
{
	if (aggregate_flags & aggregate_f_iface) {
		printf("iface, ");
	}
	if (aggregate_flags & aggregate_f_af) {
		printf("af, ");
	}
	if (aggregate_flags & aggregate_f_proto) {
		printf("proto, ");
	}
	if (aggregate_flags & aggregate_f_vlan) {
		printf("vlan, ");
	}
	if (aggregate_flags & aggregate_f_src_host) {
		printf("src_host, ");
	}
	if (aggregate_flags & aggregate_f_dst_host) {
		printf("dst_host, ");
	}
	if (aggregate_flags & aggregate_f_src_as) {
		printf("src_as, ");
	}
	if (aggregate_flags & aggregate_f_dst_as) {
		printf("dst_as, ");
	}
	if (aggregate_flags & aggregate_f_src_port) {
		printf("src_port, ");
	}
	if (aggregate_flags & aggregate_f_dst_port) {
		printf("dst_port, ");
	}
	if (aggregate_flags & aggregate_f_app) {
		printf("app, ");
	}
}

void
debug_print_ctx(struct dpdkflow_context *ctx)
{
	printf("main_core_index = %d\n", ctx->main_core_index);
	printf("interval = %d\n", ctx->interval);
	printf("aggregate_flags_incoming = ");
	debug_print_aggregate_flags(ctx->aggregate_flags_incoming);
	printf("\n");
	printf("aggregate_flags_outgoing = ");
	debug_print_aggregate_flags(ctx->aggregate_flags_outgoing);
	printf("\n");
	printf("aggregate_flags_internal = ");
	debug_print_aggregate_flags(ctx->aggregate_flags_internal);
	printf("\n");
	printf("aggregate_flags_external = ");
	debug_print_aggregate_flags(ctx->aggregate_flags_external);
	printf("\n");
	printf("local_nets_ipv4 =\n");
	for (int i = 0; i < ctx->local_nets_ipv4_num; i++) {
		printf("    i = %d ", i);
		for (int j = 0; j < 16; j++) {
			printf("%02x", ctx->local_nets_ipv4_pfix[i][j]);
		}
		printf("/%d\n", ctx->local_nets_ipv4_plen[i]);
	}
	printf("local_nets_ipv6 =\n");
	for (int i = 0; i < ctx->local_nets_ipv6_num; i++) {
		printf("    i = %d ", i);
		for (int j = 0; j < 16; j++) {
			printf("%02x", ctx->local_nets_ipv6_pfix[i][j]);
		}
		printf("/%d\n", ctx->local_nets_ipv6_plen[i]);
	}
	printf("mrt_rib_path = %s\n", ctx->mrt_rib_path);
	printf("core_num = %d\n", ctx->core_num);
	for (int i = 0; i < ctx->core_num; i++) {
		printf("    i = %d\n", i);
		printf("    core index = %d\n", ctx->cores[i].index);
		printf("    port_num = %d\n", ctx->cores[i].port_num);
		for (int j = 0; j < ctx->cores[i].port_num; j++) {
			printf("        j = %d\n", j);
			printf("        port index = %d\n", ctx->cores[i].ports[j].index);
			printf("        port_vlan_id = %d\n", ctx->cores[i].ports[j].port_vlan_id);
			for (int k = 0; k < ctx->cores[i].ports[j].tag_vlan_num; k++) {
				printf("            k = %d\n", k);
				printf("            tag_vlan_id = %d\n", ctx->cores[i].ports[j].tag_vlan_ids[k]);
			}
		}
	}
}

int8_t
get_direction(struct dpdkflow_context *ctx, uint8_t af, uint8_t *src_host, uint8_t *dst_host)
{
	int8_t direction = -1;
	int src_match = 0;
	int dst_match = 0;
	switch (af) {
	case AF_IPV4:
		{
			uint32_t src_addr = ntohl(*(uint32_t *)&src_host[12]);
			uint32_t dst_addr = ntohl(*(uint32_t *)&dst_host[12]);
			for (int i = 0; i < ctx->local_nets_ipv4_num; i++) {
				int mask_shift = (32 - ctx->local_nets_ipv4_plen[i]);
				uint32_t local_addr = ntohl(*(uint32_t *)&ctx->local_nets_ipv4_pfix[i][12]);
				uint32_t netmask = (uint64_t)0xffffffff << mask_shift;
				if ((src_addr & netmask) == (local_addr & netmask)) {
					src_match++;
				}
				if ((dst_addr & netmask) == (local_addr & netmask)) {
					dst_match++;
				}
			}
		}
		break;
	case AF_IPV6:
		{
			for (int i = 0; i < ctx->local_nets_ipv6_num; i++) {
				int src_ne = 0;
				int dst_ne = 0;
				for (int j = 0; j < 4; j++) {
					uint32_t src_addr = ntohl(*(uint32_t *)&src_host[j * 4]);
					uint32_t dst_addr = ntohl(*(uint32_t *)&dst_host[j * 4]);
					uint8_t *prefix_tmp = &ctx->local_nets_ipv6_pfix[i][j * 4];
					uint32_t local_addr = ntohl(*(uint32_t *)prefix_tmp);
					uint32_t netmask;
					if (j < (ctx->local_nets_ipv6_plen[i] >> 5)) {
						netmask = 0xffffffff;
					} else if (j > (ctx->local_nets_ipv6_plen[i] >> 5)) {
						netmask = 0x00000000;
					} else {
						int mask_shift = (32 - (ctx->local_nets_ipv6_plen[i] & 0x1f));
						netmask = (uint64_t)0xffffffff << mask_shift;
					}
					if ((src_addr & netmask) != (local_addr & netmask)) {
						src_ne++;
					}
					if ((dst_addr & netmask) != (local_addr & netmask)) {
						dst_ne++;
					}
				}
				if (!src_ne) {
					src_match++;
				}
				if (!dst_ne) {
					dst_match++;
				}
			}
		}
		break;
	}
	if (src_match) {
		if (dst_match) {
			direction = direction_internal;
		} else {
			direction = direction_outgoing;
		}
	} else {
		if (dst_match) {
			direction = direction_incoming;
		} else {
			direction = direction_external;
		}
	}
	return direction;
}

uint32_t
aggregate_flags(struct dpdkflow_context *ctx, int8_t direction)
{
	switch (direction) {
	case DIRECTION_INCOMING:
		return ctx->aggregate_flags_incoming;
	case DIRECTION_OUTGOING:
		return ctx->aggregate_flags_outgoing;
	case DIRECTION_INTERNAL:
		return ctx->aggregate_flags_internal;
	case DIRECTION_EXTERNAL:
		return ctx->aggregate_flags_external;
	}
	return 0;
}

int
aggregate_flag_up(struct dpdkflow_context *ctx, int8_t direction, uint32_t aggregate_f)
{
	uint32_t aggregate_flags;
	switch (direction) {
	case DIRECTION_INCOMING:
		aggregate_flags = ctx->aggregate_flags_incoming;
		break;
	case DIRECTION_OUTGOING:
		aggregate_flags = ctx->aggregate_flags_outgoing;
		break;
	case DIRECTION_INTERNAL:
		aggregate_flags = ctx->aggregate_flags_internal;
		break;
	case DIRECTION_EXTERNAL:
		aggregate_flags = ctx->aggregate_flags_external;
		break;
	default:
		return 0;
	}
	if (aggregate_flags & aggregate_f) {
		return 1;
	}
	return 0;
}

int
get_cores_str(struct dpdkflow_context *ctx, char *cores_str, int buf_len)
{
	char *p = cores_str;
	snprintf(p, (buf_len - (p - cores_str)), "%d", ctx->main_core_index);
	p += strnlen(p, (buf_len - (p - cores_str)));
	*p = '\0';
	for (int i = 0; i < ctx->core_num; i++) {
		if ((p - cores_str + 3) >= buf_len) {
			printf("cores_str: buf short\n");
			cores_str[0] = '\0';
			return -1;
		}
		*p = ',';
		p++;
		*p = '\0';
		snprintf(p, (buf_len - (p - cores_str)), "%d", ctx->cores[i].index);
		p += strnlen(p, (buf_len - (p - cores_str)));
		*p = '\0';
	}
	printf("cores_str = [%s]\n", cores_str);
	return 0;
}

int
core_num(struct dpdkflow_context *ctx)
{
	int core_num = 0;
	for (int i = 0; i < ctx->core_num; i++) {
		core_num++;
	}
	printf("core_num = %d\n", core_num);
	return core_num;
}

int
port_index_max(struct dpdkflow_context *ctx)
{
	int port_index_max = -1;
	for (int i = 0; i < ctx->core_num; i++) {
		for (int j = 0; j < ctx->cores[i].port_num; j++) {
			if (ctx->cores[i].ports[j].index > port_index_max) {
				port_index_max = ctx->cores[i].ports[j].index;
			}
		}
	}
	printf("port_index_max = %d\n", port_index_max);
	return port_index_max;
}

static inline int
port_init(struct dpdkflow_context *ctx, uint16_t port)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port)) {
		return -1;
	}

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	if (rte_eth_dev_info_get(port, &dev_info) != 0) {
		printf("port_init: rte_eth_dev_info_get failed\n");
		return -1;
	}

	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
		port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
	}

	if (rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf) != 0) {
		printf("port_init: rte_eth_dev_configure failed\n");
		return -1;
	}

	if (rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd) != 0) {
		printf("port_init: rte_eth_dev_adjust_nb_rx_tx_desc failed\n");
		return -1;
	}

	for (q = 0; q < rx_rings; q++) {
		if (rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, ctx->mbuf_pool) < 0) {
			printf("port_init: rte_eth_rx_queue_setup failed\n");
			return -1;
		}
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	for (q = 0; q < tx_rings; q++) {
		if (rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf) < 0) {
			printf("port_init: rte_eth_tx_queue_setup failed\n");
			return -1;
		}
	}

	if (rte_eth_dev_start(port) < 0) {
		printf("port_init: rte_eth_dev_start failed\n");
		return -1;
	}

	if (rte_eth_promiscuous_enable(port) != 0) {
		printf("port_init: rte_eth_promiscuous_enable failed\n");
		return -1;
	}

	return 0;
}

uint64_t
now()
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return (uint64_t)t.tv_sec * 1000000 + t.tv_usec;
}

static int
lcore_flow(void *arg)
{
	struct dpdkflow_context *ctx = (struct dpdkflow_context *)arg;
	struct dpdkflow_context_core *me = NULL;
	unsigned my_core_id = rte_lcore_id();
	for (int i = 0; i < ctx->core_num; i++) {
		if (ctx->cores[i].index == my_core_id) {
			me = &ctx->cores[i];
		}
	}
	if (me == NULL) {
		printf("lcore_flow: context core not found\n");
		return -1;
	}
	printf("#### lcore_flow: %d\n", my_core_id);
	for (;;) {
		for (int j = 0; j < me->port_num; j++) {
			struct rte_mbuf *bufs[4];
			const uint16_t nb_rx = rte_eth_rx_burst(me->ports[j].index, 0, bufs, 4);
			uint64_t start_time = now();
			for (int k = 0; k < nb_rx; k++) {
				int8_t iface = me->ports[j].index;
				int8_t direction = -1;
				uint8_t af = 0;
				uint8_t proto = 0;
				int32_t vlan = me->ports[j].port_vlan_id;
				uint8_t src_host[16] = {0};
				uint8_t dst_host[16] = {0};
				int64_t src_as = -1;
				int64_t dst_as = -1;
				int src_port = -1;
				int dst_port = -1;
				uint32_t app = 0;
				struct dpdkflow_metric *m;
				if (rte_mempool_get(ctx->metric_pool, (void **)&m) < 0) {
					rte_rwlock_write_lock(&ctx->metric_stats_lock);
					ctx->metric_getfailed++;
					rte_rwlock_write_unlock(&ctx->metric_stats_lock);
					goto free_mbuf;
				} else {
					rte_rwlock_write_lock(&ctx->metric_stats_lock);
					ctx->metric_alloced++;
					rte_rwlock_write_unlock(&ctx->metric_stats_lock);
				}
				metric_init(m);
				m->start_time = start_time;
				m->packets = 1;
				m->bytes = bufs[k]->pkt_len;
				uint8_t *p = (uint8_t *)bufs[k]->buf_addr + bufs[k]->data_off;
				struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)p;
				p = (uint8_t *)(eth_hdr + 1);
				uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
				if (ether_type == 0x8100) {
					m->bytes -= 4; /* VLAN 拡張ヘッダ分は無視する。 */
					struct rte_vlan_hdr *vlan_hdr = (struct rte_vlan_hdr *)p;
					p = (uint8_t *)(vlan_hdr + 1);
					vlan = rte_be_to_cpu_16(vlan_hdr->vlan_tci) & 0x0fff;
					int vlan_included = 0;
					for (int l = 0; l < me->ports[j].tag_vlan_num; l++) {
						if (me->ports[j].tag_vlan_ids[l] == vlan)
							vlan_included += 1;
					}
					if (!vlan_included)
						goto free_metric;
					ether_type = rte_be_to_cpu_16(vlan_hdr->eth_proto);
				}
				switch (ether_type) {
				case 0x0800: /* IPv4 */
					{
						af = af_ipv4;
						struct rte_ipv4_hdr *ipv4_hdr = (struct rte_ipv4_hdr *)p;
						p = (uint8_t *)p + (ipv4_hdr->version_ihl & RTE_IPV4_HDR_IHL_MASK)
								 * RTE_IPV4_IHL_MULTIPLIER;
						if ((rte_be_to_cpu_16(ipv4_hdr->fragment_offset)
						  & RTE_IPV4_HDR_OFFSET_MASK) == 0) {
							proto = ipv4_hdr->next_proto_id;
						} else {
							proto = IPPROTO_FRAGMENT;
						}
						*((uint32_t *)&src_host[12]) = ipv4_hdr->src_addr;
						*((uint32_t *)&dst_host[12]) = ipv4_hdr->dst_addr;
					}
					break;
				case 0x86dd: /* IPv6 */
					{
						af = af_ipv6;
						struct rte_ipv6_hdr *ipv6_hdr = (struct rte_ipv6_hdr *)p;
						p = (uint8_t *)(ipv6_hdr + 1);
						proto = ipv6_hdr->proto;
						if (proto == IPPROTO_FRAGMENT) {
							struct ipv6_extension_fragment *frag_hdr =
								(struct ipv6_extension_fragment *)p;
							p = (uint8_t *)(frag_hdr + 1);
							if (frag_hdr->frag_data == 0) {
								proto = frag_hdr->next_header;
							}
						}
						memcpy(src_host, &ipv6_hdr->src_addr[0], 16);
						memcpy(dst_host, &ipv6_hdr->dst_addr[0], 16);
					}
					break;
				default:
					goto free_metric;
				}
				direction = get_direction(ctx, af, src_host, dst_host);
				if (aggregate_flag_up(ctx, direction, aggregate_f_src_as)) {
					src_as = mrt_rib_lookup(ctx, af, src_host);
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_dst_as)) {
					dst_as = mrt_rib_lookup(ctx, af, dst_host);
				}
				switch (proto) {
				case IPPROTO_UDP:
					{
						struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)p;
						p = (uint8_t *)(udp_hdr + 1);
						src_port = rte_be_to_cpu_16(udp_hdr->src_port);
						dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);
					}
					break;
				case IPPROTO_TCP:
					{
						struct rte_tcp_hdr *tcp_hdr = (struct rte_tcp_hdr *)p;
						p = (uint8_t *)(tcp_hdr + 1);
						src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
						dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
					}
					break;
				}
				int min_port = (src_port < dst_port) ? src_port : dst_port;
				if (min_port > 0) {
					app = ((uint32_t)proto << 16) | ((uint32_t)min_port);
				} else {
					app = ((uint32_t)proto << 16);
				}
				m->direction = direction;
				if (aggregate_flag_up(ctx, direction, aggregate_f_iface)) {
					m->iface = iface;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_af)) {
					m->af = af;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_proto)) {
					m->proto = proto;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_vlan)) {
					m->vlan = vlan;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_src_host)) {
					memcpy(&m->src_host[0], src_host, 16);
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_dst_host)) {
					memcpy(&m->dst_host[0], dst_host, 16);
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_src_as)) {
					m->src_as = src_as;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_dst_as)) {
					m->dst_as = dst_as;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_src_port)) {
					m->src_port = src_port;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_dst_port)) {
					m->dst_port = dst_port;
				}
				if (aggregate_flag_up(ctx, direction, aggregate_f_app)) {
					m->app = app;
					fill_app_desc(m->app_desc, m->app, ctx);
				}
				int stored;
				metric_update(ctx, m, &stored);
				if (stored) {
					/* コンテキストのメトリックテーブルに格納されたので put しない。 */
					goto free_mbuf;
				}
free_metric:
				rte_mempool_put(ctx->metric_pool, (void *)m);
				rte_rwlock_write_lock(&ctx->metric_stats_lock);
				ctx->metric_alloced--;
				rte_rwlock_write_unlock(&ctx->metric_stats_lock);
free_mbuf:
				rte_pktmbuf_free(bufs[k]);
			}
		}
	}
}

static void
lcore_main(struct dpdkflow_context *ctx)
{
	extern int gather(struct dpdkflow_metric *m);
	printf("#### lcore_main: %d\n", rte_lcore_id());
	while (!ctx->done) {
		struct dpdkflow_metric *mbuf[8];
		int deqed = metric_deq(ctx, mbuf, 8);
		if (deqed == 0) {
			usleep(500);
			continue;
		}
		for (int i = 0; i < deqed; i++) {
			//metric_print(mbuf[i]);
			gather(mbuf[i]);
			rte_mempool_put(ctx->metric_pool, (void *)mbuf[i]);
			rte_rwlock_write_lock(&ctx->metric_stats_lock);
			ctx->metric_sent++;
			ctx->metric_alloced--;
			rte_rwlock_write_unlock(&ctx->metric_stats_lock);
		}
	}
}

void
print_stats(struct dpdkflow_context *ctx)
{
	static uint64_t sent_last = 0;
	static uint64_t getfailed_last = 0;
	static uint64_t imissed_last[PORT_MAX] = {0};
	static uint64_t rx_nombuf_last[PORT_MAX] = {0};
	char buf[256];
	char buf2[128];
	char *p = buf;
	time_t rawtime;
	struct tm * timeinfo;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	sprintf(buf2, "%s", asctime(timeinfo));
	sprintf(p, "%s", buf2);
	p += strlen(buf2) - 1;

	sprintf(p, " stats: ");
	p += strlen(" stats: ");

	sprintf(buf2, "sent = %8ld alloced = %8ld getfailed = %8ld ",
			(ctx->metric_sent - sent_last),
			ctx->metric_alloced,
			(ctx->metric_getfailed - getfailed_last));
	sprintf(p, "%s", buf2);
	p += strlen(buf2);

	/*
	{
		int maxdepth = 0;
		for (int i = 0; i < ctx->metrics_num; i++) {
			struct dpdkflow_metric *tmp;
			int depth = 0;
			rte_rwlock_read_lock(&ctx->metric_lock);
			for (tmp = ctx->metric_hash_table[i]; tmp != NULL; tmp = tmp->hash_next) {
				depth++;
			}
			rte_rwlock_read_unlock(&ctx->metric_lock);
			if (depth > maxdepth) {
				maxdepth = depth;
			}
		}
		sprintf(buf2, "maxdepth = %4d ", maxdepth);
		sprintf(p, "%s", buf2);
		p += strlen(buf2);
	}
	*/

	sent_last = ctx->metric_sent;
	getfailed_last = ctx->metric_getfailed;
	for (int i = 0; i < ctx->core_num; i++) {
		for (int j = 0; j < ctx->cores[i].port_num; j++) {
			struct rte_eth_stats stats;
			rte_eth_stats_get(ctx->cores[i].ports[j].index, &stats);
			sprintf(buf2, " [%d] imissed = %8ld rx_nombuf = %8ld ",
					ctx->cores[i].ports[j].index,
					(stats.imissed - imissed_last[ctx->cores[i].ports[j].index]),
					(stats.rx_nombuf - rx_nombuf_last[ctx->cores[i].ports[j].index]));
			sprintf(p, "%s", buf2);
			p += strlen(buf2);
			imissed_last[ctx->cores[i].ports[j].index] = stats.imissed;
			rx_nombuf_last[ctx->cores[i].ports[j].index] = stats.rx_nombuf;
		}
	}

	sprintf(p, "\n");
	p += strlen("\n");

	printf("%s", buf);
}

int
check_and_reload_tables(struct dpdkflow_context *ctx)
{
	//printf("#### check_and_reload_tables:\n");
	if (mrt_rib_updated(ctx)) {
		mrt_rib_load(ctx);
	}
	if (app_table_updated(ctx)) {
		app_table_load(ctx);
	}
}

void
context_init(struct dpdkflow_context *ctx)
{
	uint64_t t1, t2;
	t1 = rte_rdtsc();
	sleep(1);
	t2 = rte_rdtsc();
	ctx->tsc1s = t2 - t1;

	ctx->metric_sent = 0;
	ctx->metric_alloced = 0;
	ctx->metric_getfailed = 0;
	rte_rwlock_init(&ctx->metric_stats_lock);

	mrt_rib_context_init(ctx);
	app_table_context_init(ctx);
	metric_context_init(ctx);
}

int
start(struct dpdkflow_context *ctx)
{
	int ret;
	int argc;
	char *argv[3];
	char cores_str[32];

	printf("start: beg\n");

	debug_print_ctx(ctx);

	ret = get_cores_str(ctx, cores_str, 32);
	if (ret < 0) {
		printf("start: cores_str failed\n");
		return -1;
	}
	argc = 3;
	argv[0] = "dpdkflow_cgo";
	argv[1] = "-l";
	argv[2] = cores_str;
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		printf("start: rte_eal_init failed: %d\n", ret);
		return -1;
	}

	if (rte_lcore_count() != core_num(ctx) + 1) {
		printf("start: core num mismatch\n");
		return -1;
	}

	if (rte_eth_dev_count_avail() <= port_index_max(ctx)) {
		printf("start: not enough ports\n");
		return -1;
	}

	ctx->mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool",
			NUM_MBUFS, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (ctx->mbuf_pool == NULL) {
		printf("start: mbuf_pool create failed\n");
		return -1;
	}

	ctx->metric_pool = rte_mempool_create("metric_pool",
			ctx->metrics_num, sizeof(struct dpdkflow_metric), 0, 0, NULL, NULL, NULL, NULL, rte_socket_id(), 0);
	if (ctx->metric_pool == NULL) {
		printf("start: metric_pool create failed\n");
		return -1;
	}

	for (int i = 0; i < ctx->core_num; i++) {
		for (int j = 0; j < ctx->cores[i].port_num; j++) {
			if (port_init(ctx, ctx->cores[i].ports[j].index) != 0) {
				printf("start: port_init failed: core%d port%d\n",
						ctx->cores[i].index, ctx->cores[i].ports[j].index);
				return -1;
			}
		}
	}

	context_init(ctx);

	printf("start: rte_eal_remote_launch beg\n");
	for (int i = 0; i < ctx->core_num; i++) {
		if (rte_eal_remote_launch(lcore_flow, ctx, ctx->cores[i].index)) {
			printf("start: rte_eal_remote_launch failed: core%d\n", ctx->cores[i].index);
			return -1;
		}
	}
	printf("start: rte_eal_remote_launch end\n");

	ctx->running = 1;

	lcore_main(ctx);

	rte_eal_cleanup();

	printf("start: end\n");
}
