/*
 * Copyright (C) 2009 Tony Wasserka
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */


#include "d3dx9_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dx);

D3DFORMAT d3dformat_from_d3dx_pixel_format_id(enum d3dx_pixel_format_id format)
{
    switch (format)
    {
        case D3DX_PIXEL_FORMAT_B8G8R8_UNORM:             return D3DFMT_R8G8B8;
        case D3DX_PIXEL_FORMAT_B8G8R8A8_UNORM:           return D3DFMT_A8R8G8B8;
        case D3DX_PIXEL_FORMAT_B8G8R8X8_UNORM:           return D3DFMT_X8R8G8B8;
        case D3DX_PIXEL_FORMAT_R8G8B8A8_UNORM:           return D3DFMT_A8B8G8R8;
        case D3DX_PIXEL_FORMAT_R8G8B8X8_UNORM:           return D3DFMT_X8B8G8R8;
        case D3DX_PIXEL_FORMAT_B5G6R5_UNORM:             return D3DFMT_R5G6B5;
        case D3DX_PIXEL_FORMAT_B5G5R5X1_UNORM:           return D3DFMT_X1R5G5B5;
        case D3DX_PIXEL_FORMAT_B5G5R5A1_UNORM:           return D3DFMT_A1R5G5B5;
        case D3DX_PIXEL_FORMAT_B2G3R3_UNORM:             return D3DFMT_R3G3B2;
        case D3DX_PIXEL_FORMAT_B2G3R3A8_UNORM:           return D3DFMT_A8R3G3B2;
        case D3DX_PIXEL_FORMAT_B4G4R4A4_UNORM:           return D3DFMT_A4R4G4B4;
        case D3DX_PIXEL_FORMAT_B4G4R4X4_UNORM:           return D3DFMT_X4R4G4B4;
        case D3DX_PIXEL_FORMAT_B10G10R10A2_UNORM:        return D3DFMT_A2R10G10B10;
        case D3DX_PIXEL_FORMAT_R10G10B10A2_UNORM:        return D3DFMT_A2B10G10R10;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_UNORM:       return D3DFMT_A16B16G16R16;
        case D3DX_PIXEL_FORMAT_R16G16_UNORM:             return D3DFMT_G16R16;
        case D3DX_PIXEL_FORMAT_A8_UNORM:                 return D3DFMT_A8;
        case D3DX_PIXEL_FORMAT_L8A8_UNORM:               return D3DFMT_A8L8;
        case D3DX_PIXEL_FORMAT_L4A4_UNORM:               return D3DFMT_A4L4;
        case D3DX_PIXEL_FORMAT_L8_UNORM:                 return D3DFMT_L8;
        case D3DX_PIXEL_FORMAT_L16_UNORM:                return D3DFMT_L16;
        case D3DX_PIXEL_FORMAT_DXT1_UNORM:               return D3DFMT_DXT1;
        case D3DX_PIXEL_FORMAT_DXT2_UNORM:               return D3DFMT_DXT2;
        case D3DX_PIXEL_FORMAT_DXT3_UNORM:               return D3DFMT_DXT3;
        case D3DX_PIXEL_FORMAT_DXT4_UNORM:               return D3DFMT_DXT4;
        case D3DX_PIXEL_FORMAT_DXT5_UNORM:               return D3DFMT_DXT5;
        case D3DX_PIXEL_FORMAT_R16_FLOAT:                return D3DFMT_R16F;
        case D3DX_PIXEL_FORMAT_R16G16_FLOAT:             return D3DFMT_G16R16F;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_FLOAT:       return D3DFMT_A16B16G16R16F;
        case D3DX_PIXEL_FORMAT_R32_FLOAT:                return D3DFMT_R32F;
        case D3DX_PIXEL_FORMAT_R32G32_FLOAT:             return D3DFMT_G32R32F;
        case D3DX_PIXEL_FORMAT_R32G32B32A32_FLOAT:       return D3DFMT_A32B32G32R32F;
        case D3DX_PIXEL_FORMAT_P8_UINT:                  return D3DFMT_P8;
        case D3DX_PIXEL_FORMAT_P8_UINT_A8_UNORM:         return D3DFMT_A8P8;
        case D3DX_PIXEL_FORMAT_U8V8W8Q8_SNORM:           return D3DFMT_Q8W8V8U8;
        case D3DX_PIXEL_FORMAT_U8V8_SNORM:               return D3DFMT_V8U8;
        case D3DX_PIXEL_FORMAT_U8V8_SNORM_Cx:            return D3DFMT_CxV8U8;
        case D3DX_PIXEL_FORMAT_U16V16_SNORM:             return D3DFMT_V16U16;
        case D3DX_PIXEL_FORMAT_U8V8_SNORM_L8X8_UNORM:    return D3DFMT_X8L8V8U8;
        case D3DX_PIXEL_FORMAT_U10V10W10_SNORM_A2_UNORM: return D3DFMT_A2W10V10U10;
        case D3DX_PIXEL_FORMAT_U16V16W16Q16_SNORM:       return D3DFMT_Q16W16V16U16;
        case D3DX_PIXEL_FORMAT_G8R8_G8B8_UNORM:          return D3DFMT_G8R8_G8B8;
        case D3DX_PIXEL_FORMAT_R8G8_B8G8_UNORM:          return D3DFMT_R8G8_B8G8;
        case D3DX_PIXEL_FORMAT_UYVY:                     return D3DFMT_UYVY;
        case D3DX_PIXEL_FORMAT_YUY2:                     return D3DFMT_YUY2;
        default:
        {
            const struct pixel_format_desc *fmt_desc = get_d3dx_pixel_format_info(format);

            if (!is_internal_format(fmt_desc) && !is_dxgi_format(fmt_desc))
                FIXME("Unknown d3dx_pixel_format_id %u.\n", format);
            return D3DFMT_UNKNOWN;
        }
    }
}

