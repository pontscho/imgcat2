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
- **Multiple Image Formats** - PNG, JPEG, GIF (animated), BMP, TGA, HDR, PNM, ICO, CUR, QOI, PSD (partial), and optionally WebP, HEIF, TIFF, RAW, JXL, APNG. Dual SVG decoders (resvg preferred, nanosvg fallback)
- **Image Conversion** - Convert images to JPEG, PNG, HEIF, WebP, or JXL with quality control and lossless mode support
- **Native Protocol Support** - Automatically uses iTerm2 inline images or Ghostty Kitty graphics protocol for higher quality when available
- **Transparency Support** - Handles alpha channel with threshold-based rendering
- **Terminal-Aware Resizing** - Automatically scales images to fit your terminal
- **Animation Support** - Displays animated GIFs with smooth playback (native in iTerm2)
- **High-Quality Scaling** - Uses advanced interpolation for photographic images
- **Automatic EXIF Orientation** - Images from mobile devices display right-side-up based on camera orientation metadata
- **Metadata Extraction** - Read EXIF and XMP metadata from JPEG files (camera, GPS, copyright, keywords)
- **Pure C Implementation** - Fast, efficient, and lightweight (C11 standard)
- **Static Linking** - Produces fully static binaries where possible
- **Intelligent Fallback** - Automatically falls back to ANSI rendering when needed

## How It Works

imgcat2 supports multiple rendering modes depending on your terminal:

### Native Protocol Mode (iTerm2 / Ghostty / Kitty)
When running in iTerm2, Ghostty, or Kitty, imgcat2 uses native image protocols for the highest quality:
- **iTerm2**: Uses OSC 1337 inline images protocol with PNG/JPEG recompression
  - Decode → Scale → Encode pipeline for bandwidth optimization
  - Default: JPEG encoding (smaller files, better performance)
  - Optional: PNG encoding via `--iterm2-format png` (lossless, transparency support)
  - Compression: PNG level 3 (speed), JPEG quality 90 (configurable via `--jpeg-quality`)
  - Bandwidth savings displayed in non-silent mode
  - Supports all image formats (WebP, HEIF, RAW, JXL, TIFF, etc.)
- **Ghostty**: Uses Kitty graphics protocol
- **Kitty**: Uses Kitty graphics protocol
- Automatic detection based on `TERM_PROGRAM` environment variable
- Falls back to ANSI rendering if protocol fails or for animated images with `-a` flag

### ANSI Rendering Mode (Universal Fallback)
For all other terminals, imgcat2 uses the **half-block rendering technique** with Unicode character `▄` (U+2584) to achieve double vertical resolution:

- **Background color** represents the top pixel
- **Foreground color + half-block** represents the bottom pixel
- Each terminal cell displays **two pixels vertically**

This technique provides universal compatibility with any terminal supporting true color and Unicode.

## Prerequisites

### Required Dependencies

- **CMake** 3.15 or later
- **C11-compatible compiler** (GCC, Clang, or MSVC)
- **libpng** (PNG support)
- **Compression library**: **zlib-ng** (recommended, better performance) or **zlib** (fallback)
  - CMake automatically selects the best available compression library
  - Priority order: zlib-ng > zlib
- **libjpeg** or **libjpeg-turbo** (recommended for faster JPEG decoding)

### Optional Dependencies

- **giflib** - For GIF animation support
- **resvg** - For advanced SVG rendering (recommended for better SVG support)
- **libwebp** - For WebP format support
- **libheif** - For HEIF/HEIC format support
- **libtiff** - For TIFF format support
- **libraw** - For RAW camera format support
- **libjxl** - For JPEG XL format support

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
# Install required dependencies on Arch Linux
sudo pacman -S cmake gcc libpng zlib libjpeg-turbo

# Install optional dependencies
sudo pacman -S giflib libwebp libheif libtiff libraw libjxl
# Note: resvg may be available in AUR
```

```bash
# Install required dependencies on Debian/Ubuntu
sudo apt-get update
sudo apt-get install cmake gcc libpng-dev zlib1g-dev libjpeg-dev

# Install optional dependencies
sudo apt-get install libgif-dev libwebp-dev libheif-dev libtiff-dev libraw-dev libjxl-dev
```

```bash
# Clone repository with submodules for full format support
git clone --recurse-submodules https://github.com/yourusername/imgcat2.git
git submodule update --init --recursive
cd imgcat2

mkdir build && cd build
cmake ..
make
```

</details>

<details>

<summary>macOS (Homebrew)</summary>

```bash
# Install dependencies
brew install cmake libpng zlib-ng jpeg-turbo giflib webp libheif libtiff libraw jpeg-xl

