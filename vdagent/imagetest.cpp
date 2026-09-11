/*
   Copyright (C) 2017 Red Hat, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#undef NDEBUG
#include <assert.h>
#include <vector>
#include <memory>

#include "vdcommon.h"
#include "image.h"
#include "imagepng.h"

#include <objbase.h>

static void
save_dib_to_file(ImageCoder& coder, const uint8_t *raw_dib, const char *filename)
{
    const BITMAPINFO& info(*(BITMAPINFO*) raw_dib);
    const uint8_t *raw_bits = &raw_dib[sizeof(BITMAPINFOHEADER) + 4 * info.bmiHeader.biClrUsed];

    long size = 0;
    void *raw_file = coder.from_bitmap(info, raw_bits, size);
    assert(raw_file && size > 0);

    FILE *f = fopen(filename, "wb");
    assert(f);
    assert(fwrite(raw_file, 1, size, f) == (unsigned long) size);
    fclose(f);
    free_raw_clipboard_image(raw_file);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <in-image> [<out-bmp> [<out-png>]]\n", argv[0]);
        return 1;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        fprintf(stderr, "CoInitializeEx() failed: %lu\n", GetLastError());
        return 1;
    }

    std::unique_ptr<ImageCoder> coder(create_png_coder());
    assert(coder);

    // read all file into memory
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long len = ftell(f);
    assert(len > 0);
    assert(fseek(f, 0, SEEK_SET) == 0);

    std::vector<uint8_t> data(len);
    assert(fread(&data[0], 1, len, f) == (unsigned long) len);
    fclose(f);

    size_t dib_size = coder->get_dib_size(&data[0], len);
    assert(dib_size);
    std::vector<uint8_t> out(dib_size);
    memset(&out[0], 0xcc, dib_size);
    coder->get_dib_data(&out[0], &data[0], len);

    // write BMP file
    std::unique_ptr<ImageCoder> bmp_coder(create_bitmap_coder());
    assert(bmp_coder);
    save_dib_to_file(*bmp_coder, &out[0], argc > 2 ? argv[2] : "out.bmp");

    // convert back to PNG
    save_dib_to_file(*coder, &out[0], argc > 3 ? argv[3] : "out.png");

    CoUninitialize();
    return 0;
}