enum d3dx_pixel_format_id d3dx_pixel_format_id_from_d3dformat(D3DFORMAT format)
{
    switch (format)
    {
        case D3DFMT_R8G8B8:        return D3DX_PIXEL_FORMAT_B8G8R8_UNORM;
        case D3DFMT_A8R8G8B8:      return D3DX_PIXEL_FORMAT_B8G8R8A8_UNORM;
        case D3DFMT_X8R8G8B8:      return D3DX_PIXEL_FORMAT_B8G8R8X8_UNORM;
        case D3DFMT_A8B8G8R8:      return D3DX_PIXEL_FORMAT_R8G8B8A8_UNORM;
        case D3DFMT_X8B8G8R8:      return D3DX_PIXEL_FORMAT_R8G8B8X8_UNORM;
        case D3DFMT_R5G6B5:        return D3DX_PIXEL_FORMAT_B5G6R5_UNORM;
        case D3DFMT_X1R5G5B5:      return D3DX_PIXEL_FORMAT_B5G5R5X1_UNORM;
        case D3DFMT_A1R5G5B5:      return D3DX_PIXEL_FORMAT_B5G5R5A1_UNORM;
        case D3DFMT_R3G3B2:        return D3DX_PIXEL_FORMAT_B2G3R3_UNORM;
        case D3DFMT_A8R3G3B2:      return D3DX_PIXEL_FORMAT_B2G3R3A8_UNORM;
        case D3DFMT_A4R4G4B4:      return D3DX_PIXEL_FORMAT_B4G4R4A4_UNORM;
        case D3DFMT_X4R4G4B4:      return D3DX_PIXEL_FORMAT_B4G4R4X4_UNORM;
        case D3DFMT_A2R10G10B10:   return D3DX_PIXEL_FORMAT_B10G10R10A2_UNORM;
        case D3DFMT_A2B10G10R10:   return D3DX_PIXEL_FORMAT_R10G10B10A2_UNORM;
        case D3DFMT_A16B16G16R16:  return D3DX_PIXEL_FORMAT_R16G16B16A16_UNORM;
        case D3DFMT_G16R16:        return D3DX_PIXEL_FORMAT_R16G16_UNORM;
        case D3DFMT_A8:            return D3DX_PIXEL_FORMAT_A8_UNORM;
        case D3DFMT_A8L8:          return D3DX_PIXEL_FORMAT_L8A8_UNORM;
        case D3DFMT_A4L4:          return D3DX_PIXEL_FORMAT_L4A4_UNORM;
        case D3DFMT_L8:            return D3DX_PIXEL_FORMAT_L8_UNORM;
        case D3DFMT_L16:           return D3DX_PIXEL_FORMAT_L16_UNORM;
        case D3DFMT_DXT1:          return D3DX_PIXEL_FORMAT_DXT1_UNORM;
        case D3DFMT_DXT2:          return D3DX_PIXEL_FORMAT_DXT2_UNORM;
        case D3DFMT_DXT3:          return D3DX_PIXEL_FORMAT_DXT3_UNORM;
        case D3DFMT_DXT4:          return D3DX_PIXEL_FORMAT_DXT4_UNORM;
        case D3DFMT_DXT5:          return D3DX_PIXEL_FORMAT_DXT5_UNORM;
        case D3DFMT_R16F:          return D3DX_PIXEL_FORMAT_R16_FLOAT;
        case D3DFMT_G16R16F:       return D3DX_PIXEL_FORMAT_R16G16_FLOAT;
        case D3DFMT_A16B16G16R16F: return D3DX_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        case D3DFMT_R32F:          return D3DX_PIXEL_FORMAT_R32_FLOAT;
        case D3DFMT_G32R32F:       return D3DX_PIXEL_FORMAT_R32G32_FLOAT;
        case D3DFMT_A32B32G32R32F: return D3DX_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        case D3DFMT_P8:            return D3DX_PIXEL_FORMAT_P8_UINT;
        case D3DFMT_A8P8:          return D3DX_PIXEL_FORMAT_P8_UINT_A8_UNORM;
        case D3DFMT_Q8W8V8U8:      return D3DX_PIXEL_FORMAT_U8V8W8Q8_SNORM;
        case D3DFMT_V8U8:          return D3DX_PIXEL_FORMAT_U8V8_SNORM;
        case D3DFMT_CxV8U8:        return D3DX_PIXEL_FORMAT_U8V8_SNORM_Cx;
        case D3DFMT_V16U16:        return D3DX_PIXEL_FORMAT_U16V16_SNORM;
        case D3DFMT_X8L8V8U8:      return D3DX_PIXEL_FORMAT_U8V8_SNORM_L8X8_UNORM;
        case D3DFMT_A2W10V10U10:   return D3DX_PIXEL_FORMAT_U10V10W10_SNORM_A2_UNORM;
        case D3DFMT_Q16W16V16U16:  return D3DX_PIXEL_FORMAT_U16V16W16Q16_SNORM;
        case D3DFMT_R8G8_B8G8:     return D3DX_PIXEL_FORMAT_R8G8_B8G8_UNORM;
        case D3DFMT_G8R8_G8B8:     return D3DX_PIXEL_FORMAT_G8R8_G8B8_UNORM;
        case D3DFMT_UYVY:          return D3DX_PIXEL_FORMAT_UYVY;
        case D3DFMT_YUY2:          return D3DX_PIXEL_FORMAT_YUY2;
        default:
            FIXME("No d3dx_pixel_format_id for D3DFORMAT %s.\n", debugstr_fourcc(format));
            return D3DX_PIXEL_FORMAT_COUNT;
    }
}

