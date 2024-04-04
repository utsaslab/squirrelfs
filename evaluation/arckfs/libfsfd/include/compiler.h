#ifndef SUFS_LIBFS_COMPILER_H_
#define SUFS_LIBFS_COMPILER_H_

#include "../../include/libfs_config.h"

#define __XCONCAT2(a, b) a##b
#define __XCONCAT(a, b) __XCONCAT2(a, b)

#define __padout__                                                         \
	char __XCONCAT(__padout, __COUNTER__)[0]                               \
		__attribute__((aligned(SUFS_CACHELINE)))
#define __mpalign__ __attribute__((aligned(SUFS_CACHELINE)))
#define __noret__ __attribute__((noreturn))
#define sufs_barrier() __asm volatile("" ::: "memory")

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif

#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif

/* TODO: Port the actual implementation of WRITE_ONCE */
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ((x) = (val))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    (type *)( (char *)(ptr) - offsetof(type,member) )
#endif

#endif /* SUFS_COMPILER_H_ */
