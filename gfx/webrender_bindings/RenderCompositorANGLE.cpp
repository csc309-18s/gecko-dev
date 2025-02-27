/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorANGLE.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/layers/HelpersD3D11.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/widget/WinCompositorWidget.h"
#include "mozilla/WindowsVersion.h"

#include <d3d11.h>
#include <dxgi1_2.h>

namespace mozilla {
namespace wr {

/* static */ UniquePtr<RenderCompositor>
RenderCompositorANGLE::Create(RefPtr<widget::CompositorWidget>&& aWidget)
{
  UniquePtr<RenderCompositorANGLE> compositor = MakeUnique<RenderCompositorANGLE>(Move(aWidget));
  if (!compositor->Initialize()) {
    return nullptr;
  }
  return compositor;
}

RenderCompositorANGLE::RenderCompositorANGLE(RefPtr<widget::CompositorWidget>&& aWidget)
  : RenderCompositor(Move(aWidget))
  , mEGLConfig(nullptr)
  , mEGLSurface(nullptr)
{
}

RenderCompositorANGLE::~RenderCompositorANGLE()
{
  DestroyEGLSurface();
  MOZ_ASSERT(!mEGLSurface);
}

ID3D11Device*
RenderCompositorANGLE::GetDeviceOfEGLDisplay()
{
  const auto& egl = &gl::sEGLLibrary;

  // Fetch the D3D11 device.
  EGLDeviceEXT eglDevice = nullptr;
  egl->fQueryDisplayAttribEXT(egl->Display(), LOCAL_EGL_DEVICE_EXT, (EGLAttrib*)&eglDevice);
  MOZ_ASSERT(eglDevice);
  ID3D11Device* device = nullptr;
  egl->fQueryDeviceAttribEXT(eglDevice, LOCAL_EGL_D3D11_DEVICE_ANGLE, (EGLAttrib*)&device);
  if (!device) {
    gfxCriticalNote << "Failed to get D3D11Device from EGLDisplay";
    return nullptr;
  }
  return device;
}

bool
RenderCompositorANGLE::Initialize()
{
  const auto& egl = &gl::sEGLLibrary;

  nsCString discardFailureId;
  if (!egl->EnsureInitialized(/* forceAccel */ true, &discardFailureId)) {
    gfxCriticalNote << "Failed to load EGL library: " << discardFailureId.get();
    return false;
  }

  mDevice = GetDeviceOfEGLDisplay();

  if (!mDevice) {
    gfxCriticalNote << "[D3D11] failed to get compositor device.";
    return false;
  }

  mDevice->GetImmediateContext(getter_AddRefs(mCtx));
  if (!mCtx) {
    gfxCriticalNote << "[D3D11] failed to get immediate context.";
    return false;
  }

  HWND hwnd = mWidget->AsWindows()->GetHwnd();

  RefPtr<IDXGIDevice> dxgiDevice;
  mDevice->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));

  RefPtr<IDXGIFactory> dxgiFactory;
  {
    RefPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(getter_AddRefs(adapter));

    adapter->GetParent(IID_PPV_ARGS((IDXGIFactory**)getter_AddRefs(dxgiFactory)));
  }

  RefPtr<IDXGIFactory2> dxgiFactory2;
  if (SUCCEEDED(dxgiFactory->QueryInterface((IDXGIFactory2**)getter_AddRefs(dxgiFactory2))) &&
      dxgiFactory2 &&
      IsWin8OrLater())
  {
    RefPtr<IDXGISwapChain1> swapChain1;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // Do not use DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, since it makes HWND unreusable.
    //desc.BufferCount = 2;
    //desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.BufferCount = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.Flags = 0;

    HRESULT hr = dxgiFactory2->CreateSwapChainForHwnd(mDevice, hwnd, &desc,
                                                      nullptr, nullptr,
                                                      getter_AddRefs(swapChain1));
    if (SUCCEEDED(hr) && swapChain1) {
      DXGI_RGBA color = { 1.0f, 1.0f, 1.0f, 1.0f };
      swapChain1->SetBackgroundColor(&color);
      mSwapChain = swapChain1;
    }
  }

  if (!mSwapChain) {
    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = 0;
    swapDesc.BufferDesc.Height = 0;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 1;
    swapDesc.OutputWindow = hwnd;
    swapDesc.Windowed = TRUE;
    swapDesc.Flags = 0;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;

    HRESULT hr = dxgiFactory->CreateSwapChain(dxgiDevice, &swapDesc, getter_AddRefs(mSwapChain));
    if (FAILED(hr)) {
      gfxCriticalNote << "Could not create swap chain: " << gfx::hexa(hr);
      return false;
    }
  }

  // We need this because we don't want DXGI to respond to Alt+Enter.
  dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES);

  mSyncObject = layers::SyncObjectHost::CreateSyncObjectHost(mDevice);
  if (!mSyncObject->Init()) {
    // Some errors occur. Clear the mSyncObject here.
    // Then, there will be no texture synchronization.
    return false;
  }

  const auto flags = gl::CreateContextFlags::PREFER_ES3;

  // Create GLContext with dummy EGLSurface, the EGLSurface is not used.
  // Instread we override it with EGLSurface of SwapChain's back buffer.
  mGL = gl::GLContextProviderEGL::CreateHeadless(flags, &discardFailureId);
  if (!mGL || !mGL->IsANGLE()) {
    gfxCriticalNote << "Failed ANGLE GL context creation for WebRender: " << gfx::hexa(mGL.get());
    return false;
  }

  if (!mGL->MakeCurrent()) {
    gfxCriticalNote << "Failed GL context creation for WebRender: " << gfx::hexa(mGL.get());
    return false;
  }

  // Force enable alpha channel to make sure ANGLE use correct framebuffer formart
  if (!gl::CreateConfig(&mEGLConfig, /* bpp */ 32, /* enableDepthBuffer */ true)) {
    gfxCriticalNote << "Failed to create EGLConfig for WebRender";
  }
  MOZ_ASSERT(mEGLConfig);

  if (!ResizeBufferIfNeeded()) {
    return false;
  }

  return true;
}