enum d3dx_resource_type d3dx_resource_type_from_d3dresourcetype(D3DRESOURCETYPE type)
{
    switch (type)
    {
        case D3DRTYPE_TEXTURE:       return D3DX_RESOURCE_TYPE_TEXTURE_2D;
        case D3DRTYPE_VOLUMETEXTURE: return D3DX_RESOURCE_TYPE_TEXTURE_3D;
        case D3DRTYPE_CUBETEXTURE:   return D3DX_RESOURCE_TYPE_CUBE_TEXTURE;
        default:
            FIXME("No d3dx_resource_type for D3DRESOURCETYPE %d.\n", type);
            return D3DX_RESOURCE_TYPE_COUNT;
    }
}

static D3DRESOURCETYPE d3dresourcetype_from_d3dx_resource_type(enum d3dx_resource_type type)
{
    switch (type)
    {
        case D3DX_RESOURCE_TYPE_TEXTURE_2D:   return D3DRTYPE_TEXTURE;
        case D3DX_RESOURCE_TYPE_TEXTURE_3D:   return D3DRTYPE_VOLUMETEXTURE;
        case D3DX_RESOURCE_TYPE_CUBE_TEXTURE: return D3DRTYPE_CUBETEXTURE;
        default:
            FIXME("No D3DRESOURCETYPE for d3dx_resource_type %d.\n", type);
            return D3DRTYPE_FORCE_DWORD;
    }
}

