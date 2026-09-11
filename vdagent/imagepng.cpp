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

#include "vdcommon.h"

#include <algorithm>
#include <climits>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "imagepng.h"

using Microsoft::WRL::ComPtr;

static void log_hr(const char *what, HRESULT hr)
{
    vd_printf("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

class PngCoder: public ImageCoder
{
public:
    PngCoder() {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            log_hr("CoCreateInstance(WICImagingFactory)", hr);
            factory.Reset();
        }
    };

    size_t get_dib_size(const uint8_t *data, size_t size);
    void get_dib_data(uint8_t *dib, const uint8_t *data, size_t size);
    void *from_bitmap(const BITMAPINFO& info, const void *bits, long &size);
private:
    size_t convert_to_dib(uint8_t *out_buf, const uint8_t *data, size_t size);
    ComPtr<IWICImagingFactory> factory;
};

typedef void line_fixup_t(uint8_t *dst, const uint8_t *src, unsigned int width);

static void line_fixup_2bpp_to_4bpp(uint8_t *dst, const uint8_t *src,
                                    unsigned int width)
{
    width = (width + 3) / 4u;
    while (width--) {
        uint8_t from = src[width];
        dst[width*2+1] = ((from & 0x03) << 0) | ((from & 0x0c) << 2);
        dst[width*2+0] = ((from & 0x30) >> 4) | ((from & 0xc0) >> 2);
    }
}

size_t PngCoder::get_dib_size(const uint8_t *data, size_t size)
{
    return convert_to_dib(NULL, data, size);
}

size_t PngCoder::convert_to_dib(uint8_t *out_buf, const uint8_t *data, size_t size)
{
    if (!factory)
        return 0;

    if (size == 0 || size > MAXUINT)
        return 0;

    ComPtr<IStream> stream = SHCreateMemStream(data, static_cast<UINT>(size));
    if (!stream) {
        vd_printf("failed to create PNG stream");
        return 0;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT width = 0;
    UINT height = 0;
    WICPixelFormatGUID format = GUID_NULL;
    HRESULT hr = factory->CreateDecoder(GUID_ContainerFormatPng, nullptr, &decoder);
    if (FAILED(hr) ||
        FAILED(hr = decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) ||
        FAILED(hr = decoder->GetFrame(0, &frame)) ||
        FAILED(hr = frame->GetSize(&width, &height)) ||
        FAILED(hr = frame->GetPixelFormat(&format))) {
        log_hr("decode png", hr);
        return 0;
    }

    if (width == 0 || height == 0)
        return 0;

    auto out_format = IsEqualGUID(format, GUID_WICPixelFormat16bppGray) ?
                      GUID_WICPixelFormat8bppGray : format;
    unsigned int bits;
    unsigned int out_bits;
    bool is_gray = false;
    line_fixup_t *line_fixup = nullptr;
    if (IsEqualGUID(out_format, GUID_WICPixelFormat1bppIndexed)) {
        bits = 1;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormat2bppIndexed)) {
        bits = 2;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormat4bppIndexed)) {
        bits = 4;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormat8bppIndexed)) {
        bits = 8;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormatBlackWhite)) {
        bits = 1;
        is_gray = true;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormat2bppGray)) {
        bits = 2;
        is_gray = true;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormat4bppGray)) {
        bits = 4;
        is_gray = true;
    } else if (IsEqualGUID(out_format, GUID_WICPixelFormat8bppGray)) {
        bits = 8;
        is_gray = true;
    } else {
        out_format = GUID_WICPixelFormat24bppBGR;
        bits = 24;
    }

    // BMP does not support 2 bpp; expand to 4 bpp as libpng did.
    if (bits == 2) {
        line_fixup = line_fixup_2bpp_to_4bpp;
        out_bits = 4;
    } else {
        out_bits = bits;
    }

    DWORD out_stride = 0;
    DWORD out_image_size = 0;
    DWORD in_stride = 0;
    DWORD in_image_size = 0;
    if (!compute_dib_stride_checked(width, out_bits, &out_stride) ||
        !compute_image_size_checked(out_stride, height, &out_image_size) ||
        !compute_dib_stride_checked(width, bits, &in_stride) ||
        !compute_image_size_checked(in_stride, height, &in_image_size)) {
        vd_printf("PNG dimensions overflow width=%u height=%u bits=%u", width, height, bits);
        return 0;
    }

    DWORD out_num_colors = 0;
    if (out_bits <= 8)
        out_num_colors = (bits == 2) ? 4u : (1u << out_bits);
    const size_t palette_size = static_cast<size_t>(out_num_colors) * sizeof(RGBQUAD);
    const size_t dib_size = sizeof(BITMAPINFOHEADER) + palette_size + out_image_size;
    if (dib_size < sizeof(BITMAPINFOHEADER) || dib_size < out_image_size)
        return 0;

    if (!out_buf)
        return dib_size;

    BITMAPINFOHEADER& head(*(BITMAPINFOHEADER *)out_buf);
    memset(&head, 0, sizeof(head));
    head.biSize = sizeof(head);
    head.biWidth = static_cast<LONG>(width);
    head.biHeight = static_cast<LONG>(height);
    head.biPlanes = 1;
    head.biBitCount = static_cast<WORD>(out_bits);
    head.biCompression = BI_RGB;
    head.biSizeImage = out_image_size;

    ComPtr<IWICPalette> palette;
    RGBQUAD *rgb = (RGBQUAD *)(out_buf + sizeof(BITMAPINFOHEADER));
    if (is_gray) {
        const DWORD src_colors = (bits == 2) ? 4u : (1u << bits);
        const DWORD ramp = src_colors > 1 ? src_colors - 1 : 1;
        for (DWORD i = 0; i < out_num_colors; ++i) {
            BYTE v = (i < src_colors)
                ? static_cast<BYTE>((i * 255u) / ramp)
                : 0;
            rgb->rgbBlue = rgb->rgbGreen = rgb->rgbRed = v;
            rgb->rgbReserved = 0;
            ++rgb;
        }
        head.biClrUsed = out_num_colors;
    } else if (out_bits <= 8) {
        WICColor colors[256];
        UINT num_colors = 0;
        memset(colors, 0, sizeof(colors));
        hr = factory->CreatePalette(&palette);
        if (FAILED(hr) ||
            FAILED(hr = frame->CopyPalette(palette.Get())) ||
            FAILED(hr = palette->GetColors(256, colors, &num_colors))) {
            log_hr("copy png palette", hr);
            return 0;
        }

        for (DWORD i = 0; i < out_num_colors; ++i) {
            if (i < num_colors) {
                rgb->rgbBlue = colors[i] & 0xff;
                rgb->rgbGreen = (colors[i] >> 8) & 0xff;
                rgb->rgbRed = (colors[i] >> 16) & 0xff;
            } else {
                rgb->rgbBlue = rgb->rgbGreen = rgb->rgbRed = 0;
            }
            rgb->rgbReserved = 0;
            ++rgb;
        }
        head.biClrUsed = out_num_colors;
    }

    ComPtr<IWICBitmapFlipRotator> rotator;
    hr = factory->CreateBitmapFlipRotator(&rotator);
    if (FAILED(hr) ||
        FAILED(hr = rotator->Initialize(frame.Get(), WICBitmapTransformFlipVertical))) {
        log_hr("flip png", hr);
        return 0;
    }

    ComPtr<IWICFormatConverter> converter;
    IWICBitmapSource *source = nullptr;
    if (IsEqualGUID(format, out_format)) {
        source = rotator.Get();
    } else {
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr) ||
            FAILED(hr = converter->Initialize(rotator.Get(), out_format,
                                              WICBitmapDitherTypeNone, palette.Get(),
                                              0, WICBitmapPaletteTypeCustom))) {
            log_hr("convert png format", hr);
            return 0;
        }
        source = converter.Get();
    }

    uint8_t *dst = out_buf + sizeof(BITMAPINFOHEADER) + palette_size;
    hr = source->CopyPixels(nullptr, in_stride, in_image_size, dst);
    if (FAILED(hr)) {
        log_hr("copy png pixels", hr);
        return 0;
    }

    if (line_fixup) {
        uint8_t *src = dst + in_image_size;
        dst += out_image_size;
        for (UINT row = 0; row < height; ++row) {
            ((uint32_t*)dst)[-1] = 0;
            src -= in_stride;
            dst -= out_stride;
            line_fixup(dst, src, width);
        }
    }

    return dib_size;
}

