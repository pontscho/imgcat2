/**
 * @file alloc_fail.h
 * @brief Test-only allocation fault injection through the GNU ld --wrap option
 *
 * libimg2 calls malloc/calloc/realloc/strdup directly (no allocator wrapper). A test executable linked with
 *   -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup
 * (img2_add_test(... ALLOC_FAIL), Linux/GNU ld only) routes every such call made from its own objects and from
 * the statically linked img2 archive through the __wrap_* functions below; calls made inside shared libraries
 * (libc itself, libstdc++'s operator new) are not affected. Nothing is compiled into the library.
 *
 * Include this header in exactly ONE translation unit of the test executable (it defines the wrappers).
 *
 * Usage: af_arm(n) makes the n-th (0-based) wrapped allocation after the call fail (returns NULL), every
 * later one succeeds; af_disarm() stops counting; af_fired() tells whether the armed failure happened.
 * af_for_each_failure() runs an operation with n = 0, 1, 2, ... until a run completes without the injected
 * failure firing, so every allocation of the operation is failed once.
 */

#ifndef IMGCAT2_TESTS_ALLOC_FAIL_H
#define IMGCAT2_TESTS_ALLOC_FAIL_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
char *__real_strdup(const char *s);

void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__wrap_realloc(void *ptr, size_t size);
char *__wrap_strdup(const char *s);

static long s_af_countdown = -1; /* -1: disarmed */
static bool s_af_fired;
static long s_af_calls; /* wrapped calls seen while armed */

static inline void af_arm(long n)
{
	s_af_fired = false;
	s_af_calls = 0;
	s_af_countdown = n;
}

static inline void af_disarm(void)
{
	s_af_countdown = -1;
}

static inline bool af_fired(void)
{
	return s_af_fired;
}

static inline long af_calls(void)
{
	return s_af_calls;
}

/* true: the current call must fail */
static bool af_tick(void)
{
	if (s_af_countdown < 0) {
		return false;
	}
	s_af_calls++;
	if (s_af_countdown == 0) {
		s_af_countdown = -1;
		s_af_fired = true;
		return true;
	}
	s_af_countdown--;
	return false;
}

void *__wrap_malloc(size_t size)
{
	return af_tick() ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	return af_tick() ? NULL : __real_calloc(nmemb, size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
	return af_tick() ? NULL : __real_realloc(ptr, size);
}

char *__wrap_strdup(const char *s)
{
	if (af_tick()) {
		return NULL;
	}
	size_t n = strlen(s) + 1;
	char *p = __real_malloc(n);
	if (p != NULL) {
		memcpy(p, s, n);
	}
	return p;
}

/**
 * @brief Run op(ctx) once per allocation it performs, failing that allocation
 *
 * op must clean up after itself (it returns normally whether or not it succeeded). Returns the number of
 * injected failures (runs in which the armed allocation failed); stops at max_runs.
 */
static inline long af_for_each_failure(void (*op)(void *ctx), void *ctx, long max_runs)
{
	long n = 0;
	for (; n < max_runs; n++) {
		af_arm(n);
		op(ctx);
		bool fired = af_fired();
		af_disarm();
		if (!fired) {
			break;
		}
	}
	return n;
}

/** @brief Number of wrapped allocations op(ctx) performs (nothing fails) */
static inline long af_count_allocs(void (*op)(void *ctx), void *ctx)
{
	af_arm(0x7fffffffL);
	op(ctx);
	long n = af_calls();
	af_disarm();
	return n;
}

/** @brief Run op(ctx) once for each n in [from, to), failing its n-th allocation */
static inline void af_fail_range(void (*op)(void *ctx), void *ctx, long from, long to)
{
	for (long n = from < 0 ? 0 : from; n < to; n++) {
		af_arm(n);
		op(ctx);
		af_disarm();
	}
}

#endif /* IMGCAT2_TESTS_ALLOC_FAIL_H */