enum d3dx_image_file_format d3dx_image_file_format_from_d3dximage_fileformat(D3DXIMAGE_FILEFORMAT iff)
{
    switch (iff)
    {
        case D3DXIFF_BMP: return D3DX_IMAGE_FILE_FORMAT_BMP;
        case D3DXIFF_JPG: return D3DX_IMAGE_FILE_FORMAT_JPG;
        case D3DXIFF_TGA: return D3DX_IMAGE_FILE_FORMAT_TGA;
        case D3DXIFF_PNG: return D3DX_IMAGE_FILE_FORMAT_PNG;
        case D3DXIFF_DDS: return D3DX_IMAGE_FILE_FORMAT_DDS;
        case D3DXIFF_PPM: return D3DX_IMAGE_FILE_FORMAT_PPM;
        case D3DXIFF_DIB: return D3DX_IMAGE_FILE_FORMAT_DIB;
        case D3DXIFF_HDR: return D3DX_IMAGE_FILE_FORMAT_HDR;
        case D3DXIFF_PFM: return D3DX_IMAGE_FILE_FORMAT_PFM;
        default:
            FIXME("No d3dx_image_file_format for D3DXIMAGE_FILEFORMAT %d.\n", iff);
            return D3DX_IMAGE_FILE_FORMAT_FORCE_DWORD;
    }
}

D3DXIMAGE_FILEFORMAT d3dximage_fileformat_from_d3dx_image_file_format(enum d3dx_image_file_format iff)
{
    switch (iff)
    {
        case D3DX_IMAGE_FILE_FORMAT_BMP: return D3DXIFF_BMP;
        case D3DX_IMAGE_FILE_FORMAT_JPG: return D3DXIFF_JPG;
        case D3DX_IMAGE_FILE_FORMAT_TGA: return D3DXIFF_TGA;
        case D3DX_IMAGE_FILE_FORMAT_PNG: return D3DXIFF_PNG;
        case D3DX_IMAGE_FILE_FORMAT_DDS: return D3DXIFF_DDS;
        case D3DX_IMAGE_FILE_FORMAT_PPM: return D3DXIFF_PPM;
        case D3DX_IMAGE_FILE_FORMAT_DIB: return D3DXIFF_DIB;
        case D3DX_IMAGE_FILE_FORMAT_HDR: return D3DXIFF_HDR;
        case D3DX_IMAGE_FILE_FORMAT_PFM: return D3DXIFF_PFM;
        default:
            FIXME("No D3DXIMAGE_FILEFORMAT for d3dx_image_file_format %d.\n", iff);
            return D3DXIFF_FORCE_DWORD;
    }
}

BOOL d3dximage_info_from_d3dx_image(D3DXIMAGE_INFO *info, struct d3dx_image *image)
{
    D3DXIMAGE_FILEFORMAT iff = d3dximage_fileformat_from_d3dx_image_file_format(image->image_file_format);
    D3DRESOURCETYPE rtype = d3dresourcetype_from_d3dx_resource_type(image->resource_type);

    if (rtype == D3DRTYPE_FORCE_DWORD || iff == D3DXIFF_FORCE_DWORD)
        return FALSE;

    switch (image->format)
    {
        case D3DX_PIXEL_FORMAT_P1_UINT:
        case D3DX_PIXEL_FORMAT_P2_UINT:
        case D3DX_PIXEL_FORMAT_P4_UINT:
            info->Format = D3DFMT_P8;
            break;

        case D3DX_PIXEL_FORMAT_R16G16B16_UNORM:
            info->Format = D3DFMT_A16B16G16R16;
            break;

        case D3DX_PIXEL_FORMAT_B8G8R8_UNORM:
            if (iff == D3DXIFF_PNG || iff == D3DXIFF_JPG)
                info->Format = D3DFMT_X8R8G8B8;
            else
                info->Format = d3dformat_from_d3dx_pixel_format_id(image->format);
            break;

        default:
        {
            D3DFORMAT fmt = d3dformat_from_d3dx_pixel_format_id(image->format);

            if (fmt == D3DFMT_UNKNOWN)
                return FALSE;
            info->Format = fmt;
            break;
        }
    }
    info->ImageFileFormat = iff;
    info->Width = image->size.width;
    info->Height = image->size.height;
    info->Depth = image->size.depth;
    info->MipLevels = image->mip_levels;
    info->ResourceType = rtype;
    return TRUE;
}

static void d3dx9_buffer_destroy(struct d3dx_buffer *d3dx_buffer)
{
    ID3DXBuffer *buffer_iface = (ID3DXBuffer *)d3dx_buffer->buffer_iface;

    if (buffer_iface)
        ID3DXBuffer_Release(buffer_iface);
    d3dx_buffer->buffer_iface = d3dx_buffer->buffer_data = NULL;
}

