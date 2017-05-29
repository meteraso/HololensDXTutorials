﻿
#include "pch.h"
#include "hlDeviceResources.h"
#include "hlDirectXHelper.h"

#include <Collection.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace D2D1;
using namespace Microsoft::WRL;
using namespace Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial;

// Constructor for DeviceResources.
hlDX::DeviceResources::DeviceResources()
{
    CreateDeviceIndependentResources();
}

// Configures resources that don't depend on the Direct3D device.
void hlDX::DeviceResources::CreateDeviceIndependentResources()
{
    // Initialize Direct2D resources.
    D2D1_FACTORY_OPTIONS options {};

#if defined(_DEBUG)
    // If the project is in a debug build, enable Direct2D debugging via SDK Layers.
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    // Initialize the Direct2D Factory.
    hlDX::ThrowIfFailed(
        D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory2),
            &options,
            &m_d2dFactory
            )
        );

    // Initialize the DirectWrite Factory.
    hlDX::ThrowIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory2),
            &m_dwriteFactory
            )
        );

    // Initialize the Windows Imaging Component (WIC) Factory.
    hlDX::ThrowIfFailed(
        CoCreateInstance(
            CLSID_WICImagingFactory2,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_wicFactory)
            )
        );
}

void hlDX::DeviceResources::SetHolographicSpace(HolographicSpace^ holographicSpace)
{
    // Cache the holographic space. Used to re-initalize during device-lost scenarios.
    m_holographicSpace = holographicSpace;
	
    InitializeUsingHolographicSpace();
}

void hlDX::DeviceResources::InitializeUsingHolographicSpace()
{
    // The holographic space might need to determine which adapter supports
    // holograms, in which case it will specify a non-zero PrimaryAdapterId.
    LUID id =
    {
        m_holographicSpace->PrimaryAdapterId.LowPart,
        m_holographicSpace->PrimaryAdapterId.HighPart
    };

    // When a primary adapter ID is given to the app, the app should find
    // the corresponding DXGI adapter and use it to create Direct3D devices
    // and device contexts. Otherwise, there is no restriction on the DXGI
    // adapter the app can use.
    if ((id.HighPart != 0) && (id.LowPart != 0))
    {
        UINT createFlags = 0;
#ifdef DEBUG
        if (hlDX::SdkLayersAvailable())
        {
            createFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        // Create the DXGI factory.
        ComPtr<IDXGIFactory1> dxgiFactory;
        hlDX::ThrowIfFailed(
            CreateDXGIFactory2(
                createFlags,
                IID_PPV_ARGS(&dxgiFactory)
                )
            );
        ComPtr<IDXGIFactory4> dxgiFactory4;
        hlDX::ThrowIfFailed(dxgiFactory.As(&dxgiFactory4));

        // Retrieve the adapter specified by the holographic space.
        hlDX::ThrowIfFailed(
            dxgiFactory4->EnumAdapterByLuid(
                id,
                IID_PPV_ARGS(&m_dxgiAdapter)
                )
            );
    }
    else
    {
        m_dxgiAdapter.Reset();
    }

    CreateDeviceResources();

    m_holographicSpace->SetDirect3D11Device(m_d3dInteropDevice);
}

// Configures the Direct3D device, and stores handles to it and the device context.
void hlDX::DeviceResources::CreateDeviceResources()
{
    // This flag adds support for surfaces with a different color channel ordering
    // than the API default. It is required for compatibility with Direct2D.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
    if (hlDX::SdkLayersAvailable())
    {
        // If the project is in a debug build, enable debugging via SDK Layers with this flag.
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif

    // This array defines the set of DirectX hardware feature levels this app will support.
    // Note the ordering should be preserved.
    // Note that HoloLens supports feature level 11.1. The HoloLens emulator is also capable
    // of running on graphics cards starting with feature level 10.0.
    D3D_FEATURE_LEVEL featureLevels [] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    // Create the Direct3D 11 API device object and a corresponding context.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    const HRESULT hr = D3D11CreateDevice(
        m_dxgiAdapter.Get(),        // Either nullptr, or the primary adapter determined by Windows Holographic.
        D3D_DRIVER_TYPE_HARDWARE,   // Create a device using the hardware graphics driver.
        0,                          // Should be 0 unless the driver is D3D_DRIVER_TYPE_SOFTWARE.
        creationFlags,              // Set debug and Direct2D compatibility flags.
        featureLevels,              // List of feature levels this app can support.
        ARRAYSIZE(featureLevels),   // Size of the list above.
        D3D11_SDK_VERSION,          // Always set this to D3D11_SDK_VERSION for Windows Store apps.
        &device,                    // Returns the Direct3D device created.
        &m_d3dFeatureLevel,         // Returns feature level of device created.
        &context                    // Returns the device immediate context.
        );

    if (FAILED(hr))
    {
        // If the initialization fails, fall back to the WARP device.
        // For more information on WARP, see:
        // http://go.microsoft.com/fwlink/?LinkId=286690
        hlDX::ThrowIfFailed(
            D3D11CreateDevice(
                nullptr,              // Use the default DXGI adapter for WARP.
                D3D_DRIVER_TYPE_WARP, // Create a WARP device instead of a hardware device.
                0,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &device,
                &m_d3dFeatureLevel,
                &context
                )
            );
    }

    // Store pointers to the Direct3D device and immediate context.
    hlDX::ThrowIfFailed(
        device.As(&m_d3dDevice)
        );

    hlDX::ThrowIfFailed(
        context.As(&m_d3dContext)
        );

    // Acquire the DXGI interface for the Direct3D device.
    ComPtr<IDXGIDevice3> dxgiDevice;
    hlDX::ThrowIfFailed(
        m_d3dDevice.As(&dxgiDevice)
        );

    // Wrap the native device using a WinRT interop object.
    m_d3dInteropDevice = CreateDirect3DDevice(dxgiDevice.Get());

    // Cache the DXGI adapter.
    // This is for the case of no preferred DXGI adapter, or fallback to WARP.
    ComPtr<IDXGIAdapter> dxgiAdapter;
    hlDX::ThrowIfFailed(
        dxgiDevice->GetAdapter(&dxgiAdapter)
        );
    hlDX::ThrowIfFailed(
        dxgiAdapter.As(&m_dxgiAdapter)
        );

    // Check for device support for the optional feature that allows setting the render target array index from the vertex shader stage.
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 options;
    m_d3dDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &options, sizeof(options));
    if (options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer)
    {
        m_supportsVprt = true;
    }
}

