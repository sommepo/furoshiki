// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reader fork: helper constructors that MinGW's d3d11.h declares and leaves undefined, which
// DuckStation's Direct3D 11 renderer uses (src/core/gpu_hw_d3d11.cpp), as Microsoft's header
// defines them. In the Windows build made with MinGW only (reader/CMakeLists.txt).
#include <d3d11.h>

CD3D11_SHADER_RESOURCE_VIEW_DESC::CD3D11_SHADER_RESOURCE_VIEW_DESC(ID3D11Texture2D* texture,
                                                                   D3D11_SRV_DIMENSION dim, DXGI_FORMAT format,
                                                                   UINT most_detailed_mip, UINT mip_levels,
                                                                   UINT first_slice, UINT array_size)
{
  ViewDimension = dim;
  if (format == DXGI_FORMAT_UNKNOWN ||
      (mip_levels == static_cast<UINT>(-1) && dim != D3D11_SRV_DIMENSION_TEXTURE2DMS &&
       dim != D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY) ||
      (array_size == static_cast<UINT>(-1) &&
       (dim == D3D11_SRV_DIMENSION_TEXTURE2DARRAY || dim == D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY ||
        dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)))
  {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (format == DXGI_FORMAT_UNKNOWN)
      format = desc.Format;
    if (mip_levels == static_cast<UINT>(-1))
      mip_levels = desc.MipLevels - most_detailed_mip;
    if (array_size == static_cast<UINT>(-1))
    {
      array_size = desc.ArraySize - first_slice;
      if (dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)
        array_size /= 6;
    }
  }
  Format = format;
  switch (dim)
  {
    case D3D11_SRV_DIMENSION_TEXTURE2D:
      Texture2D.MostDetailedMip = most_detailed_mip;
      Texture2D.MipLevels = mip_levels;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
      Texture2DArray.MostDetailedMip = most_detailed_mip;
      Texture2DArray.MipLevels = mip_levels;
      Texture2DArray.FirstArraySlice = first_slice;
      Texture2DArray.ArraySize = array_size;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS:
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
      Texture2DMSArray.FirstArraySlice = first_slice;
      Texture2DMSArray.ArraySize = array_size;
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
      TextureCube.MostDetailedMip = most_detailed_mip;
      TextureCube.MipLevels = mip_levels;
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
      TextureCubeArray.MostDetailedMip = most_detailed_mip;
      TextureCubeArray.MipLevels = mip_levels;
      TextureCubeArray.First2DArrayFace = first_slice;
      TextureCubeArray.NumCubes = array_size;
      break;
    default:
      break;
  }
}

CD3D11_RENDER_TARGET_VIEW_DESC::CD3D11_RENDER_TARGET_VIEW_DESC(ID3D11Texture2D* texture, D3D11_RTV_DIMENSION dim,
                                                               DXGI_FORMAT format, UINT mip_slice, UINT first_slice,
                                                               UINT array_size)
{
  ViewDimension = dim;
  if (format == DXGI_FORMAT_UNKNOWN ||
      (array_size == static_cast<UINT>(-1) &&
       (dim == D3D11_RTV_DIMENSION_TEXTURE2DARRAY || dim == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY)))
  {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (format == DXGI_FORMAT_UNKNOWN)
      format = desc.Format;
    if (array_size == static_cast<UINT>(-1))
      array_size = desc.ArraySize - first_slice;
  }
  Format = format;
  switch (dim)
  {
    case D3D11_RTV_DIMENSION_TEXTURE2D:
      Texture2D.MipSlice = mip_slice;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
      Texture2DArray.MipSlice = mip_slice;
      Texture2DArray.FirstArraySlice = first_slice;
      Texture2DArray.ArraySize = array_size;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS:
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
      Texture2DMSArray.FirstArraySlice = first_slice;
      Texture2DMSArray.ArraySize = array_size;
      break;
    default:
      break;
  }
}