void PngCoder::get_dib_data(uint8_t *dib, const uint8_t *data, size_t size)
{
    convert_to_dib(dib, data, size);
}

void *PngCoder::from_bitmap(const BITMAPINFO& bmp_info, const void *bits, long &size)
{
    size = 0;
    if (!factory || !bits)
        return nullptr;

    const BITMAPINFOHEADER& head(bmp_info.bmiHeader);
    if (head.biWidth <= 0 || head.biHeight <= 0 || head.biCompression != BI_RGB)
        return nullptr;

    const DWORD width = static_cast<DWORD>(head.biWidth);
    const DWORD height = static_cast<DWORD>(head.biHeight);
    const DWORD out_bits = head.biBitCount;
    DWORD max_colors = out_bits <= 8 ? (1u << out_bits) : 0;
    DWORD num_colors = 0;
    if (max_colors) {
        num_colors = head.biClrUsed ? std::min(head.biClrUsed, max_colors) : max_colors;
    }

    WICPixelFormatGUID format;
    switch (out_bits) {
    case 1:
        format = GUID_WICPixelFormat1bppIndexed;
        break;
    case 4:
        format = GUID_WICPixelFormat4bppIndexed;
        break;
    case 8:
        format = GUID_WICPixelFormat8bppIndexed;
        break;
    case 24:
        format = GUID_WICPixelFormat24bppBGR;
        break;
    case 32:
        format = GUID_WICPixelFormat32bppBGR;
        break;
    default:
        vd_printf("BMP bit count %d not supported", out_bits);
        return nullptr;
    }

    WICColor colors[256];
    memset(colors, 0, sizeof(colors));
    if (num_colors) {
        const RGBQUAD *rgb = bmp_info.bmiColors;
        for (DWORD i = 0; i < num_colors; ++i) {
            colors[i] = 0xff000000 |
                        (static_cast<WICColor>(rgb->rgbRed) << 16) |
                        (static_cast<WICColor>(rgb->rgbGreen) << 8) |
                        rgb->rgbBlue;
            ++rgb;
        }
    }

    DWORD stride = 0;
    DWORD image_size = 0;
    if (!compute_dib_stride_checked(width, out_bits, &stride) ||
        !compute_image_size_checked(stride, height, &image_size)) {
        vd_printf("BMP dimensions overflow width=%lu height=%lu bits=%lu",
                  static_cast<unsigned long>(width),
                  static_cast<unsigned long>(height),
                  static_cast<unsigned long>(out_bits));
        return nullptr;
    }

    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICPalette> palette;
    ComPtr<IWICBitmapFlipRotator> rotator;
    HRESULT hr = factory->CreateBitmapFromMemory(width, height, format, stride,
                                                 image_size,
                                                 const_cast<BYTE *>(static_cast<const BYTE *>(bits)),
                                                 &bitmap);
    if (FAILED(hr) ||
        (num_colors &&
         (FAILED(hr = factory->CreatePalette(&palette)) ||
          FAILED(hr = palette->InitializeCustom(colors, num_colors)) ||
          FAILED(hr = bitmap->SetPalette(palette.Get())))) ||
        FAILED(hr = factory->CreateBitmapFlipRotator(&rotator)) ||
        FAILED(hr = rotator->Initialize(bitmap.Get(), WICBitmapTransformFlipVertical))) {
        log_hr("create png bitmap", hr);
        return nullptr;
    }

    ComPtr<IWICBitmapSource> source;
    if (out_bits <= 24) {
        source = std::move(rotator);
    } else {
        hr = WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR,
                                    rotator.Get(), &source);
        if (FAILED(hr)) {
            log_hr("convert bmp to 24bpp", hr);
            return nullptr;
        }
    }

    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, 0);
    if (!hmem) {
        vd_printf("failed to allocate PNG stream: %lu", GetLastError());
        return nullptr;
    }

    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    hr = CreateStreamOnHGlobal(hmem, FALSE, &stream);
    if (FAILED(hr) ||
        FAILED(hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(hr = encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(hr = frame->Initialize(nullptr)) ||
        FAILED(hr = frame->WriteSource(source.Get(), nullptr)) ||
        FAILED(hr = frame->Commit()) ||
        FAILED(hr = encoder->Commit())) {
        log_hr("encode png", hr);
        GlobalFree(hmem);
        return nullptr;
    }

    SIZE_T gsize = GlobalSize(hmem);
    if (gsize == 0 || gsize > static_cast<SIZE_T>(LONG_MAX)) {
        GlobalFree(hmem);
        return nullptr;
    }

    void *data = GlobalLock(hmem);
    if (!data) {
        vd_printf("failed to lock PNG stream: %lu", GetLastError());
        GlobalFree(hmem);
        return nullptr;
    }

    size = static_cast<long>(gsize);
    return data;
}

ImageCoder *create_png_coder()
{
    return new PngCoder();
}
