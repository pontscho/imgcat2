```
     /\_/\     _                           _    ____
    ( o.o )   (_)_ __ ___   __ _  ___ __ _| |_ |___ \
     > ^ <    | | '_ ` _ \ / _` |/ __/ _` | __| __) |
    /|   |\   | | | | | | | (_| | (_| (_| | |_ / __/
   (_|   |_)  |_|_| |_| |_|\__, |\___\__,_|\__|_____|
                           |___/

             -- Purr-fect terminal image viewer! --
            -- ANSI • iTerm2 • Kitty • True Color --
```

A command-line tool that displays images and animated GIFs directly in terminal emulators using standard ANSI escape sequences, iTerm2 inline images protocol, and Kitty graphics protocol.

<p align="center">
  <img src="docs/example.gif" alt="imgcat2 in action">
</p>

## Features

- **Universal Compatibility** - Works with any modern terminal that supports true color and Unicode
- **Cross-Platform** - Supports Linux, macOS, Windows, and BSD systems
- **Multiple Image Formats** - PNG/APNG, JPEG, GIF (animated), BMP, TGA, HDR, PBM/PGM/PPM/PAM (Netpbm P1-P7, native), ICO, CUR, QOI, PSD (partial), WebP, HEIF/HEIC, AVIF, TIFF, camera RAW, JPEG XL, raw 8-bit YUV frames (`--yuv`) and SVG (nanosvg with `<text>` support, optional resvg). See [Supported Formats](#supported-formats)
- **Image Conversion** - Convert images to JPEG, PNG, HEIF, WebP, or JXL with quality control and lossless mode support
- **Native Protocol Support** - Automatically uses the iTerm2 inline images protocol (iTerm2, WezTerm) or the Kitty graphics protocol (Kitty, Ghostty, Konsole) for higher quality when available
- **Transparency Support** - Handles alpha channel with threshold-based rendering
- **Terminal-Aware Resizing** - Automatically scales images to fit your terminal
- **Animation Support** - Plays animated GIF, APNG, WebP, JPEG XL and HEIF/AVIF sequences frame by frame in ANSI mode (`-a`)
- **High-Quality Scaling** - Uses advanced interpolation for photographic images
- **Automatic EXIF Orientation** - Images from mobile devices display right-side-up based on camera orientation metadata
- **Metadata Extraction** - Read EXIF and XMP metadata from JPEG, PNG, WebP, HEIF/AVIF, JPEG XL, TIFF and RAW files (camera, GPS, copyright, keywords)
- **Pure C Implementation** - Fast, efficient, and lightweight (C11 standard)
- **Self-Contained Static Build** - Every codec library is vendored as a pinned git submodule and built from source; on Linux `imgcat2` is a fully static executable (static PIE where the toolchain supports it)
- **Reusable Library** - The decoders, encoders and scaler are available as the static C library `libimg2` (see [Using imgcat2 as a library](#using-imgcat2-as-a-library-libimg2))
- **Intelligent Fallback** - Automatically falls back to ANSI rendering when needed

## How It Works

imgcat2 supports multiple rendering modes depending on your terminal:

### Native Protocol Mode (iTerm2 / WezTerm / Ghostty / Kitty / Konsole)
When running in iTerm2, WezTerm, Ghostty, Kitty or Konsole, imgcat2 uses native image protocols for the highest quality:
- **iTerm2, WezTerm**: Uses OSC 1337 inline images protocol with PNG/JPEG recompression
  - Decode → Scale → Encode pipeline for bandwidth optimization
  - Default: JPEG encoding (smaller files, better performance)
  - Optional: PNG encoding via `--iterm2-format png` (lossless, transparency support)
  - Compression: PNG level 3 (speed), JPEG quality 90
  - Bandwidth savings displayed in verbose mode (`-v`)
  - Supports all image formats (WebP, HEIF, RAW, JXL, TIFF, etc.)
- **Ghostty, Kitty, Konsole**: Use the Kitty graphics protocol
- Automatic detection from environment variables (`TERM_PROGRAM`, `LC_TERMINAL`, `TERM=xterm-kitty`, `GHOSTTY_*`, `KONSOLE_*`)
- Falls back to ANSI rendering if protocol fails, if the terminal's pixel size is unknown, or for animated images with `-a` flag

### ANSI Rendering Mode (Universal Fallback)
For all other terminals, imgcat2 uses the **half-block rendering technique** with Unicode character `▄` (U+2584) to achieve double vertical resolution:

- **Background color** represents the top pixel
- **Foreground color + half-block** represents the bottom pixel
- Each terminal cell displays **two pixels vertically**

This technique provides universal compatibility with any terminal supporting true color and Unicode.

## Prerequisites

### Required Dependencies

- **CMake** 3.21 or later
- **C11 compiler** (GCC or Clang) and a **C++ compiler** (for the vendored LibRaw, libheif, libjxl and highway)
- **Python 3** (embeds the fallback font at build time)
- **nasm** (x86 assembly of the vendored aom)
- **pkg-config**
- **patch** or **git** (applies the APNG patch to the vendored libpng)
- The **git submodules** under `vendor/` (`git submodule update --init --recursive`)

No image library has to be installed on the host. zlib-ng, libjpeg-turbo, jpeg-quantsmooth, libpng (with the
APNG patch), giflib, libwebp, libheif (with libde265, kvazaar, aom and OpenJPEG), libtiff, LibRaw, libjxl (with
highway, brotli and Little-CMS) and the header-only stb, qoi and nanosvg code are pinned submodules, built in-tree
and merged into `libimg2.a`. `IMG2_USE_SYSTEM_<X>` switches a codec to the host library (see
[Build Options](#build-options)); the configure step writes the pinned versions to
`build/fat/vendored-libraries.txt`.

### Optional Dependencies

- **OpenMP** - Used by jpeg-quantsmooth and LibRaw when the compiler provides it
- **resvg** - Host library for advanced SVG rendering, enabled with `-DIMG2_ENABLE_RESVG=ON` (see below)
- **clang-format** / **clang-tidy** - Developer tooling (`format` target, `IMG2_ENABLE_CLANG_TIDY`)

### Terminal Requirements

Your terminal emulator must support:

1. **24-bit True Color (16.7M colors)**
   - Test: `printf '\x1b[38;2;255;100;0mTEST\x1b[0m\n'` should display orange text
2. **Unicode Support**
   - Specifically the half-block character: `▄` (U+2584)
3. **ANSI Escape Sequences**
   - Cursor control and color sequences

**Recommended Terminals:**
- macOS: iTerm2, Terminal.app, Kitty, Alacritty, Ghostty
- Linux: GNOME Terminal, Konsole, Kitty, Alacritty, xterm (with true color)
- Windows: Windows Terminal, PowerShell 7+, ConEmu

## Installation

<details>

<summary>Linux</summary>

```bash
# Build tools on Arch Linux
sudo pacman -S --needed base-devel cmake nasm python git
```

```bash
# Build tools on Debian/Ubuntu
sudo apt-get update
sudo apt-get install build-essential cmake nasm python3 pkg-config git
```

```bash
# Clone repository with submodules (the codecs are built from vendor/)
git clone --recurse-submodules https://github.com/pontscho/imgcat2.git
cd imgcat2
git submodule update --init --recursive   # only needed if cloned without --recurse-submodules

cmake -S . -B build
cmake --build build -j
./build/imgcat2 --version
```

</details>

<details>

<summary>macOS (Homebrew)</summary>

```bash
# Install build tools (the C/C++ compiler comes with the Xcode command line tools)
brew install cmake nasm python pkg-config

# Clone repository with submodules
git clone --recurse-submodules https://github.com/pontscho/imgcat2.git
cd imgcat2
git submodule update --init --recursive

# Build
cmake -S . -B build
cmake --build build -j
```

macOS cannot link fully static executables, so only the third-party libraries are linked statically. The
fat-archive step uses `libtool -static` on macOS and is untested (see [docs/libimg2.md](docs/libimg2.md#the-fat-archive)).

</details>

<details>

<summary>APNG Support</summary>

**APNG (Animated PNG)** support is always built in: the configure step copies the vendored libpng into the build
tree and applies `vendor/patches/libpng-1.6.59-apng.patch` there (the submodule stays unmodified).

Only with `-DIMG2_USE_SYSTEM_PNG=ON` does the host libpng have to carry the APNG patch; a copy installed under
`/usr/local/libpng-apng` is preferred. Configure prints `libpng has APNG support: YES`, or warns that APNG
animations will not be decoded (animated PNG files are then shown as static images).

</details>

<details>

<summary>resvg Support (Advanced SVG Rendering)</summary>

**resvg** is an optional, high-quality SVG rendering library that provides significantly better SVG support compared to the built-in nanosvg decoder. It is a host library (not vendored) and is off by default (`IMG2_ENABLE_RESVG=OFF`).

**Note:** With resvg enabled, imgcat2 tries resvg first and falls back to nanosvg when resvg fails. If resvg is requested but not found (pkg-config `resvg` on Linux, `find_library` on macOS), configure prints `resvg not found, using nanosvg fallback` and builds with nanosvg only. A resvg build links the host library, so the binary and the installed package depend on it.

#### Linux
```bash
# Install Rust toolchain and cargo-c (on Debian/Ubuntu)
sudo apt-get install cargo cargo-c

# Install from AUR (on Arch Linux)
yay -S resvg

# Or build from source (on Arch Linux)
sudo pacman -S rust cargo-c

# Build and install resvg C API
git clone https://github.com/linebender/resvg.git
cd resvg/crates/c-api
cargo cinstall --release --prefix=/usr/local
cd ../../..
```

#### macOS (Homebrew)
```bash
# Install Rust toolchain (if not already installed)
brew install rust

# Install cargo-c
brew install cargo-c

# Build and install resvg C API
git clone https://github.com/linebender/resvg.git
cd resvg/crates/c-api
cargo cinstall --release --prefix=/usr/local
cd ../../..
```

### Building imgcat2 with resvg

```bash
# Build with resvg support
cmake -S . -B build -DIMG2_ENABLE_RESVG=ON
cmake --build build -j

# Back to nanosvg only (the default)
cmake -S . -B build -DIMG2_ENABLE_RESVG=OFF
cmake --build build -j

# Verify which SVG decoder is active: "SVG (resvg)" or "SVG (nanosvg)"
./build/imgcat2 --version
```

</details>

### Build Options

| Option | Default | Meaning |
|---|---|---|
| `IMG2_ENABLE_GIF` / `_WEBP` / `_HEIF` / `_TIFF` / `_RAW` / `_JXL` | ON | Optional codecs |
| `IMG2_ENABLE_RESVG` | OFF | resvg SVG renderer (host library) |
| `IMG2_ENABLE_EXIF_READER` | ON | EXIF/XMP metadata |
| `IMG2_ENABLE_JPEGQS` | ON | jpeg-quantsmooth JPEG deblocking |
| `IMG2_USE_SYSTEM_<X>` | OFF | Host library instead of the vendored copy (`JPEG`, `PNG`, `GIF`, `WEBP`, `TIFF`, `LCMS2`, `RAW`, `HEIF`, `JXL`) |
| `IMG2_BUILD_CLI` / `IMG2_BUILD_TESTS` | ON (top level) | Build the `imgcat2` tool / the tests |
| `IMG2_INSTALL` | ON (top level) | Install rules and the `img2Config.cmake` package |
| `IMG2_FAT_ARCHIVE` | `IMG2_INSTALL` | Merge libimg2 and all vendored libraries into one `libimg2.a` |
| `IMG2_WERROR` | ON (top level) | Treat warnings as errors |
| `IMG2_POSITION_INDEPENDENT_CODE` | ON | PIC library and vendors |
| `IMG2_STATIC_PIE` | ON | Linux: link `imgcat2` with `-static-pie` when supported, else `-static` |
| `IMG2_SANITIZE` | empty (off) | `address,undefined` or `thread` sanitizer build ([docs/sanitizers.md](docs/sanitizers.md)) |
| `IMG2_COVERAGE` | OFF | Clang source-based coverage of libimg2 ([docs/coverage.md](docs/coverage.md)) |
| `IMG2_FUZZ` | OFF | libFuzzer targets, Clang only ([docs/fuzzing.md](docs/fuzzing.md)) |

See [docs/libimg2.md](docs/libimg2.md#options) for the full list.

## Using imgcat2 as a library (libimg2)

The decoders, encoders and scaler behind `imgcat2` are a static C11 library, `libimg2`, with public headers
under `include/img2/`. Install it (the fat `libimg2.a` contains every vendored codec library):

```bash
cmake -S . -B build && cmake --build build -j
cmake --install build --prefix /opt/img2 --component img2_development
```

and consume it with CMake:

```cmake
find_package(img2 1.0 REQUIRED)            # CMAKE_PREFIX_PATH=/opt/img2
target_link_libraries(app PRIVATE img2::img2)
```

or build it as part of your project:

```cmake
set(IMG2_BUILD_CLI OFF)
set(IMG2_BUILD_TESTS OFF)
set(IMG2_INSTALL OFF)
add_subdirectory(third_party/imgcat2 img2 EXCLUDE_FROM_ALL)
target_link_libraries(app PRIVATE img2::img2)
```

A short example:

```c
#include <img2/img2.h>

img2_frames_t fr;
img2_image_t *thumb = NULL;
if (img2_decode(data, len, NULL, &fr) == IMG2_OK) {
	if (img2_scale_fit(fr.frames[0], 128, 128, IMG2_FILTER_DEFAULT, &thumb) == IMG2_OK) {
		/* thumb->pixels: RGBA8888, thumb->width x thumb->height */
		img2_image_destroy(thumb);
	}
	img2_frames_free(&fr);
}
```

API overview, ownership rules, error codes, logging, threading and limits: [docs/libimg2.md](docs/libimg2.md).

The installed archive embeds the vendored codec libraries (listed with their versions in
`share/img2/vendored-libraries.txt`); linking another copy of libpng, libjpeg-turbo, libwebp etc. into the same
program causes duplicate symbols, see [docs/libimg2.md](docs/libimg2.md#known-limitations--cli-deltas).

## iTerm2 Native Support

When running in iTerm2, imgcat2 automatically uses the native inline images protocol for significantly better quality:

### Advantages in iTerm2
- **Higher Quality** - Native image rendering without ANSI approximation
- **Better Colors** - No color quantization or dithering
- **Faster Rendering** - Direct image display without pixel-by-pixel processing
- **Original Image Size** - Images up to half the terminal height are shown at their original size
- **Custom Sizing** - Supports `-w` and `-H` flags for pixel-perfect sizing
- Any decodable format is decoded, scaled and re-encoded as JPEG (default) or PNG
- Sends image data via iTerm2's OSC 1337 protocol (wrapped for tmux when `TMUX` is set)
- **Default behavior**: Images taller than half the terminal height are scaled down to half the terminal height; `-f` fits the image to the terminal instead
- Only the first frame of an animated image is sent; with `-a` the animation is played with ANSI rendering
- The encoded image is limited to 8192x8192 pixels
- Automatically falls back to ANSI rendering if protocol fails

### Force ANSI Rendering
To disable native protocols (iTerm2/Kitty) and use ANSI rendering:
```bash
imgcat2 --force-ansi image.png
```

This is useful for:
- Testing ANSI rendering in iTerm2, Kitty or Ghostty
- Compatibility with tools that expect ANSI output
- Scenarios where native protocols may not work (e.g., remote sessions)

<details>

<summary>iTerm2 Sizing Examples</summary>

```bash
# Default sizing (original size up to half the terminal height)
imgcat2 image.png

# Scale to 800px width, preserve aspect ratio
imgcat2 -w 800 image.png

# Scale to 600px height, preserve aspect ratio
imgcat2 -H 600 image.png

# Exact dimensions (may distort)
imgcat2 -w 800 -H 600 image.png

# Animated GIF: first frame only (use -a for ANSI animation)
imgcat2 animation.gif
```

</details>

## Kitty Native Support

When running in Kitty, Ghostty or Konsole, imgcat2 automatically uses the Kitty graphics protocol for high-quality image rendering:

### Advantages in Kitty
- **Higher Quality** - Native rendering using Kitty graphics protocol (every format is decoded and sent as RGBA, `f=32`)
- **Better Colors** - No color quantization or dithering
- **Faster Rendering** - Direct image display without pixel-by-pixel processing
- **Automatic Scaling** - Images taller than half the terminal height are scaled down, preserving aspect ratio
- **Custom Sizing** - Supports `-w` and `-H` flags for precise control
- **Note:** Only the first frame of an animated image is sent; with `-a` the animation uses ANSI rendering
- **Default behavior**: Same sizing as in iTerm2 (original size up to half the terminal height, `-f` fits the image to the terminal)

### SSH Sessions
Kitty's graphics protocol works over SSH by default, so imgcat2 will use it even in remote sessions just enable or add TERM_PROGRAM=kitty in your SSH environment.

<details>

<summary>Kitty Sizing Examples</summary>

```bash
# Any format - decoded to RGBA and sent with f=32
imgcat2 image.png
imgcat2 -w 800 image.png
imgcat2 -H 600 photo.jpg

# Animated GIF - first frame via the Kitty protocol
imgcat2 animation.gif

# Animated GIF with -a - ANSI rendering (frame-by-frame animation)
imgcat2 -a animation.gif

# Force ANSI rendering for any format
imgcat2 --force-ansi image.png
```

</details>

## Usage

### Basic Usage

Display an image:
```bash
imgcat2 image.png
```

### Pipe from stdin

Display image from URL:
```bash
curl -s https://example.com/image.png | imgcat2
```

Display image from file via pipe:
```bash
cat image.jpg | imgcat2
```

### Command-Line Options

```bash
Usage: ./imgcat2 [OPTIONS] [FILE]

Display images in the terminal using ANSI escape sequences and half-block characters.

Options:
  -h, --help                Show this help message and exit
      --version             Show version information and exit
      --fonts               List available fonts and exit
  -i, --interpolation TYPE  Interpolation method (default: lanczos)
                            Available: lanczos, bilinear, nearest, cubic, box, mitchell
                            (lanczos = stb_image_resize2 default; stb has no Lanczos filter)
  -f, --fit                 Fit image to terminal (maintain aspect ratio, default)
  -r, --resize              Resize to exact terminal dimensions (may distort)
  -w, --width N             Target width in pixels
  -H, --height N            Target height in pixels
                            If both: exact dimensions
                            If one: aspect ratio preserved
                            If neither: fit to terminal (default)
  -v, --verbose             Verbose mode (show non-error messages)
      --fps N               Animation FPS (1-15, default: 15)
  -a, --animate             Animate GIF frames
      --force-ansi          Force ANSI rendering (disable iTerm2 protocol)

Metadata Options:
      --info                Output image metadata instead of rendering
      --json                Format --info output as JSON (single line)
      --exif-detailed       Include all EXIF fields (default: summary only)

Conversion Options:
  -j, --jpeg <quality>      Convert to JPEG format (0-100, default: 90)
  -p, --png <level>         Convert to PNG format (0-9, default: 6)
      --heif <quality>      Convert to HEIF (quality: 0-100, default: 80)
      --webp <quality>      Convert to WebP (quality: 0-100, default: 80, 100=lossless)
      --jxl <quality>       Convert to JXL (quality: 0-100, default: 80, >=95=lossless)
  -o, --output <file>       Output file (stdout if not specified)
  -n, --frame <index>       Select frame for animated images (default: 0)
      --iterm2-format <png|jpeg>  Format for iTerm2 encoding (default: jpeg)
                            png: lossless, transparency support
                            jpeg: smaller files, no transparency

Input Options:
      --yuv FMT:W:H[:YSTRIDE][:UVSTRIDE]
                            Treat input as a raw 8-bit YUV frame (no format detection)
                            FMT: i420 (yuv420p), yv12, nv12, nv21, i422 (yuv422p),
                                 i444 (yuv444p), yuyv (yuy2), uyvy, gray (y8, y800)
                            YSTRIDE: bytes per luma/packed row (default: minimal)
                            UVSTRIDE: bytes per chroma row (default: minimal)
                            Planes are contiguous (Y, U, V; yv12: Y, V, U);
                            BT.601 limited range; only the first frame is used

Arguments:
  FILE                      Input image file (omit or '-' for stdin)

Examples:
  ./imgcat2 image.png              Display PNG image
  ./imgcat2 -a animation.gif       Animate GIF
  cat image.jpg | ./imgcat2        Read from stdin
  ./imgcat2 --fps 10 anim.gif      Animate at 10 FPS
  ./imgcat2 --jpeg 90 img.png > out.jpg  Convert to JPEG (stdout)
  ./imgcat2 --png 6 --output out.png img.jpg  Convert to PNG file
  ./imgcat2 --yuv nv12:1920:1080 frame.yuv  Display a raw NV12 frame
```

<details>

<summary>Examples</summary>

**Fit image to terminal (default):**
```bash
imgcat2 photo.jpg
```

**Display with nearest-neighbor scaling (for pixel art):**
```bash
imgcat2 --interpolation=nearest pixelart.png
```

`--interpolation` maps to the libimg2 resampling filters: `lanczos` (default) is the stb_image_resize2 sRGB
default filter (stb has no true Lanczos), `nearest` = point sampling, `bilinear` = triangle, `cubic` =
Catmull-Rom, `box` = box, `mitchell` = Mitchell-Netravali.

**View animated GIF:**
```bash
imgcat2 -a animation.gif
# Press Ctrl+C to exit
```

**Custom animation frame rate (1-15 FPS):**
```bash
imgcat2 -a --fps=10 animation.gif
```

**Display a raw YUV frame (no header, geometry given on the command line):**
```bash
imgcat2 --yuv i420:1920:1080 frame.yuv
# with row strides: luma rows of 2048 bytes, chroma rows of 1024 bytes
imgcat2 --yuv i420:1920:1080:2048:1024 frame.yuv
```

**List the fonts available for SVG `<text>`:**
```bash
imgcat2 --fonts
```

**Display image from clipboard (macOS):**
```bash
pngpaste - | imgcat2
```

**Display from web:**
```bash
curl -sL https://picsum.photos/800/600 | imgcat2
```

**Use with ImageMagick for format conversion:**
```bash
convert input.webp png:- | imgcat2
```

**Extract EXIF metadata from JPEG:**
```bash
# Human-readable format
imgcat2 --info photo.jpg

# JSON format for scripting
imgcat2 --info --json photo.jpg | jq .exif.gps

# Check GPS coordinates
imgcat2 --info photo.jpg | grep "GPS:"
```

</details>

## Image Conversion

imgcat2 can convert any input it can decode (raw YUV frames included) to JPEG, PNG, HEIF, WebP or JPEG XL:

### JPEG Conversion
```bash
# Convert to JPEG with default quality (90)
imgcat2 --jpeg 90 image.png > output.jpg

# Convert with custom quality (0-100)
imgcat2 --jpeg 80 image.png > output.jpg

# Save to file
imgcat2 --jpeg 95 --output output.jpg image.png
```

### PNG Conversion
```bash
# Convert to PNG with default compression (6)
imgcat2 --png 6 image.jpg > output.png

# Convert with custom compression (0-9)
imgcat2 --png 9 image.jpg > output.png

# Save to file
imgcat2 --png 5 --output output.png image.jpg
```

### HEIF Conversion
```bash
# Convert to HEIF with default quality (80)
imgcat2 --heif 80 image.png > output.heif

# Convert with custom quality (0-100)
imgcat2 --heif 95 image.jpg > output.heif

# Save to file
imgcat2 --heif 85 --output output.heif image.png
```

### WebP Conversion
```bash
# Convert to WebP with default quality (80)
imgcat2 --webp 80 image.png > output.webp

# Convert with custom quality (0-100)
imgcat2 --webp 90 image.jpg > output.webp

# Lossless WebP (quality 100)
imgcat2 --webp 100 image.png > output.webp

# Save to file
imgcat2 --webp 85 --output output.webp image.jpg
```

### JXL Conversion
```bash
# Convert to JXL with default quality (80)
imgcat2 --jxl 80 image.png > output.jxl

# Convert with custom quality (0-100)
imgcat2 --jxl 90 image.jpg > output.jxl

# Lossless JXL (quality >= 95)
imgcat2 --jxl 95 image.png > output.jxl
imgcat2 --jxl 100 image.png > output.jxl

# Save to file
imgcat2 --jxl 85 --output output.jxl image.jpg
```

### Animated Images
```bash
# Convert first frame of animated GIF
imgcat2 --jpeg 90 --output output.jpg animation.gif

# Convert specific frame (0-based index)
imgcat2 --jpeg 90 --frame 5 --output output.jpg animation.gif
```

### Conversion Options
- `--jpeg <quality>` or `-j <quality>` - Convert to JPEG format (quality: 0-100, default: 90)
  - Higher quality = better image quality but larger file size
- `--png <level>` or `-p <level>` - Convert to PNG format (compression: 0-9, default: 6)
  - Higher compression = smaller file size but slower encoding
- `--heif <quality>` - Convert to HEIF format (quality: 0-100, default: 80)
  - Higher quality = better image quality but larger file size
- `--webp <quality>` - Convert to WebP format (quality: 0-100, default: 80)
  - Quality 100 = lossless mode (preserves transparency)
  - Quality 0-99 = lossy mode
- `--jxl <quality>` - Convert to JXL format (quality: 0-100, default: 80)
  - Quality >= 95 = lossless mode (preserves transparency)
  - Quality 0-94 = lossy mode with distance-based quality
- `--output <file>` or `-o <file>` - Output file path (stdout if not specified)
- `--frame <index>` or `-n <index>` - Select specific frame from animated images (default: 0)

### Conversion Notes
- **JPEG** does not support transparency - alpha channel will be removed
- **PNG** preserves transparency - ideal for images with alpha channel
- **HEIF** supports transparency and offers better compression than JPEG
- **WebP** preserves transparency in lossless mode (quality 100) and offers excellent compression
- **JXL** preserves transparency in lossless mode (quality >= 95) and provides modern, high-efficiency compression with HDR support
- Output to stdout by default (use `>` to redirect or `--output` flag)
- Quality and compression affect both file size and processing time
- Out-of-range JPEG quality and PNG level values are clamped with a warning; HEIF, WebP and JXL quality outside 0-100 is an error
- Only one frame is encoded (no animated output)

## Metadata Extraction

imgcat2 can extract EXIF and XMP metadata from JPEG, PNG, WebP, HEIF/AVIF, JPEG XL and RAW files (EXIF only from TIFF), including camera settings, GPS coordinates, copyright information, and more.

### Automatic Orientation Correction

Images captured by digital cameras and smartphones often contain EXIF orientation metadata that indicates how the image should be rotated or flipped for proper display. imgcat2 automatically detects and applies these transformations so your images always appear right-side-up.

**Supported transformations:**
- Horizontal flip (orientation 2)
- 180° rotation (orientation 3)
- Vertical flip (orientation 4)
- Transpose (orientation 5)
- 90° clockwise rotation (orientation 6)
- Transverse (orientation 7)
- 270° clockwise rotation (orientation 8)

**Supported formats:**
- Automatic orientation correction works with JPEG, PNG, WebP, JPEG XL, and RAW formats
- TIFF, HEIF, and AVIF formats handle orientation natively through their libraries (no additional processing needed)
- Transformation is applied during decoding, before rendering or scaling
- No user interaction required - orientation is automatically detected and applied

When viewing images with orientation metadata in verbose mode (`-v`), imgcat2 will display a message:
```
Applying EXIF orientation: 6 (Rotate 90° CW)
```

### Text Output

Display metadata in human-readable format:
```bash
imgcat2 --info photo.heif
```

Example output:
```
HEIF image/heif 3024x4032 1 frame

Camera:
  Make/Model: Apple iPhone 13 Pro Max
  Software: 15.2.1

Exposure:
  Shutter Speed: 1/60 s
  Aperture: f/1.5
  ISO: 250

Lens:
  Model: iPhone 13 Pro Max back triple camera 5.7mm f/1.5
  Focal Length: 6 mm

Image:
  Orientation: Rotate 90 CW
  Date/Time: 2022:02:16 12:55:41
```

Depending on the file, the output also has `GPS:`, `Rights:` (artist, copyright), `Annotations:` (description,
comment) and `XMP Metadata:` (creator, title, keywords, rating, location, ...) sections.

### JSON Output

Get metadata as JSON for programmatic processing:
```bash
imgcat2 --info --json photo.heif
```

Example output:
```json
{"type":"HEIF","mime":"image/heif","width":3024,"height":4032,"frames":1,"exif":{"camera":{"make":"Apple","model":"iPhone 13 Pro Max","software":"15.2.1"},"exposure":{"time":0.016667,"aperture":1.5,"iso":250},"lens":{"model":"iPhone 13 Pro Max back triple camera 5.7mm f/1.5","focalLength":6},"image":{"orientation":6,"dateTime":"2022:02:16 12:55:41"}}}
```

### Metadata Options

- `--info` - Extract and display image metadata instead of rendering
- `--json` - Output metadata as single-line JSON (requires `--info`)
- `--exif-detailed` - Show all EXIF fields instead of summary (accepted, but currently has no effect)

### Supported Metadata

**EXIF Data (from camera):**
- Camera make, model, software
- Exposure settings (shutter speed, aperture, ISO, exposure compensation)
  - ISO values above 65535 are read from the EXIF 2.3 sensitivity tags (SensitivityType, StandardOutputSensitivity, RecommendedExposureIndex, ISOSpeed)
- Lens information (model, focal length, max aperture)
- Date/time original
- GPS coordinates (latitude, longitude, altitude, date stamp)
- Image orientation
- Artist, copyright, image description, user comment
- Flash settings, white balance, metering mode (parsed by libimg2, not printed by `--info`)

**XMP Data (from editing software):**
- Creator, copyright, rights information
- Title, description, keywords
- Star rating, color label
- Creation/modification dates
- Location (city, country)
- IPTC metadata (credit, source)

### Metadata Notes

- Formats without metadata support (GIF, BMP, SVG, QOI, raw YUV, etc.) show basic info only (type, MIME type, dimensions, frame count)
- Handles both little-endian (Canon, most cameras) and big-endian (Nikon) EXIF data
- Gracefully handles missing or malformed metadata
- Built-in XML parser for XMP data (zero dependencies)
- Text taken from the file is escaped: in the text output, control characters (C0 except tab and newline, DEL, C1) are written as `\xNN`; the JSON output escapes C0 control characters as `\u00NN`

## Supported Formats

### Always Built In
- **PNG/APNG** - Portable Network Graphics (with transparency), animated PNG through the APNG-patched vendored libpng
- **JPEG/JPG** - JPEG images (no transparency), libjpeg-turbo with jpeg-quantsmooth deblocking (`IMG2_ENABLE_JPEGQS`)
- **BMP** - Windows Bitmap (uncompressed)
- **SVG** - Scalable Vector Graphics (nanosvg fork with `<text>` / `<tspan>` support, see [docs/nanosvg-fork.md](docs/nanosvg-fork.md); resvg when enabled)
- **PNM/PAM** - Netpbm PBM, PGM, PPM and PAM (P1-P7, ASCII and binary, 8/16-bit, alpha in PAM), native decoder
- **TGA** - Targa Image File
- **HDR** - Radiance RGBE High Dynamic Range
- **ICO** - Windows Icon format
- **CUR** - Windows Cursor format
- **QOI** - Quite OK Image format (simple, fast)
- **PSD** - Adobe Photoshop Document (partial support)
- **Raw YUV** - Headerless 8-bit frames (I420, YV12, NV12, NV21, I422, I444, YUYV, UYVY, gray), only with `--yuv` (no format detection)

### Optional Codecs (Built by Default)
- **GIF** - Graphics Interchange Format (animated), giflib
- **WebP** - Google WebP format (animated), libwebp
- **HEIF/HEIC and AVIF** - High Efficiency Image Format and AV1 Image File Format (image sequences are animated), libheif
- **TIFF** - Tagged Image File Format (multi-page files decode as frames), libtiff
- **RAW** - Camera raw formats (CR2, NEF, ARW, DNG, etc.), LibRaw
- **JXL** - JPEG XL format (animated; modern, high efficiency, HDR support), libjxl

Each one can be left out with `-DIMG2_ENABLE_<X>=OFF` (`GIF`, `WEBP`, `HEIF`, `TIFF`, `RAW`, `JXL`). HEIF, WebP
and JXL output need the matching codec; JPEG and PNG output are always available.

**Note:** Run `imgcat2 --version` to see the decoders compiled into the binary.

### Known Limitations

#### SVG Rendering

- SVG gradients and filters have limited support (nanosvg limitation)
- `<text>` / `<tspan>` are rendered with the system fonts (Linux: `/usr/share/fonts`, `/usr/local/share/fonts`, `~/.local/share/fonts`, `~/.fonts`) or the embedded DejaVu Sans Mono fallback; `imgcat2 --fonts` lists them. `<textPath>`, `writing-mode`, `text-decoration`, `letter-spacing` / `word-spacing`, `baseline-shift` and per-glyph `rotate` are not supported (convert text to paths for exact results)
- A `<text>` / `<tspan>` keeps at most 4094 bytes of content; a longer run is truncated at a UTF-8 code point boundary and a warning is printed
- SVGZ (compressed SVG) not supported - decompress first: `gunzip file.svgz`
- Complex SVGs with thousands of elements may render slowly
- No animation support (SMIL animations ignored)

#### Resource Limits

Inputs are untrusted, so the decoders enforce fixed limits ([docs/libimg2.md](docs/libimg2.md#limits)):

- Input files larger than 1 GiB are rejected
- Each frame: at most 65536 pixels per side and 200 megapixels; the size in the file header is checked before the codec allocates
- Animations (GIF, APNG, WebP, JPEG XL, HEIF/AVIF) and multi-page TIFF: the first 200 frames are decoded (with a warning), and at most 2^30 pixels summed over all frames
- `-w` / `-H` accept at most 16384 pixels; the iTerm2 output is limited to 8192x8192 pixels

#### Untrusted Text

Control characters taken from the input are escaped before they are printed: in the `--info` text output, `--fonts` family
names, file paths and arguments echoed in messages, and libimg2's log messages, C0 control characters other than
tab and newline, DEL and C1 control characters are written as `\xNN` (log messages are single lines, so a newline
inside one is escaped too). Details: [docs/libimg2.md](docs/libimg2.md#logging).

## Terminal Requirements

### Color Support

imgcat2 requires **24-bit true color** support. To verify your terminal:

```bash
# Test true color
printf '\x1b[48;2;255;0;0m  \x1b[48;2;0;255;0m  \x1b[48;2;0;0;255m  \x1b[0m\n'
# Should display: red, green, blue blocks
```

If colors don't appear correctly:
- Check `$COLORTERM` environment variable (should be `truecolor` or `24bit`)
- Update to a newer terminal emulator
- Try setting: `export COLORTERM=truecolor`

### Unicode Support

Test half-block character support:
```bash
echo -e 'Half-block test: ▄▀▄▀▄'
```

If characters don't display correctly:
- Ensure terminal uses UTF-8 encoding
- Install a Unicode-capable font (e.g., DejaVu Sans Mono, Menlo, Consolas)

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature-name`
3. Follow the coding style (use provided `.clang-format` and `.clang-tidy`)
4. Add tests for new functionality
5. Ensure all tests pass: `ctest --test-dir build --output-on-failure`
6. Submit a pull request

### Code Style

The project uses:
- **C11 standard** with GNU extensions (gnu11)
- **clang-format** for code formatting (`.clang-format` provided)
- **clang-tidy** for static analysis (`.clang-tidy` provided)
- **Doxygen** comments for documentation

Format code before committing (the `format` target exists when clang-format is found):
```bash
cmake --build build --target format
```

### Testing

Run tests with:
```bash
ctest --test-dir build --output-on-failure
# skip the slow add_subdirectory consumer test
ctest --test-dir build -LE expensive --output-on-failure
```

The default run includes the guard tests (label `guard`: symbol prefix, no-terminate, layering and public-header
checks) and the installed-package consumer test (label `consumer`).

Separate build trees for deeper checks:

- `scripts/coverage.sh` - Clang source-based coverage of libimg2 with a machine-checked exclusion list ([docs/coverage.md](docs/coverage.md))
- `scripts/sanitize.sh asan` / `scripts/sanitize.sh tsan` - ASan+UBSan or TSan build and test run ([docs/sanitizers.md](docs/sanitizers.md))
- `scripts/fuzz.sh` - libFuzzer targets for the decoders, EXIF/XMP and YUV parsers ([docs/fuzzing.md](docs/fuzzing.md))

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Inspired by the original [imgcat](https://github.com/danielgatis/imgcat) by Daniel Gatis
- Built with [stb](https://github.com/nothings/stb) libraries for additional image format support
- SVG rendering uses a fork of [nanosvg](https://github.com/memononen/nanosvg) ([docs/nanosvg-fork.md](docs/nanosvg-fork.md))
- The codec libraries are vendored as git submodules under `vendor/` (upstream URLs in `.gitmodules`)

## Troubleshooting

### Image doesn't display correctly

**Problem:** Colors appear wrong or image is distorted

**Solutions:**
- Verify terminal supports true color: `echo $COLORTERM`
- Test color support with the commands in [Terminal Requirements](#terminal-requirements)
- Update terminal emulator to latest version

### Unsupported format error

**Problem:** "pipeline_decode: unknown image format" or "No decoder found for MIME type"

**Solutions:**
- Check enabled formats: `imgcat2 --version`
- Rebuild with format support: `cmake -S . -B build -DIMG2_ENABLE_WEBP=ON`
- Raw YUV data has no header: pass its geometry with `--yuv format:width:height`
- Convert image: `convert input.webp png:- | imgcat2`

### Build fails with missing dependencies

**Problem:** CMake fails in a `vendor/` directory or cannot find a tool

**Solutions:**
- Fetch the submodules: `git submodule update --init --recursive`
- Install the build tools (C/C++ compiler, CMake 3.21+, Python 3, nasm, pkg-config, see [Prerequisites](#prerequisites))
- After moving a submodule, re-run CMake (the vendored-library manifest is written at configure time)
- With `IMG2_USE_SYSTEM_<X>=ON` the host library is looked up with pkg-config (or `find_library`); set `PKG_CONFIG_PATH` if needed

### Animation is slow or choppy

**Problem:** GIF animation stutters or has low frame rate

**Solutions:**
- Reduce image size: `imgcat2 --fit animation.gif`
- Use faster terminal emulator (try Alacritty or Kitty)
- Simplify GIF or reduce frame count

### Program crashes or segfaults

**Problem:** imgcat2 crashes when processing certain images

**Solutions:**
- Verify image file is not corrupted: `identify image.png`
- Report issue with sample image for debugging

## Support

For bugs, feature requests, or questions:
- Open an issue on GitHub
- Check existing issues for similar problems
- Provide terminal type, OS version, and sample image if applicable
