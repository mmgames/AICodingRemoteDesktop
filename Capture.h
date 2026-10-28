#pragma once

// Link against necessary libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.graphics.capture.h>

#include <vector>
#include <iostream>
#include <atomic>
#include <mutex>
#include <chrono>
#include <condition_variable>

#include "ScaleVS.h"
#include "ScalePS.h"
#include "ScaleGrayPS.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;

// High-performance, zero-copy memory stream writing directly to std::vector<BYTE>
class MemoryBufferStream : public IStream {
private:
    std::atomic<ULONG> m_refCount{ 1 };
    std::vector<BYTE>* m_buffer = nullptr;
    size_t m_pos = 0;

public:
    MemoryBufferStream() = default;

    void Attach(std::vector<BYTE>* buffer) {
        m_buffer = buffer;
        m_pos = 0;
        if (m_buffer) {
            m_buffer->clear();
        }
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IStream) || riid == __uuidof(ISequentialStream)) {
            *ppvObject = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return m_refCount.fetch_add(1) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        return m_refCount.fetch_sub(1) - 1;
    }

    // ISequentialStream
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        if (!m_buffer) return E_UNEXPECTED;
        if (m_pos >= m_buffer->size()) {
            if (pcbRead) *pcbRead = 0;
            return S_FALSE;
        }
        ULONG bytesToRead = (ULONG)min((size_t)cb, m_buffer->size() - m_pos);
        memcpy(pv, m_buffer->data() + m_pos, bytesToRead);
        m_pos += bytesToRead;
        if (pcbRead) *pcbRead = bytesToRead;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write(const void* pv, ULONG cb, ULONG* pcbWritten) override {
        if (!m_buffer) return E_UNEXPECTED;
        size_t newPos = m_pos + cb;
        if (newPos > m_buffer->size()) {
            m_buffer->resize(newPos);
        }
        memcpy(m_buffer->data() + m_pos, pv, cb);
        m_pos = newPos;
        if (pcbWritten) *pcbWritten = cb;
        return S_OK;
    }

    // IStream
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) override {
        if (!m_buffer) return E_UNEXPECTED;
        LONGLONG newPos = 0;
        switch (dwOrigin) {
        case STREAM_SEEK_SET:
            newPos = dlibMove.QuadPart;
            break;
        case STREAM_SEEK_CUR:
            newPos = (LONGLONG)m_pos + dlibMove.QuadPart;
            break;
        case STREAM_SEEK_END:
            newPos = (LONGLONG)m_buffer->size() + dlibMove.QuadPart;
            break;
        default:
            return STG_E_INVALIDFUNCTION;
        }
        if (newPos < 0) return STG_E_INVALIDFUNCTION;
        m_pos = (size_t)newPos;
        if (plibNewPosition) plibNewPosition->QuadPart = m_pos;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) override {
        if (!m_buffer) return E_UNEXPECTED;
        m_buffer->resize((size_t)libNewSize.QuadPart);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* pstm, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD grfStatFlag) override {
        if (!pstatstg) return E_POINTER;
        ZeroMemory(pstatstg, sizeof(STATSTG));
        pstatstg->type = STGTY_STREAM;
        pstatstg->cbSize.QuadPart = m_buffer ? m_buffer->size() : 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm) override { return E_NOTIMPL; }
};

class ScreenCapturer {
private:
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    IDirect3DDevice winrtDevice{ nullptr };
    
    GraphicsCaptureItem item{ nullptr };
    Direct3D11CaptureFramePool framePool{ nullptr };
    GraphicsCaptureSession session{ nullptr };
    
    winrt::com_ptr<ID3D11Texture2D> latestTexture;
    winrt::com_ptr<ID3D11ShaderResourceView> latestSrv;

