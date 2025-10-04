/*
 * Copyright 2016 Andrey Gusev
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
 */

#include "d3dx11.h"
#include "d3dcompiler.h"
#include "dxhelpers.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dx);

/*
 * These are mappings from legacy DDS header formats to DXGI formats. Some
 * don't map to a DXGI_FORMAT at all, and some only map to the default format.
 */
static DXGI_FORMAT dxgi_format_from_legacy_dds_d3dx_pixel_format_id(enum d3dx_pixel_format_id format)
{
    switch (format)
    {
        /*
         * Some of these formats do have DXGI_FORMAT equivalents, but get
         * mapped to DXGI_FORMAT_R8G8B8A8_UNORM instead.
         */
        case D3DX_PIXEL_FORMAT_P8_UINT:                  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_P8_UINT_A8_UNORM:         return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_R8G8B8A8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_R8G8B8X8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B8G8R8_UNORM:             return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B8G8R8A8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B8G8R8X8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B5G6R5_UNORM:             return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B5G5R5X1_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B5G5R5A1_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B2G3R3_UNORM:             return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B2G3R3A8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B4G4R4A4_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B4G4R4X4_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_L8A8_UNORM:               return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_L4A4_UNORM:               return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_L8_UNORM:                 return DXGI_FORMAT_R8G8B8A8_UNORM;

        /* B10G10R10A2 doesn't exist in DXGI, both map to R10G10B10A2. */
        case D3DX_PIXEL_FORMAT_B10G10R10A2_UNORM:
        case D3DX_PIXEL_FORMAT_R10G10B10A2_UNORM:        return DXGI_FORMAT_R10G10B10A2_UNORM;

        case D3DX_PIXEL_FORMAT_U16V16W16Q16_SNORM:       return DXGI_FORMAT_R16G16B16A16_SNORM;
        case D3DX_PIXEL_FORMAT_L16_UNORM:                return DXGI_FORMAT_R16G16B16A16_UNORM;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_UNORM:       return DXGI_FORMAT_R16G16B16A16_UNORM;
        case D3DX_PIXEL_FORMAT_R16G16_UNORM:             return DXGI_FORMAT_R16G16_UNORM;
        case D3DX_PIXEL_FORMAT_A8_UNORM:                 return DXGI_FORMAT_A8_UNORM;
        case D3DX_PIXEL_FORMAT_R16_FLOAT:                return DXGI_FORMAT_R16_FLOAT;
        case D3DX_PIXEL_FORMAT_R16G16_FLOAT:             return DXGI_FORMAT_R16G16_FLOAT;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_FLOAT:       return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case D3DX_PIXEL_FORMAT_R32_FLOAT:                return DXGI_FORMAT_R32_FLOAT;
        case D3DX_PIXEL_FORMAT_R32G32_FLOAT:             return DXGI_FORMAT_R32G32_FLOAT;
        case D3DX_PIXEL_FORMAT_R32G32B32A32_FLOAT:       return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case D3DX_PIXEL_FORMAT_G8R8_G8B8_UNORM:          return DXGI_FORMAT_G8R8_G8B8_UNORM;
        case D3DX_PIXEL_FORMAT_R8G8_B8G8_UNORM:          return DXGI_FORMAT_R8G8_B8G8_UNORM;

        case D3DX_PIXEL_FORMAT_DXT1_UNORM:               return DXGI_FORMAT_BC1_UNORM;
        case D3DX_PIXEL_FORMAT_DXT2_UNORM:               return DXGI_FORMAT_BC2_UNORM;
        case D3DX_PIXEL_FORMAT_DXT3_UNORM:               return DXGI_FORMAT_BC2_UNORM;
        case D3DX_PIXEL_FORMAT_DXT4_UNORM:               return DXGI_FORMAT_BC3_UNORM;
        case D3DX_PIXEL_FORMAT_DXT5_UNORM:               return DXGI_FORMAT_BC3_UNORM;
        case D3DX_PIXEL_FORMAT_BC4_UNORM:                return DXGI_FORMAT_BC4_UNORM;
        case D3DX_PIXEL_FORMAT_BC4_SNORM:                return DXGI_FORMAT_BC4_SNORM;
        case D3DX_PIXEL_FORMAT_BC5_UNORM:                return DXGI_FORMAT_BC5_UNORM;
        case D3DX_PIXEL_FORMAT_BC5_SNORM:                return DXGI_FORMAT_BC5_SNORM;

        /* These formats are known and explicitly unsupported on d3dx10+. */
        case D3DX_PIXEL_FORMAT_U8V8W8Q8_SNORM:
        case D3DX_PIXEL_FORMAT_U8V8_SNORM:
        case D3DX_PIXEL_FORMAT_U8V8_SNORM_Cx:
        case D3DX_PIXEL_FORMAT_U16V16_SNORM:
        case D3DX_PIXEL_FORMAT_U8V8_SNORM_L8X8_UNORM:
        case D3DX_PIXEL_FORMAT_U10V10W10_SNORM_A2_UNORM:
        case D3DX_PIXEL_FORMAT_UYVY:
        case D3DX_PIXEL_FORMAT_YUY2:
            return DXGI_FORMAT_UNKNOWN;

        default:
            FIXME("Unknown d3dx_pixel_format_id %#x.\n", format);
            return DXGI_FORMAT_UNKNOWN;
    }
}

