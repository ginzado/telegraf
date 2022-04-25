#include "dpdkflow_cgo.h"

struct mrt_hdr {
	uint32_t timestamp;
	uint16_t type;
	uint16_t subtype;
	uint32_t length;
};

void
mrt_rib_table_add_ipv4(struct rte_lpm *lpm4, uint8_t *prefix, uint8_t prefix_len, uint32_t as_num)
{
	uint32_t ip = ntohl(*(uint32_t *)prefix);
	int ret = rte_lpm_add(lpm4, ip, prefix_len, as_num);
}

void
mrt_rib_table_add_ipv6(struct rte_lpm6 *lpm6, uint8_t *prefix, uint8_t prefix_len, uint32_t as_num)
{
	int ret = rte_lpm6_add(lpm6, prefix, prefix_len, as_num);
}

uint32_t
parse_as_path_attrs(char *buf, int len)
{
	//printf("parse_as_path_attrs: \n");
	uint32_t last_as_num = 0;
	uint8_t *p = buf;
	while ((size_t)p < (size_t)buf + len) {
		uint8_t segment_type = p[0];
		uint8_t segment_length = p[1];
		p += 2;
		uint32_t *as_num = (uint32_t *)p;
		p += 4 * segment_length;
		for (int i = 0; i < segment_length; i++) {
			last_as_num = ntohl(*as_num);
			as_num++;
		}
	}
	return last_as_num;
}

uint32_t
parse_attrs(uint8_t *buf, int len)
{
	//printf("parse_attrs: \n");
	uint8_t *p = buf;
	while ((size_t)p < (size_t)buf + len) {
		uint8_t attr_flags = p[0];
		uint8_t attr_type_code = p[1];
		uint16_t attr_len;
		p += 2;
		if (attr_flags & 0x10) {
			attr_len = ntohs(*(uint16_t *)p);
			p += 2;
		} else {
			attr_len = *p;
			p += 1;
		}
		if (attr_type_code == 2) {
			return parse_as_path_attrs(p, attr_len);
		}
		p += attr_len;
	}
	return 0;
}

void
parse_rib(uint8_t *buf, int len, uint16_t subtype, struct rte_lpm *lpm_ipv4, struct rte_lpm6 *lpm_ipv6)
{
	uint8_t *p = buf;
	uint32_t seq_num;
	uint8_t prefix_len;
	uint8_t prefix[16];
	int entry_count;
	int array_len;

	//printf("parse_rib: \n");

	seq_num = ntohl(*(uint32_t *)p);
	p += sizeof(uint32_t);
	prefix_len = *p;
	p += sizeof(uint8_t);
	array_len = ((prefix_len + 7) >> 3);
	memcpy(prefix, p, array_len);
	memset(prefix + array_len, 0, 16 - array_len);
	p += array_len;
	entry_count = ntohs(*(uint16_t *)p);
	p += sizeof(uint16_t);
	for (int i = 0; i < entry_count; i++) {
		uint32_t as_num;
		uint16_t peer_index = ntohs(*(uint16_t *)p);
		p += sizeof(uint16_t);
		uint32_t originated_time = ntohl(*(uint32_t *)p);
		p += sizeof(uint32_t);
		uint16_t attr_len = ntohs(*(uint16_t *)p);
		p += sizeof(uint16_t);
		as_num = parse_attrs(p, attr_len);
		p += attr_len;
		/*
		printf("%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %3d %6d\n",
				prefix[0], prefix[1], prefix[2], prefix[3],
				prefix[4], prefix[5], prefix[6], prefix[7],
				prefix[8], prefix[9], prefix[10], prefix[11],
				prefix[12], prefix[13], prefix[14], prefix[15],
				prefix_len, as_num);
		*/
		if (as_num > 0) {
			if (subtype == 2) {
				mrt_rib_table_add_ipv4(lpm_ipv4, prefix, prefix_len, as_num);
			}
			if (subtype == 4) {
				mrt_rib_table_add_ipv6(lpm_ipv6, prefix, prefix_len, as_num);
			}
			return;
		}
	}
}

uint32_t
mrt_rib_lookup(struct dpdkflow_context *ctx, uint8_t af, uint8_t *addr)
{
	uint32_t as_num;
	int ret;
	switch (af) {
	case AF_IPV4:
		{
			rte_rwlock_read_lock(&ctx->mrt_rib_lock);
			if (ctx->mrt_rib_table_ipv4 != NULL) {
				uint32_t ip = ntohl(*(uint32_t *)&addr[12]);
				ret = rte_lpm_lookup(ctx->mrt_rib_table_ipv4, ip, &as_num);
			}
			rte_rwlock_read_unlock(&ctx->mrt_rib_lock);
			if (ret == 0) {
				return as_num;
			}
		}
		break;
	case AF_IPV6:
		{
			rte_rwlock_read_lock(&ctx->mrt_rib_lock);
			if (ctx->mrt_rib_table_ipv6 != NULL) {
				ret = rte_lpm6_lookup(ctx->mrt_rib_table_ipv6, addr, &as_num);
			}
			rte_rwlock_read_unlock(&ctx->mrt_rib_lock);
			if (ret == 0) {
				return as_num;
			}
		}
		break;
	}
	return 0;
}

