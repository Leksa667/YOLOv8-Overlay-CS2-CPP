// Created by Leksa667
#include "screen_capture.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

cv::Mat captureScreen(int x, int y, int width, int height)
{
    HDC hScreen  = GetDC(NULL);
    HDC hMemDC   = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, width, height);
    HGDIOBJ hOld = SelectObject(hMemDC, hBmp);

    BitBlt(hMemDC, 0, 0, width, height, hScreen, x, y, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = width;
    bi.biHeight      = -height;
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;
    bi.biCompression = BI_RGB;

    cv::Mat frame(height, width, CV_8UC3);
    GetDIBits(hMemDC, hBmp, 0, static_cast<UINT>(height),
              frame.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hMemDC, hOld);
    DeleteObject(hBmp);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreen);

    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    return frame;
}

struct DXGICapture::Impl
{
    ComPtr<ID3D11Device>           device;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<IDXGIOutputDuplication> dupl;
    ComPtr<ID3D11Texture2D>        staging;
    cv::Mat                        bgra_copy;
    cv::Mat                        rgb;
    int  staging_width  = 0;
    int  staging_height = 0;
    int  width      = 0;
    int  height     = 0;
    int  monitor_id = 0;
    bool valid      = false;

    bool Init(int idx)
    {
        monitor_id = idx;
        valid = false;
        device.Reset(); ctx.Reset(); dupl.Reset(); staging.Reset();
        staging_width = 0; staging_height = 0;

        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &device, &fl, &ctx);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice>  dxgi_dev;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIOutput>  out0;
        ComPtr<IDXGIOutput1> out1;

        if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice),  &dxgi_dev))) return false;
        if (FAILED(dxgi_dev->GetParent(__uuidof(IDXGIAdapter),    &adapter)))  return false;
        if (FAILED(adapter->EnumOutputs(idx, &out0)))                          return false;
        if (FAILED(out0->QueryInterface(__uuidof(IDXGIOutput1),   &out1)))     return false;

        DXGI_OUTPUT_DESC desc{};
        out0->GetDesc(&desc);
        width  = desc.DesktopCoordinates.right  - desc.DesktopCoordinates.left;
        height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

        hr = out1->DuplicateOutput(device.Get(), &dupl);
        if (FAILED(hr)) return false;

        valid = true;
        return true;
    }

    bool EnsureStaging(int w, int h)
    {
        if (staging && staging_width == w && staging_height == h) return true;

        staging.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width              = static_cast<UINT>(w);
        td.Height             = static_cast<UINT>(h);
        td.MipLevels          = 1;
        td.ArraySize          = 1;
        td.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count   = 1;
        td.Usage              = D3D11_USAGE_STAGING;
        td.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

        HRESULT hr = device->CreateTexture2D(&td, nullptr, &staging);
        if (FAILED(hr)) {
            staging_width = 0;
            staging_height = 0;
            return false;
        }

        staging_width = w;
        staging_height = h;
        return true;
    }

    cv::Mat CaptureBgraRegion(int x, int y, int w, int h)
    {
        if (!valid || !dupl) return {};

        x = std::max(0, std::min(x, width - 1));
        y = std::max(0, std::min(y, height - 1));
        w = std::max(1, std::min(w, width - x));
        h = std::max(1, std::min(h, height - y));
        if (!EnsureStaging(w, h)) return {};

        IDXGIResource*         res  = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info{};

        HRESULT hr = dupl->AcquireNextFrame(0, &info, &res);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return {};

        if (hr == DXGI_ERROR_ACCESS_LOST ||
            hr == DXGI_ERROR_INVALID_CALL) {
            valid = false;
            return {};
        }
        if (FAILED(hr)) return {};

        ComPtr<ID3D11Texture2D> tex;
        hr = res->QueryInterface(__uuidof(ID3D11Texture2D), &tex);
        res->Release();
        if (FAILED(hr)) { dupl->ReleaseFrame(); return {}; }

        D3D11_BOX box{};
        box.left   = static_cast<UINT>(x);
        box.top    = static_cast<UINT>(y);
        box.right  = static_cast<UINT>(x + w);
        box.bottom = static_cast<UINT>(y + h);
        box.front  = 0;
        box.back   = 1;
        ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
        dupl->ReleaseFrame();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return {};

        cv::Mat bgra(h, w, CV_8UC4,
                     mapped.pData, static_cast<size_t>(mapped.RowPitch));
        bgra.copyTo(bgra_copy);

        ctx->Unmap(staging.Get(), 0);
        return bgra_copy;
    }

    cv::Mat CaptureBgra()
    {
        return CaptureBgraRegion(0, 0, width, height);
    }

    cv::Mat Capture()
    {
        cv::Mat bgra = CaptureBgra();
        if (bgra.empty()) return {};
        cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);
        return rgb;
    }
};

DXGICapture::DXGICapture()  = default;
DXGICapture::~DXGICapture() = default;

bool    DXGICapture::Init(int idx) { impl_ = std::make_unique<Impl>(); return impl_->Init(idx); }
cv::Mat DXGICapture::Capture()     { return impl_ ? impl_->Capture() : cv::Mat{}; }
cv::Mat DXGICapture::CaptureBgra() { return impl_ ? impl_->CaptureBgra() : cv::Mat{}; }
cv::Mat DXGICapture::CaptureBgraRegion(int x, int y, int width, int height)
{
    return impl_ ? impl_->CaptureBgraRegion(x, y, width, height) : cv::Mat{};
}
int  DXGICapture::Width()   const { return impl_ ? impl_->width  : 0; }
int  DXGICapture::Height()  const { return impl_ ? impl_->height : 0; }
bool DXGICapture::IsValid() const { return impl_ && impl_->valid; }