# Clone repository with submodules for full format support
git clone --recurse-submodules https://github.com/yourusername/imgcat2.git
git submodule update --init --recursive
cd imgcat2

# Build
mkdir build && cd build
cmake ..
make
```

</details>

<details>

<summary>Windows (MinGW)</summary>

```bash
# Install MSYS2 from https://www.msys2.org/
# In MSYS2 MinGW64 shell:

pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-libpng mingw-w64-x86_64-zlib mingw-w64-x86_64-libjpeg-turbo
pacman -S mingw-w64-x86_64-giflib mingw-w64-x86_64-libwebp mingw-w64-x86_64-libjxl

# Clone with submodules and build
git clone --recurse-submodules https://github.com/yourusername/imgcat2.git
cd imgcat2
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

</details>

<details>

<summary>APNG Support</summary>

**APNG (Animated PNG)** support requires libpng version 1.6.0 or later compiled with APNG patch. Most system-provided libpng installations do **not** include APNG support by default.
**Note**: Without APNG support, animated PNG files will be decoded as static images (first frame only). All other PNG functionality works normally.

To enable APNG animation support:

```bash
# Download and build libpng with APNG patch
cd /tmp
wget https://downloads.sourceforge.net/libpng/libpng-1.6.43.tar.gz
tar xzf libpng-1.6.43.tar.gz
cd libpng-1.6.43

# Apply APNG patch
wget https://sourceforge.net/projects/libpng-apng/files/libpng16/1.6.43/libpng-1.6.43-apng.patch.gz
gunzip libpng-1.6.43-apng.patch.gz
patch -p1 < libpng-1.6.43-apng.patch

# Build and install
./configure --prefix=/usr/local/libpng-apng
make -j4
sudo make install
```

Then configure imgcat2 to use the APNG-enabled libpng:

```bash
cd imgcat2/build
cmake .. -DPNG_PNG_INCLUDE_DIR=/usr/local/libpng-apng/include \
         -DPNG_LIBRARY=/usr/local/libpng-apng/lib/libpng16.a
make
```

Verify APNG support is enabled:
```bash
./imgcat2 --version  # Should show "APNG support: YES"
```

</details>

<details>

<summary>resvg Support (Advanced SVG Rendering)</summary>

**resvg** is an optional, high-quality SVG rendering library that provides significantly better SVG support compared to the built-in nanosvg decoder. If resvg is not installed, imgcat2 will automatically fall back to nanosvg.

**Note:** If resvg is not installed, imgcat2 will automatically use nanosvg as a fallback. Both decoders are always available at runtime for maximum compatibility.

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
# Build with resvg support (default if installed)
mkdir build && cd build
cmake ..
make

# To disable resvg (use nanosvg only)
cmake .. -DENABLE_RESVG=OFF
make

# Verify which SVG decoder is active
./imgcat2 --version
```

</details>

## iTerm2 Native Support

When running in iTerm2, imgcat2 automatically uses the native inline images protocol for significantly better quality:

### Advantages in iTerm2
- **Higher Quality** - Native image rendering without ANSI approximation
- **Better Colors** - No color quantization or dithering
- **Faster Rendering** - Direct image display without pixel-by-pixel processing
- **Native Animations** - Smooth GIF playback handled by iTerm2
- **Original Image Size** - Displays images at their native resolution by default
- **Custom Sizing** - Supports `-w` and `-H` flags for pixel-perfect sizing
- Validates image format is supported (PNG, JPEG, GIF, BMP)
- Sends image data via iTerm2's OSC 1337 protocol
- **Default behavior**: Displays image at original size (no scaling)
- Automatically falls back to ANSI rendering if protocol fails

### Force ANSI Rendering
To disable native protocols (iTerm2/Ghostty) and use ANSI rendering:
```bash
imgcat2 --force-ansi image.png
```

This is useful for:
- Testing ANSI rendering in iTerm2 or Ghostty
- Compatibility with tools that expect ANSI output
- Scenarios where native protocols may not work (e.g., remote sessions)

<details>

<summary>iTerm2 Sizing Examples</summary>

```bash
# Display at original size (default in iTerm2)
imgcat2 image.png

# Scale to 800px width, preserve aspect ratio
imgcat2 -w 800 image.png

# Scale to 600px height, preserve aspect ratio
imgcat2 -H 600 image.png

# Exact dimensions (may distort)
imgcat2 -w 800 -H 600 image.png

