/*
   Copyright (C) 2013-2017 Red Hat, Inc.

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

#ifndef VDAGENT_IMAGE_H_
#define VDAGENT_IMAGE_H_

class ImageCoder
{
public:
    ImageCoder() {};
    virtual ~ImageCoder() {}
    virtual HANDLE create_dib_handle(const uint8_t *data, size_t size)=0;
    virtual void *from_bitmap(const BITMAPINFO& info, const void *bits, long &size)=0;
private:
    ImageCoder(const ImageCoder& rhs);
    void operator=(const ImageCoder &rhs);
};

/**
 * Compute stride in bytes of a DIB
 */
static inline DWORD compute_dib_stride(DWORD width, DWORD bit_count)
{
    return ((width * bit_count + 31u) & ~31u) / 8u;
}

static inline bool compute_dib_stride_checked(DWORD width, DWORD bit_count, DWORD *stride)
{
    if (!stride || bit_count == 0 || bit_count > 32)
        return false;
    if (width > (MAXDWORD - 31u) / bit_count)
        return false;
    *stride = ((width * bit_count + 31u) & ~31u) / 8u;
    return *stride != 0;
}

static inline bool compute_image_size_checked(DWORD stride, DWORD height, DWORD *size)
{
    if (!size || stride == 0 || height == 0)
        return false;
    if (stride > MAXDWORD / height)
        return false;
    *size = stride * height;
    return true;
}

ImageCoder *create_bitmap_coder();

/**
 * Returns image to put in the clipboard.
 *
 * @param         clipboard  data to write in the clipboard
 * @param         size       size of data
 * @param[in,out] format     suggested clipboard format. This can be changed by
 *                           the function to reflect a better format
 */
HANDLE get_image_handle(const VDAgentClipboard& clipboard, uint32_t size, UINT& format);

/**
 * Return raw data got from the clipboard.
 *
 * Function could use clip_data or get new data from the clipboard.
 * You should free data returned with free_raw_clipboard_image.
 * Encoded buffers are HGLOBAL moveable memory returned locked; the free
 * helper unlocks and frees the handle.
 * @param      clipboard_request  request
 * @param      clip_data          clipboard data
 * @param[out] new_size           size of returned data
 */
void* get_raw_clipboard_image(const VDAgentClipboardRequest& clipboard_request,
                              HANDLE clip_data, long& new_size);

/**
 * Free data returned by get_raw_clipboard_image
 */
void free_raw_clipboard_image(void *data);

#endif