// Validates the back buffer for each HolographicCamera and recreates
// resources for back buffers that have changed.
// Locks the set of holographic camera resources until the function exits.
void hlDX::DeviceResources::EnsureCameraResources(
    HolographicFrame^ frame,
    HolographicFramePrediction^ prediction)
{
   /* UseHolographicCameraResources<void>([this, frame, prediction](std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap)
    {
        for (HolographicCameraPose^ pose : prediction->CameraPoses)
        {
            HolographicCameraRenderingParameters^ renderingParameters = frame->GetRenderingParameters(pose);
            CameraResources* pCameraResources = cameraResourceMap[pose->HolographicCamera->Id].get();
			
            pCameraResources->CreateResourcesForBackBuffer(this, renderingParameters);
			
        }
    });*/
}

// Prepares to allocate resources and adds resource views for a camera.
// Locks the set of holographic camera resources until the function exits.
void hlDX::DeviceResources::AddHolographicCamera(HolographicCamera^ camera)
{
	
    /*UseHolographicCameraResources<void>([this, camera](std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap)
    {
        cameraResourceMap[camera->Id] = std::make_unique<CameraResources>(camera);
    });*/
}

// Deallocates resources for a camera and removes the camera from the set.
// Locks the set of holographic camera resources until the function exits.
void hlDX::DeviceResources::RemoveHolographicCamera(HolographicCamera^ camera)
{
    /*UseHolographicCameraResources<void>([this, camera](std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap)
    {
        CameraResources* pCameraResources = cameraResourceMap[camera->Id].get();

        if (pCameraResources != nullptr)
        {
            pCameraResources->ReleaseResourcesForBackBuffer(this);
            cameraResourceMap.erase(camera->Id);
        }
    });*/
}

