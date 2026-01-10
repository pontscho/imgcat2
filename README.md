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
- **Multiple Image Formats** - PNG, JPEG, GIF (animated), BMP, TGA, HDR, PNM, ICO, CUR, QOI, PSD (partial), and optionally WebP, HEIF, TIFF, RAW, JXL, SVG
- **Native Protocol Support** - Automatically uses iTerm2 inline images or Ghostty Kitty graphics protocol for higher quality when available
- **Transparency Support** - Handles alpha channel with threshold-based rendering
- **Terminal-Aware Resizing** - Automatically scales images to fit your terminal
- **Animation Support** - Displays animated GIFs with smooth playback (native in iTerm2)
- **High-Quality Scaling** - Uses advanced interpolation for photographic images
- **Pure C Implementation** - Fast, efficient, and lightweight (C11 standard)
- **Static Linking** - Produces fully static binaries where possible
- **Intelligent Fallback** - Automatically falls back to ANSI rendering when needed

## How It Works

imgcat2 supports multiple rendering modes depending on your terminal:

### Native Protocol Mode (iTerm2 / Ghostty / Kitty)
When running in iTerm2, Ghostty, or Kitty, imgcat2 uses native image protocols for the highest quality:
- **iTerm2**: Uses OSC 1337 inline images protocol
- **Ghostty**: Uses Kitty graphics protocol
- **Kitty**: Uses Kitty graphics protocol
- Automatic detection based on `TERM_PROGRAM` environment variable
- Falls back to ANSI rendering if protocol fails

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
- **zlib** or **zlib-ng** (recommended for better performance)
- **libjpeg** or **libjpeg-turbo** (recommended for faster JPEG decoding)

### Optional Dependencies

- **giflib** - For GIF animation support
- **libwebp** - For WebP format support
- **libheif** - For HEIF/HEIC format support
- **libtiff** - For TIFF format support
- **libraw** - For RAW camera format support
- **libjxl** - For JPEG XL format support
- **nanosvg** - For SVG vector graphics support (header-only, included as submodule)

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

### Linux (Debian/Ubuntu)

```bash
# Install required dependencies
sudo apt-get update
sudo apt-get install cmake gcc libpng-dev zlib1g-dev libjpeg-dev

# Install optional dependencies
sudo apt-get install libgif-dev libwebp-dev libheif-dev libtiff-dev libraw-dev libjxl-dev

# Clone repository with submodules for full format support
git clone --recurse-submodules https://github.com/yourusername/imgcat2.git

# Or if already cloned, initialize submodules
git submodule update --init --recursive

cd imgcat2

# Build
mkdir build && cd build
cmake ..
make

# Install (optional)
sudo make install
```

### macOS (Homebrew)

```bash
# Install dependencies
brew install cmake libpng zlib-ng jpeg-turbo giflib webp libheif libtiff libraw jpeg-xl

# Clone repository with submodules for full format support
git clone --recurse-submodules https://github.com/yourusername/imgcat2.git

# Or if already cloned, initialize submodules
git submodule update --init --recursive

cd imgcat2

# Build
mkdir build && cd build
cmake ..
make

# Install (optional)
sudo make install
```

### Arch Linux

```bash
# Install dependencies
sudo pacman -S cmake gcc libpng zlib libjpeg-turbo giflib libwebp libheif libtiff libraw libjxl

# Clone with submodules and build
git clone --recurse-submodules https://github.com/yourusername/imgcat2.git
cd imgcat2
mkdir build && cd build
cmake ..
make

# Install (optional)
sudo make install
```

### Windows (MinGW)

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

# Binary will be in build/imgcat2.exe
```

### Build Options

You can customize the build with CMake options:

```bash
# Minimal build (only required formats)
cmake -DENABLE_GIF=OFF -DENABLE_WEBP=OFF -DENABLE_HEIF=OFF -DENABLE_TIFF=OFF -DENABLE_RAW=OFF -DENABLE_JXL=OFF -DENABLE_SVG=OFF ..

# Disable testing
cmake -DBUILD_TESTING=OFF ..

# Disable static analysis
cmake -DENABLE_CLANG_TIDY=OFF -DENABLE_CLANG_FORMAT=OFF ..