# Animated GIF at original size
imgcat2 animation.gif
```

</details>

## Kitty Native Support

When running in Kitty terminal, imgcat2 automatically uses the Kitty graphics protocol for high-quality image rendering:

### Advantages in Kitty
- **Higher Quality** - Native rendering using Kitty graphics protocol (PNG direct, others decoded)
- **Better Colors** - No color quantization or dithering
- **Faster Rendering** - Direct image display without pixel-by-pixel processing
- **Automatic Scaling** - Images scale to fit terminal window while preserving aspect ratio
- **Custom Sizing** - Supports `-w` and `-H` flags for precise control
- **Note:** Animated GIFs will use ANSI rendering for frame-by-frame animation
- **Default behavior**: Fits image to terminal width, preserving aspect ratio

### SSH Sessions
Kitty's graphics protocol works over SSH by default, so imgcat2 will use it even in remote sessions just enable or add TERM_PROGRAM=kitty in your SSH environment.

<details>

<summary>Kitty Sizing Examples</summary>

```bash
### Ghostty / Kitty Sizing Examples
```bash
# PNG - sent directly with f=100
imgcat2 image.png
imgcat2 -w 800 image.png

# JPEG - decoded to RGBA and sent with f=32
imgcat2 photo.jpg
imgcat2 -w 800 photo.jpg

# Static GIF - decoded to RGBA and sent with f=32
imgcat2 static.gif
imgcat2 -H 600 static.gif

# Animated GIF - uses ANSI rendering (frame-by-frame animation)
imgcat2 animation.gif

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
  -i, --interpolation TYPE  Interpolation method (default: lanczos)
                            Available: lanczos, bilinear, nearest, cubic
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
      --iterm2-format <png|jpeg>  Format for iTerm2 encoding (default: jpeg)
                            png: lossless, transparency support
                            jpeg: smaller files, no transparency

Metadata Options:
      --info                Output image metadata instead of rendering
      --json                Format --info output as JSON (single line)
      --exif-detailed       Include all EXIF fields (default: summary only)

Arguments:
  FILE                      Input image file (omit or '-' for stdin)

Examples:
  ./imgcat2 image.png              Display PNG image
  ./imgcat2 -a animation.gif       Animate GIF
  cat image.jpg | ./imgcat2        Read from stdin
  ./imgcat2 --fps 10 anim.gif      Animate at 10 FPS
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

**View animated GIF:**
```bash
imgcat2 -a animation.gif
# Press Ctrl+C to exit
```

**Custom animation frame rate:**
```bash
imgcat2 -a --fps=20 animation.gif
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

imgcat2 can convert images to JPEG or PNG formats:

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
- `--jpeg <quality>` - Convert to JPEG format (quality: 0-100, default: 90)
  - Higher quality = better image quality but larger file size
- `--png <level>` - Convert to PNG format (compression: 0-9, default: 6)
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

## Metadata Extraction

imgcat2 can extract EXIF and XMP metadata from JPEG files, including camera settings, GPS coordinates, copyright information, and more.

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

When viewing images with orientation metadata, imgcat2 will display a message:
```
Applying EXIF orientation: 6 (Rotate 90° CW)
```

### Text Output

Display metadata in human-readable format:
```bash
imgcat2 --info photo.jpg
```

Example output:
```
Type: JPEG (image/jpeg)
Dimensions: 4000x3000 (1 frame)

Camera:
  Make/Model: Canon EOS 5D Mark IV
  Software: Adobe Lightroom 12.0

Exposure:
  Shutter Speed: 1/200 s
  Aperture: f/4.0
  ISO: 800
  Exposure Compensation: +0.3 EV

Lens:
  Model: EF 24-70mm f/2.8L II USM
  Focal Length: 50mm (35mm equiv: 50mm)
  Max Aperture: f/2.8

GPS:
  Coordinates: 37.7749°N, 122.4194°W
  Altitude: 15.0m

XMP Metadata:
  Creator: John Doe
  Copyright: © 2024 John Doe
  Keywords: landscape, sunset, california
  Rating: 5 stars
  Location: San Francisco, USA
```

### JSON Output

Get metadata as JSON for programmatic processing:
```bash
imgcat2 --info --json photo.jpg
```

Example output:
```json
{"type":"JPEG","mime":"image/jpeg","width":4000,"height":3000,"frames":1,"exif":{"camera":{"make":"Canon","model":"EOS 5D Mark IV","software":"Adobe Lightroom 12.0"},"exposure":{"time":0.005000,"aperture":4.0,"iso":800,"compensation":0.3},"lens":{"model":"EF 24-70mm f/2.8L II USM","focalLength":50,"focalLength35mm":50,"maxAperture":2.8},"image":{"width":4000,"height":3000,"orientation":1,"dateTime":"2024:12:15 14:23:56"},"gps":{"latitude":37.774900,"longitude":-122.419400,"altitude":15.0}},"xmp":{"creator":"John Doe","copyright":"© 2024 John Doe","keywords":"landscape, sunset, california","rating":5,"location":{"city":"San Francisco","country":"USA"}}}
```