    // GPU Scaling & Post-Processing
    winrt::com_ptr<ID3D11VertexShader> scaleVertexShader;
    winrt::com_ptr<ID3D11PixelShader> scalePixelShader;
    winrt::com_ptr<ID3D11PixelShader> scaleGrayPixelShader;
    winrt::com_ptr<ID3D11SamplerState> linearSampler;
    winrt::com_ptr<ID3D11Texture2D> scaledTexture;
    winrt::com_ptr<ID3D11RenderTargetView> scaledRtv;
    UINT currentScaledWidth = 0;
    UINT currentScaledHeight = 0;

    // Staging Texture (CPU Readback)
    winrt::com_ptr<ID3D11Texture2D> stagingTexture;
    UINT currentStagingWidth = 0;
    UINT currentStagingHeight = 0;

    std::mutex mutex;
    std::condition_variable frameCv;
    std::atomic<bool> hasNewFrame{ false };

    // Performance
    std::atomic<long long> lastCaptureDuration{ 0 };

    // WIC Factory & Stream
    winrt::com_ptr<IWICImagingFactory> wicFactory;
    MemoryBufferStream memStream;
 
    // Configuration
    float scaleFactor = 0.5f;
    float jpegQuality = 0.5f;
    bool useBmp = false;
    bool useGray = false;

    void InitD3D() {
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        check_hresult(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            d3dDevice.put(), nullptr, d3dContext.put()
        ));

        // Create WinRT wrapper
        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        d3dDevice.as(dxgiDevice);
        winrt::com_ptr<::IInspectable> inspectable;
        check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
        winrtDevice = inspectable.as<IDirect3DDevice>();

        // Init GPU Scaling Shaders and Sampler
        check_hresult(d3dDevice->CreateVertexShader(g_ScaleVS, sizeof(g_ScaleVS), nullptr, scaleVertexShader.put()));
        check_hresult(d3dDevice->CreatePixelShader(g_ScalePS, sizeof(g_ScalePS), nullptr, scalePixelShader.put()));
        check_hresult(d3dDevice->CreatePixelShader(g_ScaleGrayPS, sizeof(g_ScaleGrayPS), nullptr, scaleGrayPixelShader.put()));

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        check_hresult(d3dDevice->CreateSamplerState(&sampDesc, linearSampler.put()));
    }

    void InitWIC() {
        check_hresult(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory.put())
        ));
    }

    void StartCapture() {
        // Get Primary Monitor
        POINT pt{ 0, 0 };
        HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

        // Create Capture Item
        auto activationFactory = get_activation_factory<GraphicsCaptureItem>();
        auto interopFactory = activationFactory.as<IGraphicsCaptureItemInterop>();
        check_hresult(interopFactory->CreateForMonitor(hMonitor, guid_of<GraphicsCaptureItem>(), put_abi(item)));

        // Create Frame Pool
        framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            item.Size()
        );

        // Hook Frame Arrived
        framePool.FrameArrived({ this, &ScreenCapturer::OnFrameArrived });

        // Start Session
        session = framePool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(true);
        session.StartCapture();
    }

    void OnFrameArrived(Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
        auto frame = sender.TryGetNextFrame();
        if (!frame) return;

        auto surface = frame.Surface();
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(guid_of<ID3D11Texture2D>(), texture.put_void()));

        {
            std::lock_guard<std::mutex> lock(mutex);
            
            // Lazy initialization of our copy texture with SRV bind support
            if (!latestTexture) {
                D3D11_TEXTURE2D_DESC desc;
                texture->GetDesc(&desc);
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, latestTexture.put()));
                check_hresult(d3dDevice->CreateShaderResourceView(latestTexture.get(), nullptr, latestSrv.put()));
            }

            // Copy GPU to GPU
            d3dContext->CopyResource(latestTexture.get(), texture.get());
            hasNewFrame = true;
        }
        frameCv.notify_all();
    }

