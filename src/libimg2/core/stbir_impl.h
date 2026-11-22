/**
 * @file stbir_impl.h
 * @brief Includes the vendored stb_image_resize2.h (implementation, core/image.c only)
 *
 * Sanitizer builds (IMG2_SANITIZE, docs/sanitizers.md): stb_image_resize2 does unaligned 8-byte moves through
 * uint64 pointers (STBIR_MOVE_2 & co.) and indexes its sRGB tables through a pointer offset below the array start
 * (fp32_to_srgb8_tab4 - (127-13)*8); both are deliberate and flagged by UBSan's alignment and array-bounds
 * checks. The attribute covers the functions defined by the vendored header only (not libimg2's own code) and
 * has no effect without -fsanitize. ASan still checks every access of these functions.
 */
#ifndef IMG2_CORE_STBIR_IMPL_H
#define IMG2_CORE_STBIR_IMPL_H

#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("alignment", "bounds"))), apply_to = function)
#endif
#include "stb_image_resize2.h"
#if defined(__clang__)
#pragma clang attribute pop
#endif

#endif /* IMG2_CORE_STBIR_IMPL_H */
