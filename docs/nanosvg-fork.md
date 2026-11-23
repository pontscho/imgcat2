# nanosvg fork

img2 renders SVG (when resvg is off) with a modified copy of [nanosvg](https://github.com/memononen/nanosvg)
(zlib licence; the img2 changes are under the same terms).

## Where the code lives

- **The fork, compiled into img2:** `src/libimg2/decoders/nanosvg.h` and `src/libimg2/decoders/nanosvgrast.h`,
  included only by `src/libimg2/decoders/decoder_nanosvg.c` (`NANOSVG_IMPLEMENTATION`,
  `NANOSVGRAST_IMPLEMENTATION`, both TU-local via `NSVG_DEF` / `NSVGRAST_DEF` = `static`). img2 changes are
  marked `[img2]` in the comments where they are not obvious.
- **The upstream baseline:** the `vendor/nanosvg` submodule, pinned at upstream `5cefd98`, kept pristine. It is not
  on any include path and is not compiled; it exists only to diff against and to merge upstream fixes from.

## What differs from upstream

`<text>` / `<tspan>` support (fork of 2026-01-18), in `nanosvg.h`:

- parser state in `NSVGparser` (position, `font-family`, `font-size`, `text-anchor`, `font-weight`, `font-style`,
  the accumulated text content);
- `nsvg__parseText` / `nsvg__parseTspan` (`x`, `y`, `dx`, `dy`), dispatch in `nsvg__startElement` /
  `nsvg__endElement`, content capture in `nsvg__content`;
- `nsvg__decodeUTF8`, `nsvg__measureText`, `nsvg__addGlyphToPaths` (TrueType quadratics converted to cubics,
  `y = baseline - y_font * scale`), `nsvg__renderTextToPath`. Glyphs come from the font manager
  (`img2_font_find`, `img2_font_face_*` in `src/libimg2/text/font_manager.h`; stb_truetype stays behind those
  wrappers). Font fallback: exact match, family regular, generic mapping, embedded DejaVu Sans Mono.
- Not supported: `<textPath>`, `writing-mode`, `text-decoration`, `letter-spacing` / `word-spacing`, OpenType
  features, `baseline-shift`, per-glyph `rotate`.
- The kerning arguments of the render loop are reversed compared with `nsvg__measureText`; kept on purpose for byte
  identity (see docs/libimg2.md).

Library integration (both headers): `NSVG_DEF` / `NSVGRAST_DEF` linkage on every non-static definition (011dd6d),
diagnostics through `img2_log` instead of `fprintf(stderr)` (f6f2d4a), include paths after the move to
`src/libimg2` (46e3eee, 4c0023a).

Fixes on top (each with a regression test):

- `da68f6b` nanosvgrast.h: saturate the rasterizer's fixed-point edge x / dx (float to int overflow found by fuzzing;
  `test_fuzz_regress`).
- `ebdbc33` nanosvg.h: the text UTF-8 decoder stops at the terminating NUL of a truncated sequence
  (`test_fuzz_regress`).
- `e8e9242` nanosvg.h: `matrix()` / `translate()` / `scale()` / `skewX()` / `skewY()` / `rotate()` with missing
  numbers no longer read uninitialised stack slots (`test_svg_state`).
- `689f456` nanosvgrast.h: new active edges are merged in one pass (same order as upstream's insertion), so `<text>`
  rendering is linear in the text length (`test_svg_text_perf`).
- `8de76b4` nanosvg.h: an over-long `<text>` run is truncated at a code point boundary instead of dropped
  (`test_svg_text_cap`).

## Merging upstream fixes

1. Move the baseline: `git -C vendor/nanosvg fetch && git -C vendor/nanosvg checkout <new commit>`.
2. See what upstream changed: `git -C vendor/nanosvg diff 5cefd98 <new commit> -- src/`.
3. Port those hunks into the fork by hand. Conflicts are likely in `NSVGparser`, `nsvg__startElement` /
   `nsvg__endElement`, `nsvg__content`, the transform parsers and `nsvg__rasterizeSortedEdges`. To review the whole
   fork delta: `diff -u vendor/nanosvg/src/nanosvg.h src/libimg2/decoders/nanosvg.h` (same for `nanosvgrast.h`).
4. Build and run the full suite (`cmake --build build -j && ctest --test-dir build --output-on-failure`); the SVG
   fixtures are in `src/tests/fixtures/svg/` (`text_*.svg` for text). Commit the fork and the new gitlink together
   and update the pinned commit in this file.

Never edit files under `vendor/nanosvg`: changes go into the fork.