static HRESULT d3dx9_buffer_create(unsigned int size, struct d3dx_buffer *buffer)
{
    ID3DXBuffer *buffer_iface;
    HRESULT hr;

    hr = D3DXCreateBuffer(size, &buffer_iface);
    if (FAILED(hr))
        return hr;

    buffer->buffer_iface = buffer_iface;
    buffer->buffer_data = ID3DXBuffer_GetBufferPointer(buffer_iface);
    return S_OK;
}

static const struct d3dx_buffer_wrapper d3dx9_buffer_wrapper =
{
    d3dx9_buffer_create,
    d3dx9_buffer_destroy,
};

HRESULT d3dx9_save_pixels_to_memory(struct d3dx_pixels *src_pixels, const struct pixel_format_desc *src_fmt_desc,
        D3DXIMAGE_FILEFORMAT file_format, ID3DXBuffer **dst_buffer)
{
    struct d3dx_buffer buffer;
    HRESULT hr;

    *dst_buffer = NULL;
    hr = d3dx_save_pixels_to_memory(src_pixels, src_fmt_desc,
            d3dx_image_file_format_from_d3dximage_fileformat(file_format), &d3dx9_buffer_wrapper, &buffer);
    if (SUCCEEDED(hr))
        *dst_buffer = (ID3DXBuffer *)buffer.buffer_iface;

    return hr;
}

/************************************************************
 * map_view_of_file
 *
 * Loads a file into buffer and stores the number of read bytes in length.
 *
 * PARAMS
 *   filename [I] name of the file to be loaded
 *   buffer   [O] pointer to destination buffer
 *   length   [O] size of the obtained data
 *
 * RETURNS
 *   Success: D3D_OK
 *   Failure:
 *     see error codes for CreateFileW, GetFileSize, CreateFileMapping and MapViewOfFile
 *
 * NOTES
 *   The caller must UnmapViewOfFile when it doesn't need the data anymore
 *
 */
HRESULT map_view_of_file(const WCHAR *filename, void **buffer, DWORD *length)
{
    HANDLE hfile, hmapping = NULL;

    hfile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if(hfile == INVALID_HANDLE_VALUE) goto error;

    *length = GetFileSize(hfile, NULL);
    if(*length == INVALID_FILE_SIZE) goto error;

    hmapping = CreateFileMappingW(hfile, NULL, PAGE_READONLY, 0, 0, NULL);
    if(!hmapping) goto error;

    *buffer = MapViewOfFile(hmapping, FILE_MAP_READ, 0, 0, 0);
    if(*buffer == NULL) goto error;

    CloseHandle(hmapping);
    CloseHandle(hfile);

    return S_OK;

error:
    if (hmapping)
        CloseHandle(hmapping);
    if (hfile != INVALID_HANDLE_VALUE)
        CloseHandle(hfile);
    return HRESULT_FROM_WIN32(GetLastError());
}

/************************************************************
 * load_resource_into_memory
 *
 * Loads a resource into buffer and stores the number of
 * read bytes in length.
 *
 * PARAMS
 *   module  [I] handle to the module
 *   resinfo [I] handle to the resource's information block
 *   buffer  [O] pointer to destination buffer
 *   length  [O] size of the obtained data
 *
 * RETURNS
 *   Success: D3D_OK
 *   Failure:
 *     See error codes for SizeofResource, LoadResource and LockResource
 *
 * NOTES
 *   The memory doesn't need to be freed by the caller manually
 *
 */
