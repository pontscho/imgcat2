#!/usr/bin/env python3
"""
Font embedding tool for imgcat2
Converts TTF font files to C byte arrays for embedding in the binary
"""

import sys
import os
import glob

def embed_single_font(ttf_path, var_name):
    """Convert a single TTF file to C array data"""
    with open(ttf_path, 'rb') as f:
        data = f.read()

    # Generate C array content
    c_content = f'const uint8_t {var_name}_data[] = {{\n'
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        c_content += '    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n'
    c_content += '};\n'
    c_content += f'const size_t {var_name}_size = {len(data)};\n\n'

    return c_content, len(data)

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <font_dir> <output_base>")
        print(f"Example: {sys.argv[0]} src/imgcat2/font src/imgcat2/font/embedded_fonts")
        sys.exit(1)

    font_dir = sys.argv[1]
    output_base = sys.argv[2]

    # Find all TTF files in the font directory
    ttf_files = glob.glob(os.path.join(font_dir, "*.ttf"))

    if not ttf_files:
        print(f"Error: No TTF files found in {font_dir}")
        sys.exit(1)

    print(f"Found {len(ttf_files)} font file(s):")
    for ttf in ttf_files:
        print(f"  - {os.path.basename(ttf)}")

    # Map font files to variant names
    font_map = {}
    for ttf_path in ttf_files:
        basename = os.path.basename(ttf_path).lower()

        if 'bold' in basename and 'oblique' in basename:
            font_map['bold_italic'] = ttf_path
        elif 'bold' in basename:
            font_map['bold'] = ttf_path
        elif 'oblique' in basename or 'italic' in basename:
            font_map['italic'] = ttf_path
        elif 'book' in basename or 'regular' in basename:
            font_map['regular'] = ttf_path
        else:
            # Default to regular if not sure
            font_map.setdefault('regular', ttf_path)

    # Generate header file
    h_content = """#ifndef EMBEDDED_FONTS_H
#define EMBEDDED_FONTS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
} embedded_font_t;

"""

    # Generate C source file
    c_content = f'#include "{os.path.basename(output_base)}.h"\n\n'

    total_size = 0
    variants_embedded = []

    # Embed each variant
    for variant, ttf_path in sorted(font_map.items()):
        var_name = f'embedded_dejavu_{variant}'

        print(f"\nEmbedding {variant}: {os.path.basename(ttf_path)}")

        array_code, size = embed_single_font(ttf_path, var_name)
        c_content += array_code

        # Add extern declaration to header
        h_content += f'extern const uint8_t {var_name}_data[];\n'
        h_content += f'extern const size_t {var_name}_size;\n'
        h_content += f'extern const embedded_font_t {var_name};\n\n'

        # Add struct definition to source
        c_content += f'const embedded_font_t {var_name} = {{\n'
        c_content += f'    .data = {var_name}_data,\n'
        c_content += f'    .size = {var_name}_size\n'
        c_content += '};\n\n'

        total_size += size
        variants_embedded.append(variant)

        print(f"  Size: {size:,} bytes ({size/1024:.1f} KB)")

    # Close header
    h_content += '#endif /* EMBEDDED_FONTS_H */\n'

    # Write files
    h_file = f'{output_base}.h'
    c_file = f'{output_base}.c'

    with open(h_file, 'w') as f:
        f.write(h_content)

    with open(c_file, 'w') as f:
        f.write(c_content)

    print(f"\n✓ Successfully generated:")
    print(f"  - {h_file}")
    print(f"  - {c_file}")
    print(f"\nTotal embedded size: {total_size:,} bytes ({total_size/1024:.1f} KB)")
    print(f"Variants embedded: {', '.join(variants_embedded)}")

    # Warn about missing variants
    expected_variants = {'regular', 'bold', 'italic', 'bold_italic'}
    missing = expected_variants - set(variants_embedded)
    if missing:
        print(f"\n⚠ Warning: Missing font variants: {', '.join(missing)}")
        print(f"  Text rendering will fall back to available variants")

if __name__ == '__main__':
    main()
