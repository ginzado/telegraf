#include "dpdkflow_cgo.h"

inline uint32_t
app_hash(uint32_t app)
{
	uint32_t hash = app;
	hash ^= (hash >> 20);
	hash ^= (hash >> 10);
	hash ^= (hash >> 5);
	hash &= APP_TABLE_HASH_SIZE - 1;
	return hash;
}

void
fill_app_desc(char *app_desc, uint32_t app, struct dpdkflow_context *ctx)
{
	struct dpdkflow_app_table_entry *tmp;
	uint32_t hash = app_hash(app);
	rte_rwlock_read_lock(&ctx->app_table_lock);
	{
		for (tmp = ctx->app_table->app_table_hash_table[hash]; tmp != NULL; tmp = tmp->next) {
			if (tmp->app == app) {
				break;
			}
		}
		if (tmp != NULL) {
			strcpy(app_desc, tmp->app_desc);
		}
	}
	rte_rwlock_read_unlock(&ctx->app_table_lock);
	if (tmp != NULL) {
		return;
	}
	switch (app >> 16) {
	case 6:
		/* tcp */
		snprintf(app_desc, APP_DESC_LEN, "tcp(6)/unknown(%d)", (app & 0xffff));
		break;
	case 17:
		/* udp */
		snprintf(app_desc, APP_DESC_LEN, "udp(17)/unknown(%d)", (app & 0xffff));
		break;
	default:
		snprintf(app_desc, APP_DESC_LEN, "unknown(%d)", (app & 0xffff));
	}
}

int
app_table_updated(struct dpdkflow_context *ctx)
{
	struct stat statbuf;
	int ret;
	ret = stat("/etc/protocols", &statbuf);
	if (ret != 0) {
		printf("app_table_updated: protocols stat failed\n");
		return 0;
	}
	if ((statbuf.st_mtim.tv_sec > ctx->protocols_last_mtim.tv_sec)
	 || ((statbuf.st_mtim.tv_sec == ctx->protocols_last_mtim.tv_sec)
	  && (statbuf.st_mtim.tv_nsec > ctx->protocols_last_mtim.tv_nsec))) {
		return 1;
	}
	ret = stat("/etc/services", &statbuf);
	if (ret != 0) {
		printf("app_table_updated: services stat failed\n");
		return 0;
	}
	if ((statbuf.st_mtim.tv_sec > ctx->services_last_mtim.tv_sec)
	 || ((statbuf.st_mtim.tv_sec == ctx->services_last_mtim.tv_sec)
	  && (statbuf.st_mtim.tv_nsec > ctx->services_last_mtim.tv_nsec))) {
		return 1;
	}
	return 0;
}