public:
    ScreenCapturer() {
        // Constructor is empty to be safe for global instantiation
    }

    ~ScreenCapturer() {
        Stop();
    }

    void SetConfiguration(float scale, float quality, bool bmp, bool gray) {
        scaleFactor = scale;
        jpegQuality = quality;
        useBmp = bmp;
        useGray = gray;
    }

    void Start() {
        if (winrtDevice) return; // Already started

        // Init WinRT, D3D, WIC
        InitD3D();
        InitWIC();
        StartCapture();
    }

    void Stop() {
        if (session) {
            session.Close();
            session = nullptr;
        }
        if (framePool) {
            framePool.Close();
            framePool = nullptr;
        }
        item = nullptr;
        winrtDevice = nullptr;
        latestTexture = nullptr;
        latestSrv = nullptr;
        scaledTexture = nullptr;
        scaledRtv = nullptr;
        stagingTexture = nullptr;
        scaleVertexShader = nullptr;
        scalePixelShader = nullptr;
        scaleGrayPixelShader = nullptr;
        linearSampler = nullptr;
        d3dDevice = nullptr;
        d3dContext = nullptr;
        wicFactory = nullptr;
        hasNewFrame = false;
        frameCv.notify_all();
    }

    long long GetLastDuration() {
        return lastCaptureDuration;
    }

    bool CaptureScreen(std::vector<BYTE>& buffer, int timeoutMs = 100) {
        if (!winrtDevice) Start();

        std::unique_lock<std::mutex> lock(mutex);
        if (!hasNewFrame) {
            frameCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
                return hasNewFrame.load();
            });
        }
        if (!hasNewFrame || !latestTexture) return false;

        hasNewFrame = false; // Mark frame as consumed

        auto start = std::chrono::high_resolution_clock::now();

        D3D11_TEXTURE2D_DESC desc = {};
        latestTexture->GetDesc(&desc);

        UINT originalWidth = desc.Width;
        UINT originalHeight = desc.Height;
        UINT targetWidth = static_cast<UINT>(originalWidth * scaleFactor);
        UINT targetHeight = static_cast<UINT>(originalHeight * scaleFactor);
        targetWidth &= ~1;  // Even dimensions
        targetHeight &= ~1;
        if (targetWidth < 2) targetWidth = 2;
        if (targetHeight < 2) targetHeight = 2;

        bool needGpuScale = (targetWidth != originalWidth || targetHeight != originalHeight || useGray);
        ID3D11Texture2D* sourceForStaging = nullptr;

        if (needGpuScale) {
            // Recreate scaled render target if size changed
            if (!scaledTexture || currentScaledWidth != targetWidth || currentScaledHeight != targetHeight) {
                D3D11_TEXTURE2D_DESC sDesc = desc;
                sDesc.Width = targetWidth;
                sDesc.Height = targetHeight;
                sDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
                sDesc.Usage = D3D11_USAGE_DEFAULT;
                sDesc.CPUAccessFlags = 0;
                sDesc.MiscFlags = 0;
                check_hresult(d3dDevice->CreateTexture2D(&sDesc, nullptr, scaledTexture.put()));
                check_hresult(d3dDevice->CreateRenderTargetView(scaledTexture.get(), nullptr, scaledRtv.put()));
                currentScaledWidth = targetWidth;
                currentScaledHeight = targetHeight;
            }

            // GPU Hardware Bilinear Blit
            ID3D11RenderTargetView* rtvList[] = { scaledRtv.get() };
            d3dContext->OMSetRenderTargets(1, rtvList, nullptr);

            D3D11_VIEWPORT vp = {};
            vp.Width = (float)targetWidth;
            vp.Height = (float)targetHeight;
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            d3dContext->RSSetViewports(1, &vp);

            d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            d3dContext->IASetInputLayout(nullptr);
            d3dContext->VSSetShader(scaleVertexShader.get(), nullptr, 0);
            d3dContext->PSSetShader(useGray ? scaleGrayPixelShader.get() : scalePixelShader.get(), nullptr, 0);

            ID3D11ShaderResourceView* srvList[] = { latestSrv.get() };
            d3dContext->PSSetShaderResources(0, 1, srvList);

            ID3D11SamplerState* sampList[] = { linearSampler.get() };
            d3dContext->PSSetSamplers(0, 1, sampList);

            d3dContext->Draw(3, 0);

            // Unbind to prevent resource hazard
            ID3D11ShaderResourceView* nullSrv[] = { nullptr };
            d3dContext->PSSetShaderResources(0, 1, nullSrv);
            ID3D11RenderTargetView* nullRtv[] = { nullptr };
            d3dContext->OMSetRenderTargets(1, nullRtv, nullptr);

            sourceForStaging = scaledTexture.get();
        } else {
            sourceForStaging = latestTexture.get();
        }

        // Ensure staging texture exists and matches target size
        if (!stagingTexture || currentStagingWidth != targetWidth || currentStagingHeight != targetHeight) {
            D3D11_TEXTURE2D_DESC stgDesc = desc;
            stgDesc.Width = targetWidth;
            stgDesc.Height = targetHeight;
            stgDesc.Usage = D3D11_USAGE_STAGING;
            stgDesc.BindFlags = 0;
            stgDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stgDesc.MiscFlags = 0;
            check_hresult(d3dDevice->CreateTexture2D(&stgDesc, nullptr, stagingTexture.put()));
            currentStagingWidth = targetWidth;
            currentStagingHeight = targetHeight;
        }

        // Copy scaled or full texture to staging
        d3dContext->CopyResource(stagingTexture.get(), sourceForStaging);

        // Map staging texture
        D3D11_MAPPED_SUBRESOURCE mapInfo;
        check_hresult(d3dContext->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapInfo));

        // Zero-copy encode directly into destination buffer
        memStream.Attach(&buffer);

        GUID format = useBmp ? GUID_ContainerFormatBmp : GUID_ContainerFormatJpeg;
        winrt::com_ptr<IWICBitmapEncoder> encoder;
        check_hresult(wicFactory->CreateEncoder(format, nullptr, encoder.put()));
        check_hresult(encoder->Initialize(&memStream, WICBitmapEncoderNoCache));

        winrt::com_ptr<IWICBitmapFrameEncode> frameEncode;
        winrt::com_ptr<IPropertyBag2> propertyBag;
        check_hresult(encoder->CreateNewFrame(frameEncode.put(), propertyBag.put()));

        if (!useBmp) {
            PROPBAG2 option = { 0 };
            option.pstrName = (LPOLESTR)L"ImageQuality";
            VARIANT varValue;
            VariantInit(&varValue);
            varValue.vt = VT_R4;
            varValue.fltVal = jpegQuality;
            check_hresult(propertyBag->Write(1, &option, &varValue));
        }

        check_hresult(frameEncode->Initialize(propertyBag.get()));
        check_hresult(frameEncode->SetSize(targetWidth, targetHeight));

        // Create lightweight WICBitmap wrapper pointing to mapped memory (No CPU Scaler!)
        winrt::com_ptr<IWICBitmap> wicBitmap;
        check_hresult(wicFactory->CreateBitmapFromMemory(
            targetWidth, targetHeight,
            GUID_WICPixelFormat32bppBGRA,
            mapInfo.RowPitch,
            targetHeight * mapInfo.RowPitch,
            (BYTE*)mapInfo.pData,
            wicBitmap.put()
        ));

        WICPixelFormatGUID formatPixel = GUID_WICPixelFormat24bppBGR;
        if (!useBmp && useGray) {
            formatPixel = GUID_WICPixelFormat8bppGray;
        }
        check_hresult(frameEncode->SetPixelFormat(&formatPixel));

        check_hresult(frameEncode->WriteSource(wicBitmap.get(), nullptr));
        check_hresult(frameEncode->Commit());
        check_hresult(encoder->Commit());

        d3dContext->Unmap(stagingTexture.get(), 0);

        auto end = std::chrono::high_resolution_clock::now();
        lastCaptureDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        return true;
    }
};
