#include "dpdkflow_cgo.h"

inline int
metric_equals(struct dpdkflow_context *ctx, struct dpdkflow_metric *m1, struct dpdkflow_metric *m2)
{
	int8_t direction;
	if (m1->direction != m2->direction) {
		return 0;
	}
	direction = m1->direction;
	if (aggregate_flag_up(ctx, direction, aggregate_f_iface) && m1->iface != m2->iface) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_af) && m1->af != m2->af) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_proto) && m1->proto != m2->proto) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_vlan) && m1->vlan != m2->vlan) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_src_host)) {
		for (int i = 0; i < 4; i++) {
			if (*(uint32_t *)&m1->src_host[i * 4] != *(uint32_t *)&m2->src_host[i * 4]) {
				return 0;
			}
		}
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_dst_host)) {
		for (int i = 0; i < 4; i++) {
			if (*(uint32_t *)&m1->dst_host[i * 4] != *(uint32_t *)&m2->dst_host[i * 4]) {
				return 0;
			}
		}
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_src_as) && m1->src_as != m2->src_as) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_dst_as) && m1->dst_as != m2->dst_as) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_src_port) && m1->src_port != m2->src_port) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_dst_port) && m1->dst_port != m2->dst_port) {
		return 0;
	}
	if (aggregate_flag_up(ctx, direction, aggregate_f_app) && m1->app != m2->app) {
		return 0;
	}
	return 1;
}

inline uint32_t
metric_hash(struct dpdkflow_context *ctx, struct dpdkflow_metric *m)
{
	uint32_t hash = 0;
	/*
	uint32_t hash = (uint32_t)m->direction << 12;
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_iface)) {
		hash += m->iface;
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_af)) {
		hash += m->af;
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_proto)) {
		hash += m->proto;
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_vlan)) {
		hash += m->vlan;
	}
	*/
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_src_host)) {
		hash += ntohl(*(uint32_t *)&m->src_host[12]);
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_dst_host)) {
		hash += ntohl(*(uint32_t *)&m->dst_host[12]);
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_src_as)) {
		hash += ((uint32_t)m->src_as << 4) | ((uint32_t)m->src_as >> 28);
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_dst_as)) {
		hash += ((uint32_t)m->dst_as << 4) | ((uint32_t)m->dst_as >> 28);
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_src_port)) {
		hash += ((uint32_t)m->src_port << 8) | ((uint32_t)m->src_port >> 24);
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_dst_port)) {
		hash += ((uint32_t)m->dst_port << 8) | ((uint32_t)m->dst_port >> 24);
	}
	if (aggregate_flag_up(ctx, m->direction, aggregate_f_app)) {
		hash += ((uint32_t)m->app << 12) | ((uint32_t)m->app >> 20);
	}
	hash ^= (hash >> 20);
	hash ^= (hash >> 10);
	hash ^= (hash >> 5);
	hash &= ctx->metrics_num - 1;
	return hash;
}

int
metric_deq(struct dpdkflow_context *ctx, struct dpdkflow_metric **mbuf, int mbuf_size)
{
	uint64_t current_time = now();
	uint64_t start_time;
	uint64_t interval_usec = ((uint64_t)ctx->interval * 1000000);
	int can_deq = 0;
	struct dpdkflow_metric *head = NULL;
	rte_rwlock_read_lock(&ctx->metric_lock);
	{
		start_time = ctx->metric_list_head->start_time;
		if ((ctx->metric_list_head != NULL) && (current_time - start_time) >= interval_usec) {
			can_deq = 1;
		}
	}
	rte_rwlock_read_unlock(&ctx->metric_lock);
	if (!can_deq) {
		return 0;
	}
	int filled = 0;
	rte_rwlock_write_lock(&ctx->metric_lock);
	{
		int i;
		for (i = 0; i < mbuf_size && ctx->metric_list_head != NULL; i++) {
			start_time = ctx->metric_list_head->start_time;
			if ((current_time - start_time) < interval_usec) {
				break;
			}
			head = ctx->metric_list_head;
			ctx->metric_list_head = head->list_next;
			if (ctx->metric_list_head == NULL) {
				ctx->metric_list_tail = NULL;
			}
			uint32_t hash = metric_hash(ctx, head);
			if (ctx->metric_hash_table[hash] == head) {
				ctx->metric_hash_table[hash] = head->hash_next;
			} else {
				struct dpdkflow_metric *tmp;
				for (tmp = ctx->metric_hash_table[hash]; tmp != NULL; tmp = tmp->hash_next) {
					if (tmp->hash_next == head) {
						tmp->hash_next = head->hash_next;
						break;
					}
				}
			}
			mbuf[i] = head;
		}
		filled = i;
	}
	rte_rwlock_write_unlock(&ctx->metric_lock);
	for (int i = 0; i < filled; i++) {
		mbuf[i]->hash_next = NULL;
		mbuf[i]->list_next = NULL;
		mbuf[i]->stop_time = current_time;
	}
	return filled;
}