# Custom build type
cmake -DCMAKE_BUILD_TYPE=Release ..
```

## iTerm2 Native Support

When running in iTerm2, imgcat2 automatically uses the native inline images protocol for significantly better quality:

### Advantages in iTerm2
- **Higher Quality** - Native image rendering without ANSI approximation
- **Better Colors** - No color quantization or dithering
- **Faster Rendering** - Direct image display without pixel-by-pixel processing
- **Native Animations** - Smooth GIF playback handled by iTerm2
- **Original Image Size** - Displays images at their native resolution by default
- **Custom Sizing** - Supports `-w` and `-H` flags for pixel-perfect sizing

### How It Works
imgcat2 automatically detects iTerm2 by checking the `TERM_PROGRAM` environment variable. When detected:
1. Validates image format is supported (PNG, JPEG, GIF, BMP)
2. Sends image data via iTerm2's OSC 1337 protocol
3. **Default behavior**: Displays image at original size (no scaling)
4. **With `-w` or `-H`**: Scales to specified pixel dimensions
5. Automatically falls back to ANSI rendering if protocol fails

### Force ANSI Rendering
To disable native protocols (iTerm2/Ghostty) and use ANSI rendering:
```bash
imgcat2 --force-ansi image.png
```

This is useful for:
- Testing ANSI rendering in iTerm2 or Ghostty
- Compatibility with tools that expect ANSI output
- Scenarios where native protocols may not work (e.g., remote sessions)

### iTerm2 Sizing Examples
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

## Ghostty Native Support

When running in Ghostty terminal, imgcat2 automatically uses the Kitty graphics protocol for high-quality image rendering:

### Advantages in Ghostty or Kitty
- **Higher Quality** - Native rendering using Kitty graphics protocol (PNG direct, JPEG/GIF decoded)
- **Better Colors** - No color quantization or dithering
- **Faster Rendering** - Direct image display without pixel-by-pixel processing
- **Automatic Scaling** - Images scale to fit terminal window while preserving aspect ratio
- **Custom Sizing** - Supports `-w` and `-H` flags for precise control
- **Format Support:** PNG (direct f=100), JPEG (decoded f=32), static GIF (decoded f=32)
- **Note:** Animated GIFs will use ANSI rendering for frame-by-frame animation

### How It Works
imgcat2 automatically detects Ghostty by checking the `TERM_PROGRAM` environment variable. When detected:
1. **PNG images**: Sent directly via Kitty protocol with `f=100` (no decoding needed)
2. **JPEG images**: Decoded to RGBA and sent with `f=32` format
3. **Static GIF images**: Decoded to RGBA and sent with `f=32` format
4. **Animated GIF images**: Fall back to ANSI rendering for frame-by-frame animation
5. **Default behavior**: Fits image to terminal width, preserving aspect ratio
6. **With `-w` or `-H`**: Scales to specified pixel dimensions (converted to terminal cells)

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
      --info                Output image metadata instead of rendering
      --json                Format --info output as JSON (single line)

Arguments:
  FILE                      Input image file (omit or '-' for stdin)

Examples:
  ./imgcat2 image.png              Display PNG image
  ./imgcat2 -a animation.gif       Animate GIF
  cat image.jpg | ./imgcat2        Read from stdin
  ./imgcat2 --fps 10 anim.gif      Animate at 10 FPS
```

### Examples

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
imgcat2 animation.gif
# Press Ctrl+C to exit
```

**Custom animation frame rate:**
```bash
imgcat2 --fps=20 animation.gif
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

## Performance Considerations

### Image Resolution
- Terminal resolution limits effective image detail
- Example: 80×24 terminal = 80×48 effective pixels
- Large images are automatically downscaled

### Memory Usage
- Typical image: 1-5 MB in memory
- Animated GIF: All frames loaded into memory (5-50 MB typical)
- Very large images (>100 MB decoded) may exceed available memory

### Animation Performance
- Fixed frame rate at 15 FPS by default
- Frame rate may drop if terminal rendering is slow
- Large GIFs (>100 frames) may take longer to load initially

## Documentation

For detailed technical information:

- **[ANSI Implementation Guide](docs/imgcat-ansi.md)** - Technical details of the rendering technique
- Additional documentation in `docs/` directory

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
- Uses the half-block rendering technique with ANSI true color escape sequences
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