// Recreate all device resources and set them back to the current state.
// Locks the set of holographic camera resources until the function exits.
void hlDX::DeviceResources::HandleDeviceLost()
{
    if (m_deviceNotify != nullptr)
    {
        m_deviceNotify->OnDeviceLost();
    }

    /*UseHolographicCameraResources<void>([this](std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap)
    {
        for (auto& pair : cameraResourceMap)
        {
            CameraResources* pCameraResources = pair.second.get();
            pCameraResources->ReleaseResourcesForBackBuffer(this);
        }
    });*/

    InitializeUsingHolographicSpace();

    if (m_deviceNotify != nullptr)
    {
        m_deviceNotify->OnDeviceRestored();
    }
}

// Register our DeviceNotify to be informed on device lost and creation.
void hlDX::DeviceResources::RegisterDeviceNotify(hlDX::IDeviceNotify* deviceNotify)
{
    m_deviceNotify = deviceNotify;
}

// Call this method when the app suspends. It provides a hint to the driver that the app
// is entering an idle state and that temporary buffers can be reclaimed for use by other apps.
void hlDX::DeviceResources::Trim()
{
    m_d3dContext->ClearState();

    ComPtr<IDXGIDevice3> dxgiDevice;
    hlDX::ThrowIfFailed(m_d3dDevice.As(&dxgiDevice));
    dxgiDevice->Trim();
}

// Present the contents of the swap chain to the screen.
// Locks the set of holographic camera resources until the function exits.
void hlDX::DeviceResources::Present(HolographicFrame^ frame)
{

    // By default, this API waits for the frame to finish before it returns.
    // Holographic apps should wait for the previous frame to finish before
    // starting work on a new frame. This allows for better results from
    // holographic frame predictions.
    HolographicFramePresentResult presentResult = frame->PresentUsingCurrentPrediction();

 //   HolographicFramePrediction^ prediction = frame->CurrentPrediction;
	//UseHolographicCameraResources<void>([this, prediction, frame](std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap)
	//{
	//	for (auto cameraPose : prediction->CameraPoses)
	//	{

	//		// The projection transform for each frame is provided by the HolographicCameraPose.
	//		HolographicStereoTransform cameraProjectionTransform = cameraPose->ProjectionTransform;
	//		auto  locator = SpatialLocator::GetDefault();

	//		auto coordinates = locator->CreateStationaryFrameOfReferenceAtCurrentLocation()->CoordinateSystem;

	//		// Get a container object with the view and projection matrices for the given
	//		// pose in the given coordinate system.
	//		Platform::IBox<HolographicStereoTransform>^ viewTransformContainer = cameraPose->TryGetViewTransform(coordinates);

	//		// If TryGetViewTransform returns a null pointer, that means the pose and coordinate
	//		// system cannot be understood relative to one another; content cannot be rendered
	//		// in this coordinate system for the duration of the current frame.
	//		// This usually means that positional tracking is not active for the current frame, in
	//		// which case it is possible to use a SpatialLocatorAttachedFrameOfReference to render
	//		// content that is not world-locked instead.
	//		bool viewTransformAcquired = viewTransformContainer != nullptr;
	//		if (viewTransformAcquired)
	//		{
	//			// Otherwise, the set of view transforms can be retrieved.
	//			HolographicStereoTransform viewCoordinateSystemTransform = viewTransformContainer->Value;

	//			// Update the view matrices. Holographic cameras (such as Microsoft HoloLens) are
	//			// constantly moving relative to the world. The view matrices need to be updated
	//			// every frame.
	//			m_view =
	//				XMLoadFloat4x4(&viewCoordinateSystemTransform.Left); 
	//			m_projection =  XMLoadFloat4x4(&cameraProjectionTransform.Left);
	//		
	//		}

 //           // This represents the device-based resources for a HolographicCamera.
 //           hlDX::CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

 //           // Discard the contents of the render target.
 //           // This is a valid operation only when the existing contents will be
 //           // entirely overwritten. If dirty or scroll rects are used, this call
 //           // should be removed.
 //           m_d3dContext->DiscardView(pCameraResources->GetBackBufferRenderTargetView());

 //           // Discard the contents of the depth stencil.
 //           m_d3dContext->DiscardView(pCameraResources->GetDepthStencilView());
 //       }
 //   });

    // The PresentUsingCurrentPrediction API will detect when the graphics device
    // changes or becomes invalid. When this happens, it is considered a Direct3D
    // device lost scenario.
    if (presentResult == HolographicFramePresentResult::DeviceRemoved)
    {
        // The Direct3D device, context, and resources should be recreated.
        HandleDeviceLost();
    }
}