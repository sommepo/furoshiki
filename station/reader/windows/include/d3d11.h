// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reader fork: MinGW's d3d11.h, with the helper types of Microsoft's own that it lacks and
// DuckStation's Direct3D 11 code uses (CD3D11_BOX, _RECT, _VIEWPORT, _DEPTH_STENCIL_VIEW_DESC),
// as Microsoft's header defines them. Only the Windows build made with MinGW
// (tools/reader/build-windows.sh) puts this folder on its include path; d3d11_helpers.cpp
// beside it has the constructors that MinGW declares and does not define.
#pragma once
#include_next <d3d11.h>

#if defined(__cplusplus) && !defined(D3D11_NO_HELPERS) && !defined(READER_D3D11_HELPERS)
#define READER_D3D11_HELPERS
struct CD3D11_RECT : public D3D11_RECT
{
  CD3D11_RECT() = default;
  explicit CD3D11_RECT(const D3D11_RECT& o) : D3D11_RECT(o) {}
  explicit CD3D11_RECT(LONG Left, LONG Top, LONG Right, LONG Bottom)
  {
    left = Left;
    top = Top;
    right = Right;
    bottom = Bottom;
  }
};

struct CD3D11_BOX : public D3D11_BOX
{
  CD3D11_BOX() = default;
  explicit CD3D11_BOX(const D3D11_BOX& o) : D3D11_BOX(o) {}
  explicit CD3D11_BOX(LONG Left, LONG Top, LONG Front, LONG Right, LONG Bottom, LONG Back)
  {
    left = static_cast<UINT>(Left);
    top = static_cast<UINT>(Top);
    front = static_cast<UINT>(Front);
    right = static_cast<UINT>(Right);
    bottom = static_cast<UINT>(Bottom);
    back = static_cast<UINT>(Back);
  }
};

struct CD3D11_VIEWPORT : public D3D11_VIEWPORT
{
  CD3D11_VIEWPORT() = default;
  explicit CD3D11_VIEWPORT(const D3D11_VIEWPORT& o) : D3D11_VIEWPORT(o) {}
  explicit CD3D11_VIEWPORT(FLOAT topLeftX, FLOAT topLeftY, FLOAT width, FLOAT height,
                           FLOAT minDepth = 0.0f, FLOAT maxDepth = 1.0f)
  {
    TopLeftX = topLeftX;
    TopLeftY = topLeftY;
    Width = width;
    Height = height;
    MinDepth = minDepth;
    MaxDepth = maxDepth;
  }
};

struct CD3D11_DEPTH_STENCIL_VIEW_DESC : public D3D11_DEPTH_STENCIL_VIEW_DESC
{
  CD3D11_DEPTH_STENCIL_VIEW_DESC() = default;
  explicit CD3D11_DEPTH_STENCIL_VIEW_DESC(const D3D11_DEPTH_STENCIL_VIEW_DESC& o)
    : D3D11_DEPTH_STENCIL_VIEW_DESC(o)
  {
  }
  explicit CD3D11_DEPTH_STENCIL_VIEW_DESC(D3D11_DSV_DIMENSION viewDimension,
                                          DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, UINT mipSlice = 0,
                                          UINT firstArraySlice = 0, UINT arraySize = static_cast<UINT>(-1),
                                          UINT flags = 0)
  {
    Format = format;
    ViewDimension = viewDimension;
    Flags = flags;
    switch (viewDimension)
    {
      case D3D11_DSV_DIMENSION_TEXTURE1D:
        Texture1D.MipSlice = mipSlice;
        break;
      case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
        Texture1DArray.MipSlice = mipSlice;
        Texture1DArray.FirstArraySlice = firstArraySlice;
        Texture1DArray.ArraySize = arraySize;
        break;
      case D3D11_DSV_DIMENSION_TEXTURE2D:
        Texture2D.MipSlice = mipSlice;
        break;
      case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        Texture2DArray.MipSlice = mipSlice;
        Texture2DArray.FirstArraySlice = firstArraySlice;
        Texture2DArray.ArraySize = arraySize;
        break;
      case D3D11_DSV_DIMENSION_TEXTURE2DMS:
        break;
      case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
        Texture2DMSArray.FirstArraySlice = firstArraySlice;
        Texture2DMSArray.ArraySize = arraySize;
        break;
      default:
        break;
    }
  }
};
#endif
