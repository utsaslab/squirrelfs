#ifndef SUFS_LIBFS_HASH_H_
#define SUFS_LIBFS_HASH_H_

#include "../../include/libfs_config.h"
#include "types.h"

static inline u64 sufs_libfs_hash_int(u64 v)
{
	u64 x = v ^ (v >> 32) ^ (v >> 20) ^ (v >> 12);
	return x ^ (x >> 7) ^ (x >> 4);
}

static inline u64 sufs_libfs_hash_string(char *string, int max_size)
{
	u64 h = 0;
	for (int i = 0; i < max_size && string[i]; i++) {
		u64 c = string[i];
		/* Lifted from dcache.h in Linux v3.3 */
 		h = (h + (c << 4) + (c >> 4)) * 11;
	}
	return h;
}

#endif /* SUFS_HASH_H_ */