HRESULT load_resource_into_memory(HMODULE module, HRSRC resinfo, void **buffer, DWORD *length)
{
    HGLOBAL resource;

    *length = SizeofResource(module, resinfo);
    if(*length == 0) return HRESULT_FROM_WIN32(GetLastError());

    resource = LoadResource(module, resinfo);
    if( !resource ) return HRESULT_FROM_WIN32(GetLastError());

    *buffer = LockResource(resource);
    if(*buffer == NULL) return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

HRESULT write_buffer_to_file(const WCHAR *dst_filename, ID3DXBuffer *buffer)
{
    HRESULT hr = S_OK;
    void *buffer_pointer;
    DWORD buffer_size;
    DWORD bytes_written;
    HANDLE file = CreateFileW(dst_filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    buffer_pointer = ID3DXBuffer_GetBufferPointer(buffer);
    buffer_size = ID3DXBuffer_GetBufferSize(buffer);

    if (!WriteFile(file, buffer_pointer, buffer_size, &bytes_written, NULL))
        hr = HRESULT_FROM_WIN32(GetLastError());

    CloseHandle(file);
    return hr;
}

/************************************************************
 * get_format_info
 *
 * Returns information about the specified format.
 * If the format is unsupported, it's filled with the D3DX_PIXEL_FORMAT_COUNT desc.
 *
 * PARAMS
 *   format [I] format whose description is queried
 *
 */
const struct pixel_format_desc *get_format_info(D3DFORMAT format)
{
    const struct pixel_format_desc *fmt_desc = get_d3dx_pixel_format_info(d3dx_pixel_format_id_from_d3dformat(format));

    if (is_unknown_format(fmt_desc))
        FIXME("Unknown format %s.\n", debugstr_fourcc(format));
    return fmt_desc;
}

const struct pixel_format_desc *get_format_info_idx(int idx)
{
    return idx < D3DX_PIXEL_FORMAT_COUNT ? get_d3dx_pixel_format_info(idx) : NULL;
}

#define WINE_D3DX_TO_STR(x) case x: return #x

const char *debug_d3dxparameter_class(D3DXPARAMETER_CLASS c)
{
    switch (c)
    {
        WINE_D3DX_TO_STR(D3DXPC_SCALAR);
        WINE_D3DX_TO_STR(D3DXPC_VECTOR);
        WINE_D3DX_TO_STR(D3DXPC_MATRIX_ROWS);
        WINE_D3DX_TO_STR(D3DXPC_MATRIX_COLUMNS);
        WINE_D3DX_TO_STR(D3DXPC_OBJECT);
        WINE_D3DX_TO_STR(D3DXPC_STRUCT);
        default:
            FIXME("Unrecognized D3DXPARAMETER_CLASS %#x.\n", c);
            return "unrecognized";
    }
}

const char *debug_d3dxparameter_type(D3DXPARAMETER_TYPE t)
{
    switch (t)
    {
        WINE_D3DX_TO_STR(D3DXPT_VOID);
        WINE_D3DX_TO_STR(D3DXPT_BOOL);
        WINE_D3DX_TO_STR(D3DXPT_INT);
        WINE_D3DX_TO_STR(D3DXPT_FLOAT);
        WINE_D3DX_TO_STR(D3DXPT_STRING);
        WINE_D3DX_TO_STR(D3DXPT_TEXTURE);
        WINE_D3DX_TO_STR(D3DXPT_TEXTURE1D);
        WINE_D3DX_TO_STR(D3DXPT_TEXTURE2D);
        WINE_D3DX_TO_STR(D3DXPT_TEXTURE3D);
        WINE_D3DX_TO_STR(D3DXPT_TEXTURECUBE);
        WINE_D3DX_TO_STR(D3DXPT_SAMPLER);
        WINE_D3DX_TO_STR(D3DXPT_SAMPLER1D);
        WINE_D3DX_TO_STR(D3DXPT_SAMPLER2D);
        WINE_D3DX_TO_STR(D3DXPT_SAMPLER3D);
        WINE_D3DX_TO_STR(D3DXPT_SAMPLERCUBE);
        WINE_D3DX_TO_STR(D3DXPT_PIXELSHADER);
        WINE_D3DX_TO_STR(D3DXPT_VERTEXSHADER);
        WINE_D3DX_TO_STR(D3DXPT_PIXELFRAGMENT);
        WINE_D3DX_TO_STR(D3DXPT_VERTEXFRAGMENT);
        WINE_D3DX_TO_STR(D3DXPT_UNSUPPORTED);
        default:
            FIXME("Unrecognized D3DXPARAMETER_TYP %#x.\n", t);
            return "unrecognized";
    }
}

const char *debug_d3dxparameter_registerset(D3DXREGISTER_SET r)
{
    switch (r)
    {
        WINE_D3DX_TO_STR(D3DXRS_BOOL);
        WINE_D3DX_TO_STR(D3DXRS_INT4);
        WINE_D3DX_TO_STR(D3DXRS_FLOAT4);
        WINE_D3DX_TO_STR(D3DXRS_SAMPLER);
        default:
            FIXME("Unrecognized D3DXREGISTER_SET %#x.\n", r);
            return "unrecognized";
    }
}

#undef WINE_D3DX_TO_STR

/***********************************************************************
 * D3DXDebugMute
 * Returns always FALSE for us.
 */
BOOL WINAPI D3DXDebugMute(BOOL mute)
{
    return FALSE;
}

/***********************************************************************
 * D3DXGetDriverLevel.
 * Returns always 900 (DX 9) for us
 */
UINT WINAPI D3DXGetDriverLevel(struct IDirect3DDevice9 *device)
{
    return 900;
}