### Metadata Options

- `--info` - Extract and display image metadata instead of rendering
- `--json` - Output metadata as single-line JSON (requires `--info`)
- `--exif-detailed` - Show all EXIF fields instead of summary (future feature)

### Supported Metadata

**EXIF Data (from camera):**
- Camera make, model, software
- Exposure settings (shutter speed, aperture, ISO, exposure compensation)
- Lens information (model, focal length, max aperture)
- Date/time original
- GPS coordinates (latitude, longitude, altitude)
- Image orientation
- Flash settings, white balance, metering mode

**XMP Data (from editing software):**
- Creator, copyright, rights information
- Title, description, keywords
- Star rating, color label
- Creation/modification dates
- Location (city, country)
- IPTC metadata (credit, source)

### Metadata Notes

- Metadata extraction is **JPEG-only** (PNG, GIF, etc. show basic info only)
- Zero parsing overhead when `--info` is not used
- Handles both little-endian (Canon, most cameras) and big-endian (Nikon) EXIF data
- Gracefully handles missing or malformed metadata
- Built-in XML parser for XMP data (zero dependencies)

## Supported Formats

### Always Supported (Required Dependencies)
- **PNG** - Portable Network Graphics (with transparency)
- **JPEG/JPG** - JPEG images (no transparency)
- **BMP** - Windows Bitmap (uncompressed)
- **SVG** - Scalable Vector Graphics (nanosvg rasterization)
- **PNM** - Portable Any Map (PBM, PGM, PPM)
- **TGA** - Targa Image File
- **HDR** - Radiance RGBE High Dynamic Range
- **ICO** - Windows Icon format
- **CUR** - Windows Cursor format
- **QOI** - Quite OK Image format (simple, fast)
- **PSD** - Adobe Photoshop Document (partial support)

### Optionally Supported (Optional Dependencies)
- **GIF** - Graphics Interchange Format (animated)
- **APNG** - Animated PNG format if built with libpng supporting APNG
- **WebP** - Google WebP format
- **HEIF/HEIC** - High Efficiency Image Format
- **TIFF** - Tagged Image File Format
- **RAW** - Camera raw formats (CR2, NEF, ARW, etc.)
- **JXL** - JPEG XL format (modern, high efficiency, HDR support)

**Note:** Format support depends on which optional libraries were available at build time. Run `imgcat2 --version` to see enabled formats.

### Known Limitations

#### SVG Rendering

- SVG gradients and filters have limited support (nanosvg limitation)
- Text in SVGs may not render perfectly (convert to paths for best results)
- SVGZ (compressed SVG) not supported - decompress first: `gunzip file.svgz`
- Complex SVGs with thousands of elements may render slowly
- No animation support (SMIL animations ignored)

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
5. Ensure all tests pass: `make test`
6. Submit a pull request

### Code Style

The project uses:
- **C11 standard** with GNU extensions (gnu11)
- **clang-format** for code formatting (`.clang-format` provided)
- **clang-tidy** for static analysis (`.clang-tidy` provided)
- **Doxygen** comments for documentation

Format code before committing:
```bash
clang-format -i src/**/*.c src/**/*.h
```

### Testing

Run tests with:
```bash
cd build
make test
# or
ctest -V
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Inspired by the original [imgcat](https://github.com/danielgatis/imgcat) by Daniel Gatis
- Built with [stb](https://github.com/nothings/stb) libraries for additional image format support

## Troubleshooting

### Image doesn't display correctly

**Problem:** Colors appear wrong or image is distorted

**Solutions:**
- Verify terminal supports true color: `echo $COLORTERM`
- Test color support with the commands in [Terminal Requirements](#terminal-requirements)
- Update terminal emulator to latest version

### Unsupported format error

**Problem:** "invalid MIME type" or format not supported

**Solutions:**
- Check enabled formats: `imgcat2 --version`
- Rebuild with format support: `cmake -DENABLE_WEBP=ON ..`
- Convert image: `convert input.webp png:- | imgcat2`

### Build fails with missing dependencies

**Problem:** CMake cannot find required libraries

**Solutions:**
- Install missing dependencies (see [Prerequisites](#prerequisites))
- Check pkg-config paths: `pkg-config --list-all | grep png`
- Set `PKG_CONFIG_PATH` environment variable if needed

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