static DXGI_FORMAT dxgi_format_from_d3dx_pixel_format_id(enum d3dx_pixel_format_id format)
{
    switch (format)
    {
        case D3DX_PIXEL_FORMAT_R8G8B8A8_UNORM:          return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B8G8R8A8_UNORM:          return DXGI_FORMAT_B8G8R8A8_UNORM;
        case D3DX_PIXEL_FORMAT_B8G8R8X8_UNORM:          return DXGI_FORMAT_B8G8R8X8_UNORM;
        case D3DX_PIXEL_FORMAT_R10G10B10A2_UNORM:       return DXGI_FORMAT_R10G10B10A2_UNORM;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_UNORM:      return DXGI_FORMAT_R16G16B16A16_UNORM;
        case D3DX_PIXEL_FORMAT_R8_UNORM:                return DXGI_FORMAT_R8_UNORM;
        case D3DX_PIXEL_FORMAT_R8G8_UNORM:              return DXGI_FORMAT_R8G8_UNORM;
        case D3DX_PIXEL_FORMAT_R16_UNORM:               return DXGI_FORMAT_R16_UNORM;
        case D3DX_PIXEL_FORMAT_R16G16_UNORM:            return DXGI_FORMAT_R16G16_UNORM;
        case D3DX_PIXEL_FORMAT_A8_UNORM:                return DXGI_FORMAT_A8_UNORM;
        case D3DX_PIXEL_FORMAT_R16_FLOAT:               return DXGI_FORMAT_R16_FLOAT;
        case D3DX_PIXEL_FORMAT_R16G16_FLOAT:            return DXGI_FORMAT_R16G16_FLOAT;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_FLOAT:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case D3DX_PIXEL_FORMAT_R32_FLOAT:               return DXGI_FORMAT_R32_FLOAT;
        case D3DX_PIXEL_FORMAT_R32G32_FLOAT:            return DXGI_FORMAT_R32G32_FLOAT;
        case D3DX_PIXEL_FORMAT_R32G32B32_FLOAT:         return DXGI_FORMAT_R32G32B32_FLOAT;
        case D3DX_PIXEL_FORMAT_R32G32B32A32_FLOAT:      return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case D3DX_PIXEL_FORMAT_G8R8_G8B8_UNORM:         return DXGI_FORMAT_G8R8_G8B8_UNORM;
        case D3DX_PIXEL_FORMAT_R8G8_B8G8_UNORM:         return DXGI_FORMAT_R8G8_B8G8_UNORM;
        case D3DX_PIXEL_FORMAT_BC1_UNORM:               return DXGI_FORMAT_BC1_UNORM;
        case D3DX_PIXEL_FORMAT_BC2_UNORM:               return DXGI_FORMAT_BC2_UNORM;
        case D3DX_PIXEL_FORMAT_BC3_UNORM:               return DXGI_FORMAT_BC3_UNORM;
        case D3DX_PIXEL_FORMAT_BC4_UNORM:               return DXGI_FORMAT_BC4_UNORM;
        case D3DX_PIXEL_FORMAT_BC4_SNORM:               return DXGI_FORMAT_BC4_SNORM;
        case D3DX_PIXEL_FORMAT_BC5_UNORM:               return DXGI_FORMAT_BC5_UNORM;
        case D3DX_PIXEL_FORMAT_BC5_SNORM:               return DXGI_FORMAT_BC5_SNORM;
        case D3DX_PIXEL_FORMAT_R16G16B16A16_SNORM:      return DXGI_FORMAT_R16G16B16A16_SNORM;
        case D3DX_PIXEL_FORMAT_R8G8B8A8_SNORM:          return DXGI_FORMAT_R8G8B8A8_SNORM;
        case D3DX_PIXEL_FORMAT_R8G8_SNORM:              return DXGI_FORMAT_R8G8_SNORM;
        case D3DX_PIXEL_FORMAT_R16G16_SNORM:            return DXGI_FORMAT_R16G16_SNORM;

        /*
         * These have DXGI_FORMAT equivalents, but are explicitly unsupported on
         * d3dx10+.
         */
        case D3DX_PIXEL_FORMAT_B5G6R5_UNORM:
        case D3DX_PIXEL_FORMAT_B5G5R5A1_UNORM:
        case D3DX_PIXEL_FORMAT_B4G4R4A4_UNORM:
            return DXGI_FORMAT_UNKNOWN;

        default:
            FIXME("Unhandled d3dx_pixel_format_id %#x.\n", format);
            return DXGI_FORMAT_UNKNOWN;
    }
}