bool
RenderCompositorANGLE::BeginFrame()
{
  if (!ResizeBufferIfNeeded()) {
    return false;
  }

  if (!mGL->MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

  if (mSyncObject) {
    // XXX: if the synchronization is failed, we should handle the device reset.
    mSyncObject->Synchronize();
  }
  return true;
}

void
RenderCompositorANGLE::EndFrame()
{
  InsertPresentWaitQuery();

  mSwapChain->Present(0, 0);

  // Note: this waits on the query we inserted in the previous frame,
  // not the one we just inserted now. Example:
  //   Insert query #1
  //   Present #1
  //   (first frame, no wait)
  //   Insert query #2
  //   Present #2
  //   Wait for query #1.
  //   Insert query #3
  //   Present #3
  //   Wait for query #2.
  //
  // This ensures we're done reading textures before swapping buffers.
  WaitForPreviousPresentQuery();
}

bool
RenderCompositorANGLE::ResizeBufferIfNeeded()
{
  MOZ_ASSERT(mSwapChain);

  LayoutDeviceIntSize size = mWidget->GetClientSize();

  // Set size to non negative.
  size.width = std::max(size.width, 0);
  size.height = std::max(size.height, 0);

  if (mBufferSize.isSome() && mBufferSize.ref() == size) {
    MOZ_ASSERT(mEGLSurface);
    return true;
  }

  HRESULT hr;
  RefPtr<ID3D11Texture2D> backBuf;

  // Release EGLSurface of back buffer before calling ResizeBuffers().
  DestroyEGLSurface();

  // Reset buffer size
  mBufferSize.reset();

  // Resize swap chain
  DXGI_SWAP_CHAIN_DESC desc;
  hr = mSwapChain->GetDesc(&desc);
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to read swap chain description: " << gfx::hexa(hr) << " Size : " << size;
    return false;
  }
  hr = mSwapChain->ResizeBuffers(desc.BufferCount, size.width, size.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to resize swap chain buffers: " << gfx::hexa(hr) << " Size : " << size;
    return false;
  }

  hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)getter_AddRefs(backBuf));
  if (hr == DXGI_ERROR_INVALID_CALL) {
    // This happens on some GPUs/drivers when there's a TDR.
    if (mDevice->GetDeviceRemovedReason() != S_OK) {
      gfxCriticalError() << "GetBuffer returned invalid call: " << gfx::hexa(hr) << " Size : " << size;
      return false;
    }
  }

  const auto& egl = &gl::sEGLLibrary;

  const EGLint pbuffer_attribs[]{
    LOCAL_EGL_WIDTH, size.width,
    LOCAL_EGL_HEIGHT, size.height,
    LOCAL_EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE, LOCAL_EGL_TRUE,
    LOCAL_EGL_NONE};

  const auto buffer = reinterpret_cast<EGLClientBuffer>(backBuf.get());

  const EGLSurface surface = egl->fCreatePbufferFromClientBuffer(
    egl->Display(), LOCAL_EGL_D3D_TEXTURE_ANGLE, buffer, mEGLConfig,
    pbuffer_attribs);

  EGLint err = egl->fGetError();
  if (err != LOCAL_EGL_SUCCESS) {
    gfxCriticalError() << "Failed to create Pbuffer of back buffer error: " << gfx::hexa(err) << " Size : " << size;
    return false;
  }

  gl::GLContextEGL::Cast(mGL)->SetEGLSurfaceOverride(surface);

  mEGLSurface = surface;
  mBufferSize = Some(size);

  return true;
}

void
RenderCompositorANGLE::DestroyEGLSurface()
{
  const auto& egl = &gl::sEGLLibrary;

  // Release EGLSurface of back buffer before calling ResizeBuffers().
  if (mEGLSurface) {
    gl::GLContextEGL::Cast(mGL)->SetEGLSurfaceOverride(EGL_NO_SURFACE);
    egl->fDestroySurface(egl->Display(), mEGLSurface);
    mEGLSurface = nullptr;
  }
}

void
RenderCompositorANGLE::Pause()
{
}

bool
RenderCompositorANGLE::Resume()
{
  return true;
}

LayoutDeviceIntSize
RenderCompositorANGLE::GetBufferSize()
{
  MOZ_ASSERT(mBufferSize.isSome());
  if (mBufferSize.isNothing()) {
    return LayoutDeviceIntSize();
  }
  return mBufferSize.ref();
}

void
RenderCompositorANGLE::InsertPresentWaitQuery()
{
  CD3D11_QUERY_DESC desc(D3D11_QUERY_EVENT);
  HRESULT hr = mDevice->CreateQuery(&desc, getter_AddRefs(mNextWaitForPresentQuery));
  if (FAILED(hr) || !mNextWaitForPresentQuery) {
    gfxWarning() << "Could not create D3D11_QUERY_EVENT: " << hexa(hr);
    return;
  }

  mCtx->End(mNextWaitForPresentQuery);
}

void
RenderCompositorANGLE::WaitForPreviousPresentQuery()
{
  if (mWaitForPresentQuery) {
    BOOL result;
    layers::WaitForGPUQuery(mDevice, mCtx, mWaitForPresentQuery, &result);
  }
  mWaitForPresentQuery = mNextWaitForPresentQuery.forget();
}


} // namespace wr
} // namespace mozilla