int
app_table_load(struct dpdkflow_context *ctx)
{
	int ret;
	struct protoent *pe;
	struct servent *se;
	struct dpdkflow_app_table *new_app_table, *old_app_table;
	struct stat statbuf_protocols, statbuf_services;

	printf("app_table_load: beg\n");

	ret = stat("/etc/protocols", &statbuf_protocols);
	if (ret != 0) {
		printf("app_table_load: stat protocols failed\n");
		return -1;
	}

	ret = stat("/etc/services", &statbuf_services);
	if (ret != 0) {
		printf("app_table_load: stat services failed\n");
		return -1;
	}

	new_app_table = malloc(sizeof(struct dpdkflow_app_table));
	if (new_app_table == NULL) {
		printf("app_table_load: new app table alloc failed\n");
		return -1;
	}
	for (int i = 0; i < APP_TABLE_HASH_SIZE; i++) {
		new_app_table->app_table_hash_table[i] = NULL;
	}
	for (uint32_t proto = 0; proto < 256; proto++) {
		pe = getprotobynumber(proto);
		if (!pe) {
			continue;
		}
		switch (proto) {
		case 6:
			/* tcp */
			{
				for (uint32_t serv = 0; serv < 65536; serv++) {
					se = getservbyport(htons(serv), "tcp");
					if (!se) {
						continue;
					}
					struct dpdkflow_app_table_entry *e;
					e = malloc(sizeof(struct dpdkflow_app_table_entry));
					if (e == NULL) {
						goto failed;
					}
					e->app = (proto << 16) | serv;
					snprintf(e->app_desc, APP_DESC_LEN, "tcp(6)/%s(%d)", se->s_name, serv);
					uint32_t hash = app_hash(e->app);
					e->next = new_app_table->app_table_hash_table[hash];
					new_app_table->app_table_hash_table[hash] = e;
				}
			}
			break;
		case 17:
			/* udp */
			{
				se = NULL;
				for (uint32_t serv = 0; serv < 65536; serv++) {
					se = getservbyport(htons(serv), "udp");
					if (!se) {
						continue;
					}
					struct dpdkflow_app_table_entry *e;
					e = malloc(sizeof(struct dpdkflow_app_table_entry));
					if (e == NULL) {
						goto failed;
					}
					e->app = (proto << 16) | serv;
					snprintf(e->app_desc, APP_DESC_LEN, "udp(17)/%s(%d)", se->s_name, serv);
					uint32_t hash = app_hash(e->app);
					e->next = new_app_table->app_table_hash_table[hash];
					new_app_table->app_table_hash_table[hash] = e;
				}
			}
			break;
		default:
			{
				struct dpdkflow_app_table_entry *e;
				e = malloc(sizeof(struct dpdkflow_app_table_entry));
				if (e == NULL) {
					goto failed;
				}
				e->app = proto << 16;
				snprintf(e->app_desc, APP_DESC_LEN, "%s(%d)", pe->p_name, proto);
				uint32_t hash = app_hash(e->app);
				e->next = new_app_table->app_table_hash_table[hash];
				new_app_table->app_table_hash_table[hash] = e;
			}
		}
	}

	int app_desc_len_max = 0;
	int max_link_depth = 0;
	for (int i = 0; i < APP_TABLE_HASH_SIZE; i++) {
		struct dpdkflow_app_table_entry *tmp;
		int i = 0;
		for (tmp = new_app_table->app_table_hash_table[i]; tmp != NULL; tmp = tmp->next) {
			i++;
			if (i > max_link_depth) {
				max_link_depth = i;
			}
			if (strlen(tmp->app_desc) > app_desc_len_max) {
				app_desc_len_max = strlen(tmp->app_desc);
			}
		}
	}
	printf("app_table_load: app_desc_len_max = %d\n", app_desc_len_max);
	printf("app_table_load: max_link_depth = %d\n", max_link_depth);

	rte_rwlock_write_lock(&ctx->app_table_lock);
	{
		old_app_table = ctx->app_table;
		ctx->app_table = new_app_table;
		ctx->protocols_last_mtim = statbuf_protocols.st_mtim;
		ctx->services_last_mtim = statbuf_services.st_mtim;
	}
	rte_rwlock_write_unlock(&ctx->app_table_lock);

	if (old_app_table != NULL) {
		for (int i = 0; i < APP_TABLE_HASH_SIZE; i++) {
			struct dpdkflow_app_table_entry *tmp, *next;
			for (tmp = old_app_table->app_table_hash_table[i]; tmp != NULL; tmp = next) {
				next = tmp->next;
				free(tmp);
			}
		}
		free(old_app_table);
	}

	printf("app_table_load: end\n");
	return 0;

failed:
	if (new_app_table != NULL) {
		for (int i = 0; i < APP_TABLE_HASH_SIZE; i++) {
			struct dpdkflow_app_table_entry *tmp, *next;
			for (tmp = new_app_table->app_table_hash_table[i]; tmp != NULL; tmp = next) {
				next = tmp->next;
				free(tmp);
			}
		}
		free(new_app_table);
	}
	return -1;
}

void
app_table_context_init(struct dpdkflow_context *ctx)
{
	printf("app_table_context_init\n");

	ctx->app_table = NULL;
	rte_rwlock_init(&ctx->app_table_lock);

	app_table_load(ctx);
}