static D3DX11_IMAGE_FILE_FORMAT d3dx11_image_file_format_from_d3dx_image_file_format(enum d3dx_image_file_format iff)
{
    switch (iff)
    {
        case D3DX_IMAGE_FILE_FORMAT_BMP: return D3DX11_IFF_BMP;
        case D3DX_IMAGE_FILE_FORMAT_JPG: return D3DX11_IFF_JPG;
        case D3DX_IMAGE_FILE_FORMAT_PNG: return D3DX11_IFF_PNG;
        case D3DX_IMAGE_FILE_FORMAT_DDS: return D3DX11_IFF_DDS;
        case D3DX_IMAGE_FILE_FORMAT_TIFF: return D3DX11_IFF_TIFF;
        case D3DX_IMAGE_FILE_FORMAT_GIF: return D3DX11_IFF_GIF;
        case D3DX_IMAGE_FILE_FORMAT_WMP: return D3DX11_IFF_WMP;
        case D3DX_IMAGE_FILE_FORMAT_DDS_DXT10: return D3DX11_IFF_DDS;
        default:
            FIXME("No D3DX11_IMAGE_FILE_FORMAT for d3dx_image_file_format %d.\n", iff);
            return D3DX11_IFF_FORCE_DWORD;
    }
}

static HRESULT d3dx11_image_info_from_d3dx_image(D3DX11_IMAGE_INFO *info, struct d3dx_image *image)
{
    D3DX11_IMAGE_FILE_FORMAT iff = d3dx11_image_file_format_from_d3dx_image_file_format(image->image_file_format);
    DXGI_FORMAT format;

    memset(info, 0, sizeof(*info));
    switch (image->image_file_format)
    {
        case D3DX_IMAGE_FILE_FORMAT_DDS_DXT10:
            format = dxgi_format_from_d3dx_pixel_format_id(image->format);
            break;

        case D3DX_IMAGE_FILE_FORMAT_DDS:
            format = dxgi_format_from_legacy_dds_d3dx_pixel_format_id(image->format);
            break;

        default:
            if (iff == D3DX11_IFF_FORCE_DWORD)
                return E_FAIL;

            /* All other image file formats use the default format. */
            format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
    }

    if (format == DXGI_FORMAT_UNKNOWN)
    {
        WARN("Tried to load file with unsupported pixel format %#x.\n", image->format);
        return E_FAIL;
    }

    switch (image->resource_type)
    {
        case D3DX_RESOURCE_TYPE_TEXTURE_2D:
            info->ResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
            break;

        case D3DX_RESOURCE_TYPE_CUBE_TEXTURE:
            info->ResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
            info->MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
            break;

        case D3DX_RESOURCE_TYPE_TEXTURE_3D:
            info->ResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
            break;

        default:
            ERR("Unhandled resource type %d.\n", image->resource_type);
            return E_FAIL;
    }

    info->ImageFileFormat = iff;
    info->Width = image->size.width;
    info->Height = image->size.height;
    info->Depth = image->size.depth;
    info->ArraySize = image->layer_count;
    info->MipLevels = image->mip_levels;
    info->Format = format;
    return S_OK;
}

