```
     /\_/\     _                           _    ____
    ( o.o )   (_)_ __ ___   __ _  ___ __ _| |_ |___ \
     > ^ <    | | '_ ` _ \ / _` |/ __/ _` | __| __) |
    /|   |\   | | | | | | | (_| | (_| (_| | |_ / __/
   (_|   |_)  |_|_| |_| |_|\__, |\___\__,_|\__|_____|
                           |___/

            -- Purr-fect terminal image viewer! --
            --   ANSI • Terminal • True Color   --
```

A command-line tool that displays images and animated GIFs directly in terminal emulators using standard ANSI escape sequences and Unicode characters.

## Features

- **Universal Compatibility** - Works with any modern terminal that supports true color and Unicode
- **Cross-Platform** - Supports Linux, macOS, Windows, and BSD systems
- **Multiple Image Formats** - PNG, JPEG, GIF (animated), BMP, and optionally WebP, HEIF, TIFF, RAW
- **Transparency Support** - Handles alpha channel with threshold-based rendering
- **Terminal-Aware Resizing** - Automatically scales images to fit your terminal
- **Animation Support** - Displays animated GIFs with smooth playback
- **High-Quality Scaling** - Uses advanced interpolation for photographic images
- **Pure C Implementation** - Fast, efficient, and lightweight (C11 standard)
- **Static Linking** - Produces fully static binaries where possible
- **No Proprietary Protocols** - Uses standard ANSI sequences, not terminal-specific features

## How It Works

imgcat2 uses the **half-block rendering technique** with Unicode character `▄` (U+2584) to achieve double vertical resolution:

- **Background color** represents the top pixel
- **Foreground color + half-block** represents the bottom pixel
- Each terminal cell displays **two pixels vertically**

This technique provides universal compatibility without requiring Sixel graphics, Kitty graphics protocol, or iTerm2 inline images.

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
sudo apt-get install libgif-dev libwebp-dev libheif-dev libtiff-dev libraw-dev

# Clone repository
git clone https://github.com/yourusername/imgcat2.git
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
brew install cmake libpng zlib-ng jpeg-turbo giflib webp libheif libtiff libraw

# Clone repository
git clone https://github.com/yourusername/imgcat2.git
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
sudo pacman -S cmake gcc libpng zlib libjpeg-turbo giflib libwebp libheif libtiff libraw

# Clone and build
git clone https://github.com/yourusername/imgcat2.git
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
pacman -S mingw-w64-x86_64-giflib mingw-w64-x86_64-libwebp

# Clone and build
git clone https://github.com/yourusername/imgcat2.git
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
cmake -DENABLE_GIF=OFF -DENABLE_WEBP=OFF -DENABLE_HEIF=OFF -DENABLE_TIFF=OFF -DENABLE_RAW=OFF ..

# Disable testing
cmake -DBUILD_TESTING=OFF ..

# Disable static analysis
cmake -DENABLE_CLANG_TIDY=OFF -DENABLE_CLANG_FORMAT=OFF ..

# Custom build type
cmake -DCMAKE_BUILD_TYPE=Release ..
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
Usage: imgcat2 [options] [file]

Options:
  --fit              Fit image to terminal (maintains aspect ratio) [default]
  --resize           Resize image to exact terminal dimensions
  --interpolation=<method>
                     Interpolation method: lanczos (default), nearest
  --top-offset=<n>   Number of lines to reserve at top (default: 8)
  --animate          Display animated GIFs (default for .gif files)
  --fps=<n>          Animation frame rate (default: 15)
  --silent           Suppress exit message during animation
  -h, --help         Show this help message
  -v, --version      Show version information

If [file] is not specified, reads from stdin.
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

**Adjust for different terminal layout:**
```bash
imgcat2 --top-offset=4 image.png
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

### Optionally Supported (Optional Dependencies)
- **GIF** - Graphics Interchange Format (animated)
- **WebP** - Google WebP format
- **HEIF/HEIC** - High Efficiency Image Format
- **TIFF** - Tagged Image File Format
- **RAW** - Camera raw formats (CR2, NEF, ARW, etc.)

**Note:** Format support depends on which optional libraries were available at build time. Run `imgcat2 --version` to see enabled formats.

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