int
mrt_rib_updated(struct dpdkflow_context *ctx)
{
	struct stat statbuf;
	int ret = stat(ctx->mrt_rib_path, &statbuf);
	if (ret != 0) {
		printf("mrt_rib_updated: stat failed\n");
		return 0;
	}
	if ((statbuf.st_mtim.tv_sec > ctx->mrt_rib_last_mtim.tv_sec)
	 || ((statbuf.st_mtim.tv_sec == ctx->mrt_rib_last_mtim.tv_sec)
	  && (statbuf.st_mtim.tv_nsec > ctx->mrt_rib_last_mtim.tv_nsec))) {
		return 1;
	}
	return 0;
}

int
mrt_rib_load(struct dpdkflow_context *ctx)
{
	int ret;
	FILE *fp;
	uint8_t buf[8192];
	struct mrt_hdr mrt_hdr;
	struct rte_lpm *new_lpm4 = NULL, *old_lpm4;
	struct rte_lpm6 *new_lpm6 = NULL, *old_lpm6;
	char new_lpm4_name[256];
	char new_lpm6_name[256];
	struct rte_lpm_config lpm4_config = {
		.max_rules = 2097152,
		.number_tbl8s = (1 << 8),
		.flags = 0,
	};
	struct rte_lpm6_config lpm6_config = {
		.max_rules = 524288,
		.number_tbl8s = (1 << 16),
		.flags = 0,
	};
	struct stat statbuf;

	printf("mrt_rib_load: beg\n");

	ret = stat(ctx->mrt_rib_path, &statbuf);
	if (ret != 0) {
		printf("mrt_rib_load: stat failed\n");
		return -1;
	}

	sprintf(new_lpm4_name, "mrt_rib_table_ipv4_%d", ctx->mrt_rib_table_ipv4_seq++);
	new_lpm4 = rte_lpm_create(new_lpm4_name, rte_socket_id(), &lpm4_config);
	if (new_lpm4 == NULL) {
		printf("mrt_rib_load: rte_lpm_create failed\n");
		goto failed_1;
	}
	sprintf(new_lpm6_name, "mrt_rib_table_ipv6_%d", ctx->mrt_rib_table_ipv6_seq++);
	new_lpm6 = rte_lpm6_create(new_lpm6_name, rte_socket_id(), &lpm6_config);
	if (new_lpm6 == NULL) {
		printf("mrt_rib_load: rte_lpm6_create failed\n");
		goto failed_2;
	}

	fp = fopen(ctx->mrt_rib_path, "rb");
	if (fp == NULL) {
		printf("mrt_rib_load: fopen failed\n");
		goto failed_3;
	}
	while (1) {
		ret = fread(&mrt_hdr, sizeof(struct mrt_hdr), 1, fp);
		if (ret != 1) {
			fclose(fp);
			break;
		}
		mrt_hdr.timestamp = ntohl(mrt_hdr.timestamp);
		mrt_hdr.type = ntohs(mrt_hdr.type);
		mrt_hdr.subtype = ntohs(mrt_hdr.subtype);
		mrt_hdr.length = ntohl(mrt_hdr.length);
		if (mrt_hdr.length > 8192) {
			printf("mrt_rib_load: too big\n");
			goto failed_4;
		}
		ret = fread(buf, mrt_hdr.length, 1, fp);
		if (ret != 1) {
			printf("mrt_rib_load: buf read failed\n");
			goto failed_4;
		}
		if (mrt_hdr.type == 13 && (mrt_hdr.subtype == 2 || mrt_hdr.subtype == 4)) {
			parse_rib(buf, mrt_hdr.length, mrt_hdr.subtype, new_lpm4, new_lpm6);
		}
	}

	rte_rwlock_write_lock(&ctx->mrt_rib_lock);
	{
		old_lpm4 = ctx->mrt_rib_table_ipv4;
		ctx->mrt_rib_table_ipv4 = new_lpm4;
		old_lpm6 = ctx->mrt_rib_table_ipv6;
		ctx->mrt_rib_table_ipv6 = new_lpm6;
		ctx->mrt_rib_last_mtim = statbuf.st_mtim;
	}
	rte_rwlock_write_unlock(&ctx->mrt_rib_lock);

	if (old_lpm4 != NULL) {
		printf("mrt_rib_load: free old mrt rib table ipv4\n");
		rte_lpm_free(old_lpm4);
	}
	if (old_lpm6 != NULL) {
		printf("mrt_rib_load: free old mrt rib table ipv6\n");
		rte_lpm6_free(old_lpm6);
	}

	printf("mrt_rib_load: end\n");
	return 0;

failed_4:
	fclose(fp);
failed_3:
	rte_lpm6_free(new_lpm6);
failed_2:
	rte_lpm_free(new_lpm4);
failed_1:
	return -1;
}

void
mrt_rib_context_init(struct dpdkflow_context *ctx)
{
	printf("mrt_rib_context_init\n");

	ctx->mrt_rib_table_ipv4 = NULL;
	ctx->mrt_rib_table_ipv4_seq = 0;
	ctx->mrt_rib_table_ipv6 = NULL;
	ctx->mrt_rib_table_ipv6_seq = 0;

	rte_rwlock_init(&ctx->mrt_rib_lock);

	mrt_rib_load(ctx);
}