HRESULT get_image_info(const void *data, SIZE_T size, D3DX11_IMAGE_INFO *img_info)
{
    struct d3dx_image image;
    HRESULT hr;

    if (!data || !size)
        return E_FAIL;

    hr = d3dx_image_init(data, size, &image, 0, D3DX_IMAGE_INFO_ONLY | D3DX_IMAGE_SUPPORT_DXT10);
    if (SUCCEEDED(hr))
        hr = d3dx11_image_info_from_d3dx_image(img_info, &image);

    if (hr != S_OK)
    {
        WARN("Invalid or unsupported image file.\n");
        return E_FAIL;
    }
    return S_OK;
}

HRESULT WINAPI D3DX11CreateShaderResourceViewFromMemory(ID3D11Device *device, const void *data,
        SIZE_T data_size, D3DX11_IMAGE_LOAD_INFO *load_info, ID3DX11ThreadPump *pump,
        ID3D11ShaderResourceView **view, HRESULT *hresult)
{
    FIXME("device %p, data %p, data_size %Iu, load_info %p, pump %p, view %p, hresult %p stub!\n",
            device, data, data_size, load_info, pump, view, hresult);

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11CreateTextureFromFileA(ID3D11Device *device, const char *filename,
        D3DX11_IMAGE_LOAD_INFO *load_info, ID3DX11ThreadPump *pump, ID3D11Resource **texture,
        HRESULT *hresult)
{
    FIXME("device %p, filename %s, load_info %p, pump %p, texture %p, hresult %p stub.\n",
            device, debugstr_a(filename), load_info, pump, texture, hresult);

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11CreateTextureFromFileW(ID3D11Device *device, const WCHAR *filename,
        D3DX11_IMAGE_LOAD_INFO *load_info, ID3DX11ThreadPump *pump, ID3D11Resource **texture,
        HRESULT *hresult)
{
    FIXME("device %p, filename %s, load_info %p, pump %p, texture %p, hresult %p stub.\n",
            device, debugstr_w(filename), load_info, pump, texture, hresult);

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11CreateTextureFromMemory(ID3D11Device *device, const void *data,
        SIZE_T data_size, D3DX11_IMAGE_LOAD_INFO *load_info, ID3DX11ThreadPump *pump,
        ID3D11Resource **texture, HRESULT *hresult)
{
    FIXME("device %p, data %p, data_size %Iu, load_info %p, pump %p, texture %p, hresult %p stub.\n",
            device, data, data_size, load_info, pump, texture, hresult);

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11SaveTextureToFileW(ID3D11DeviceContext *context, ID3D11Resource *texture,
        D3DX11_IMAGE_FILE_FORMAT format, const WCHAR *filename)
{
    FIXME("context %p, texture %p, format %u, filename %s stub!\n",
            context, texture, format, debugstr_w(filename));

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11SaveTextureToFileA(ID3D11DeviceContext *context, ID3D11Resource *texture,
        D3DX11_IMAGE_FILE_FORMAT format, const char *filename)
{
    FIXME("context %p, texture %p, format %u, filename %s stub!\n",
            context, texture, format, debugstr_a(filename));

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11SaveTextureToMemory(ID3D11DeviceContext *context, ID3D11Resource *texture,
        D3DX11_IMAGE_FILE_FORMAT format, ID3D10Blob **buffer, UINT flags)
{
    FIXME("context %p, texture %p, format %u, buffer %p, flags %#x stub!\n",
            context, texture, format, buffer, flags);

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11LoadTextureFromTexture(ID3D11DeviceContext *context, ID3D11Resource *src_texture,
        D3DX11_TEXTURE_LOAD_INFO *info, ID3D11Resource *dst_texture)
{
    FIXME("context %p, src_texture %p, info %p, dst_texture %p stub!\n",
            context, src_texture, info, dst_texture);

    return E_NOTIMPL;
}

HRESULT WINAPI D3DX11GetImageInfoFromMemory(const void *src_data, SIZE_T src_data_size, ID3DX11ThreadPump *pump,
        D3DX11_IMAGE_INFO *img_info, HRESULT *hresult)
{
    HRESULT hr;

    TRACE("src_data %p, src_data_size %Iu, pump %p, img_info %p, hresult %p.\n", src_data, src_data_size, pump,
            img_info, hresult);

    if (!src_data)
        return E_FAIL;
    if (pump)
        FIXME("D3DX11 thread pump is currently unimplemented.\n");

    hr = get_image_info(src_data, src_data_size, img_info);
    if (hresult)
        *hresult = hr;
    return hr;
}
