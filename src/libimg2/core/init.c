/**
 * @file init.c
 * @brief img2_init() / img2_shutdown(): reference-counted process-global setup
 *
 * No API requires img2_init(): the codec tables are const and the font cache
 * initialises lazily. img2_init() emits the INFO registry lists; the final
 * img2_shutdown() frees the font cache. If the font cache is built lazily while
 * no img2_init() reference is held, that build takes one implicit reference that
 * is never released: the cache (and every handle / string obtained from it) then
 * stays valid until process exit, still reachable from the font_manager statics.
 * Any other img2_* call running concurrently with the final img2_shutdown() is
 * unsupported.
 */

#include <pthread.h>
#include <stdbool.h>

#include "status.h"
#include "../decoders/decoder.h"
#include "../encoders/encoder.h"
#include "../text/font_manager.h"

/* [R19] init/shutdown bodies are serialised by s_init_lock; s_refs changes only under it. */
static pthread_mutex_t s_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_refs; /* guarded by s_init_lock; includes the implicit font-cache reference */
static bool s_font_implicit_ref; /* guarded by s_init_lock; set once by a lazy font-cache build, never cleared */

img2_status_t img2_init(void)
{
	pthread_mutex_lock(&s_init_lock);
	if (s_refs++ == (s_font_implicit_ref ? 1 : 0)) { /* first explicit reference */
		img2_encoder_registry_init(); /* encoders before decoders: main.c order */
		img2_decoder_registry_init();
	}
	pthread_mutex_unlock(&s_init_lock);
	return IMG2_OK;
}

void img2_shutdown(void)
{
	pthread_mutex_lock(&s_init_lock);
	/* [R5] unbalanced shutdown is ignored; never drops below the implicit font-cache reference */
	if (s_refs > (s_font_implicit_ref ? 1 : 0) && --s_refs == 0) {
		img2_font_manager_cleanup_final(&s_refs); /* re-checks s_refs == 0 under s_font_lock */
	}
	pthread_mutex_unlock(&s_init_lock);
}

void img2_font_cache_init_lazy(void)
{
	pthread_mutex_lock(&s_init_lock); /* lock order s_init_lock -> s_font_lock */
	if (img2_font_manager_init_lazy() && s_refs == 0 && !s_font_implicit_ref) {
		s_font_implicit_ref = true;
		s_refs++;
	}
	pthread_mutex_unlock(&s_init_lock);
}
