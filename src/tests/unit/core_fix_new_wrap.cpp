/**
 * @file core_fix_new_wrap.cpp
 * @brief Test-only operator new fault injection through the GNU ld --wrap option (Linux only)
 *
 * A test executable linked with -Wl,--wrap=_Znwm,--wrap=_Znam routes every `new` / `new[]` of its own objects
 * and of the statically linked img2 archive (LibRaw) through the wrappers below. core_fix_new_arm(n) makes the
 * n-th (0-based) call after it throw std::bad_alloc, as a failed allocation does; later calls succeed.
 */

#include <cstddef>
#include <new>

extern "C" {
void *__real__Znwm(std::size_t size);
void *__real__Znam(std::size_t size);
void *__wrap__Znwm(std::size_t size);
void *__wrap__Znam(std::size_t size);
void core_fix_new_arm(long n);
void core_fix_new_disarm(void);
int core_fix_new_fired(void);
}

static long s_countdown = -1; /* -1: disarmed */
static int s_fired;

void core_fix_new_arm(long n)
{
	s_fired = 0;
	s_countdown = n;
}

void core_fix_new_disarm(void)
{
	s_countdown = -1;
}

int core_fix_new_fired(void)
{
	return s_fired;
}

static void tick(void)
{
	if (s_countdown < 0) {
		return;
	}
	if (s_countdown-- == 0) {
		s_fired = 1;
		throw std::bad_alloc();
	}
}

void *__wrap__Znwm(std::size_t size)
{
	tick();
	return __real__Znwm(size);
}

void *__wrap__Znam(std::size_t size)
{
	tick();
	return __real__Znam(size);
}