void
metric_update(struct dpdkflow_context *ctx, struct dpdkflow_metric *m, int *stored)
{
	struct dpdkflow_metric *tmp;
	uint32_t hash = metric_hash(ctx, m);
	rte_rwlock_read_lock(&ctx->metric_lock);
	{
		for (tmp = ctx->metric_hash_table[hash]; tmp != NULL; tmp = tmp->hash_next) {
			if (metric_equals(ctx, tmp, m)) {
				break;
			}
		}
		if (tmp != NULL) {
			/* XXX: */
			tmp->packets += m->packets;
			tmp->bytes += m->bytes;
		}
	}
	rte_rwlock_read_unlock(&ctx->metric_lock);
	if (tmp != NULL) {
		*stored = 0;
		return;
	}
	rte_rwlock_write_lock(&ctx->metric_lock);
	{
		m->hash_next = ctx->metric_hash_table[hash];
		ctx->metric_hash_table[hash] = m;
		if (ctx->metric_list_tail != NULL) {
			ctx->metric_list_tail->list_next = m;
			ctx->metric_list_tail = m;
		} else {
			ctx->metric_list_head = m;
			ctx->metric_list_tail = m;
		}
	}
	rte_rwlock_write_unlock(&ctx->metric_lock);
	*stored = 1;
	return;
}

inline void
metric_print(struct dpdkflow_metric *m)
{
	printf("%2d %2d %2d %2d %4d "
	       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x "
	       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x "
	       "%10ld %10ld %6d %6d %08x %s\n",
			m->iface,
			m->direction,
			m->af,
			m->proto,
			m->vlan,
			m->src_host[0], m->src_host[1], m->src_host[2], m->src_host[3],
			m->src_host[4], m->src_host[5], m->src_host[6], m->src_host[7],
			m->src_host[8], m->src_host[9], m->src_host[10], m->src_host[11],
			m->src_host[12], m->src_host[13], m->src_host[14], m->src_host[15],
			m->dst_host[0], m->dst_host[1], m->dst_host[2], m->dst_host[3],
			m->dst_host[4], m->dst_host[5], m->dst_host[6], m->dst_host[7],
			m->dst_host[8], m->dst_host[9], m->dst_host[10], m->dst_host[11],
			m->dst_host[12], m->dst_host[13], m->dst_host[14], m->dst_host[15],
			m->src_as,
			m->dst_as,
			m->src_port,
			m->dst_port,
			m->app,
			m->app_desc);
}

void
metric_init(struct dpdkflow_metric *m)
{
	memset(m, 0, sizeof(struct dpdkflow_metric));
	m->iface = -1;
	m->direction = -1;
	m->vlan = -1;
	m->src_as = -1;
	m->dst_as = -1;
	m->src_port = -1;
	m->dst_port = -1;
}

void
metric_context_init(struct dpdkflow_context *ctx)
{
	printf("metric_context_init\n");

	ctx->metric_hash_table = malloc(sizeof(struct dpdkflow_metric *) * ctx->metrics_num);
	for (int i = 0; i < ctx->metrics_num; i++) {
		ctx->metric_hash_table[i] = NULL;
	}
	ctx->metric_list_head = NULL;
	ctx->metric_list_tail = NULL;

	rte_rwlock_init(&ctx->metric_lock);
}
