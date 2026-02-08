#pragma once

// Link against necessary libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "dwmapi.lib")

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

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;

class ScreenCapturer {
private:
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    IDirect3DDevice winrtDevice{ nullptr };
    
    GraphicsCaptureItem item{ nullptr };
    Direct3D11CaptureFramePool framePool{ nullptr };
    GraphicsCaptureSession session{ nullptr };
    
    winrt::com_ptr<ID3D11Texture2D> latestTexture;
    winrt::com_ptr<ID3D11Texture2D> stagingTexture;
    std::mutex mutex;
    std::atomic<bool> hasNewFrame{ false };

    // Performance
    std::atomic<long long> lastCaptureDuration{ 0 };

    // WIC Factory
    winrt::com_ptr<IWICImagingFactory> wicFactory;
 
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

        // Copy - Needs locking
        std::lock_guard<std::mutex> lock(mutex);
        
        // Lazy initialization of our copy texture
        if (!latestTexture) {
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            desc.BindFlags = 0; // Not bound
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, latestTexture.put()));
        }

        // Copy GPU to GPU
        d3dContext->CopyResource(latestTexture.get(), texture.get());
        hasNewFrame = true;
    }

public:
    ScreenCapturer() {
        // Constructor is now empty to be safe for global instantiation
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
        d3dDevice = nullptr;
        d3dContext = nullptr;
        wicFactory = nullptr;
        hasNewFrame = false;
    }

    long long GetLastDuration() {
        return lastCaptureDuration;
    }

    bool CaptureScreen(std::vector<BYTE>& buffer) {
        if (!winrtDevice) Start(); // Lazy start if needed

        if (!hasNewFrame) return false;

        auto start = std::chrono::high_resolution_clock::now();

        winrt::com_ptr<ID3D11Texture2D> texToMap;
        D3D11_TEXTURE2D_DESC desc = {};

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!latestTexture) return false;
            
            latestTexture->GetDesc(&desc);

            // Ensure staging texture exists and matches
            if (!stagingTexture) {
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, stagingTexture.put()));
            }

            // Copy to staging
            d3dContext->CopyResource(stagingTexture.get(), latestTexture.get());
        }

        // Map staging texture
        D3D11_MAPPED_SUBRESOURCE mapInfo;
        check_hresult(d3dContext->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapInfo));

        // Calculate scaled size
        UINT originalWidth = desc.Width;
        UINT originalHeight = desc.Height;
        UINT scaledWidth = static_cast<UINT>(originalWidth * scaleFactor);
        UINT scaledHeight = static_cast<UINT>(originalHeight * scaleFactor);

        // Encode to JPEG or BMP using WIC
        // Create Stream
        winrt::com_ptr<IStream> stream;
        CreateStreamOnHGlobal(NULL, TRUE, stream.put());

        winrt::com_ptr<IWICBitmapEncoder> encoder;
        GUID format = useBmp ? GUID_ContainerFormatBmp : GUID_ContainerFormatJpeg;
        check_hresult(wicFactory->CreateEncoder(format, nullptr, encoder.put()));
        check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));

        winrt::com_ptr<IWICBitmapFrameEncode> frameEncode;
        winrt::com_ptr<IPropertyBag2> propertyBag;
        check_hresult(encoder->CreateNewFrame(frameEncode.put(), propertyBag.put()));

        if (!useBmp) {
            // Set Quality Option for JPEG
            PROPBAG2 option = { 0 };
            option.pstrName = (LPOLESTR)L"ImageQuality";
            VARIANT varValue;
            VariantInit(&varValue);
            varValue.vt = VT_R4;
            varValue.fltVal = jpegQuality;
            check_hresult(propertyBag->Write(1, &option, &varValue));
        }

        check_hresult(frameEncode->Initialize(propertyBag.get()));
        
        // Use scaled size for frame
        check_hresult(frameEncode->SetSize(scaledWidth, scaledHeight));
        
        // Create Bitmap from Memory
        winrt::com_ptr<IWICBitmap> wicBitmap;
        check_hresult(wicFactory->CreateBitmapFromMemory(
            originalWidth, originalHeight,
            GUID_WICPixelFormat32bppBGRA,
            mapInfo.RowPitch,
            originalHeight * mapInfo.RowPitch,
            (BYTE*)mapInfo.pData,
            wicBitmap.put()
        ));

        // Create Scaler
        winrt::com_ptr<IWICBitmapScaler> scaler;
        check_hresult(wicFactory->CreateBitmapScaler(scaler.put()));
        check_hresult(scaler->Initialize(
            wicBitmap.get(),
            scaledWidth, scaledHeight,
            WICBitmapInterpolationModeLinear
        ));

        // Request 24bppBGR for JPEG (standard) or 8bppGray
        WICPixelFormatGUID formatPixel = GUID_WICPixelFormat24bppBGR;
        if (!useBmp && useGray) {
            formatPixel = GUID_WICPixelFormat8bppGray;
        }

        check_hresult(frameEncode->SetPixelFormat(&formatPixel));
        
        // Write source from scaler
        check_hresult(frameEncode->WriteSource(scaler.get(), nullptr));
        check_hresult(frameEncode->Commit());
        check_hresult(encoder->Commit());

        d3dContext->Unmap(stagingTexture.get(), 0);

        // Get content
        HGLOBAL hMem = NULL;
        GetHGlobalFromStream(stream.get(), &hMem);
        void* pData = GlobalLock(hMem);
        SIZE_T size = GlobalSize(hMem);
        
        buffer.resize(size);
        memcpy(buffer.data(), pData, size);
        
        GlobalUnlock(hMem);

        auto end = std::chrono::high_resolution_clock::now();
        lastCaptureDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        return true;
    }
};

