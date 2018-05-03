// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#include <string>

#include "RenderDisplayContext.h"
#include "RenderOutput.h"

static inline std::string GenerateUniqueTextureName(const std::string &prefix, uint32 id)
{
	return prefix + "-" + std::to_string(id);
}

void CRenderDisplayContext::BeginRendering(bool isHighDynamicRangeEnabled)
{
	// Toggle HDR/LDR output selection
	if (m_bHDRRendering != isHighDynamicRangeEnabled)
	{
		m_bHDRRendering = isHighDynamicRangeEnabled;
		m_pRenderOutput->ChangeOutputResolution(
			m_pRenderOutput->GetOutputResolution()[0],
			m_pRenderOutput->GetOutputResolution()[1]
		);
	}
}

void CRenderDisplayContext::EndRendering()
{
	m_bHDRRendering = false;
}

bool CRenderDisplayContext::IsEditorDisplay() const 
{ 
	return m_uniqueId != 0 || gRenDev->IsEditorMode(); 
}

bool CRenderDisplayContext::IsNativeScalingEnabled() const 
{ 
	return GetRenderOutput() ? 
		GetDisplayResolution() != GetRenderOutput()->GetOutputResolution() : 
		false; 
}

void CRenderDisplayContext::SetDisplayResolutionAndRecreateTargets(uint32_t displayWidth, uint32_t displayHeight, const SRenderViewport& vp)
{
	CRY_ASSERT(displayWidth > 0 && displayHeight > 0);

	m_viewport = vp;

	if (m_DisplayWidth == displayWidth && m_DisplayHeight == displayHeight)
		return;

	m_DisplayWidth = displayWidth;
	m_DisplayHeight = displayHeight;
	if (m_DisplayWidth && m_DisplayHeight)
		m_aspectRatio = (float)m_DisplayWidth / (float)m_DisplayHeight;

	AllocateColorTarget();
	AllocateDepthTarget();
}
void CRenderDisplayContext::ChangeDisplayResolution(uint32_t displayWidth, uint32_t displayHeight, const SRenderViewport& vp)
{
	SetDisplayResolutionAndRecreateTargets(displayWidth, displayHeight, vp);

	m_pRenderOutput->InitializeDisplayContext();
	m_pRenderOutput->ReinspectDisplayContext();
}

void CRenderDisplayContext::AllocateColorTarget()
{
	const ETEX_Format eHDRFormat = CRenderer::CV_r_HDRTexFormat == 0 ? eTF_R11G11B10F : eTF_R16G16B16A16F;
	const ColorF clearValue = Clr_Empty;

	// NOTE: Actual device texture allocation happens just before rendering.
	const uint32 renderTargetFlags = FT_NOMIPS | FT_DONT_STREAM | FT_USAGE_RENDERTARGET;
	const std::string uniqueTexName = GenerateUniqueTextureName("$HDR-Overlay", m_uniqueId);

	m_pColorTarget = CTexture::GetOrCreateRenderTarget(uniqueTexName.c_str(), m_DisplayWidth, m_DisplayHeight, clearValue, eTT_2D, renderTargetFlags, eHDRFormat);
}

void CRenderDisplayContext::AllocateDepthTarget()
{
	m_pDepthTarget = nullptr;
	if (!NeedsDepthStencil())
		return;

	const ETEX_Format eZFormat =
		gRenDev->GetDepthBpp() == 32 ? eTF_D32FS8 :
		gRenDev->GetDepthBpp() == 24 ? eTF_D24S8 :
		gRenDev->GetDepthBpp() == 8 ? eTF_D16S8 : eTF_D16;

	const float  clearDepth = CRenderer::CV_r_ReverseDepth ? 0.f : 1.f;
	const uint   clearStencil = 1;
	const ColorF clearValues = ColorF(clearDepth, FLOAT(clearStencil), 0.f, 0.f);

	// Create the native resolution depth stencil buffer for overlay rendering if needed
	const uint32 renderTargetFlags = FT_NOMIPS | FT_DONT_STREAM | FT_USAGE_DEPTHSTENCIL;
	const std::string uniqueOverlayTexName = GenerateUniqueTextureName("$Z-Overlay", m_uniqueId);

	m_pDepthTarget = CTexture::GetOrCreateDepthStencil(uniqueOverlayTexName.c_str(), m_DisplayWidth, m_DisplayHeight, clearValues, eTT_2D, renderTargetFlags, eZFormat);
}

void CRenderDisplayContext::ReleaseResources()
{
	m_pRenderOutput->ReleaseResources();
	m_pDepthTarget = nullptr;
	m_pColorTarget = nullptr;
}


//////////////////////////////////////////////////////////////////////////

void CSwapChainBackedRenderDisplayContext::SetHWND(HWND hWnd)
{
	CRY_ASSERT(hWnd);
	m_hWnd = hWnd;

#if !CRY_RENDERER_GNM && !CRY_PLATFORM_ORBIS && !CRY_PLATFORM_DURANGO
	CreateOutput();
	SetSwapChain(CSwapChain::CreateSwapChain(GetWindowHandle(),
		m_pOutput,
		GetDisplayResolution().x,
		GetDisplayResolution().y,
		IsMainContext(),
		IsFullscreen()));
#endif

	auto w = m_DisplayWidth, h = m_DisplayHeight;
#if CRY_PLATFORM_WINDOWS
	if (TRUE == ::IsWindow((HWND)hWnd))
	{
		RECT rc;
		if (TRUE == GetClientRect((HWND)hWnd, &rc))
		{
			// On Windows force screen resolution to be a real pixel size of the client rect of the real window
			w = rc.right - rc.left;
			h = rc.bottom - rc.top;
		}
	}
#endif

	// Create the output
	ChangeOutputIfNecessary(m_fullscreen);
}

void CSwapChainBackedRenderDisplayContext::ShutDown()
{
	ReleaseResources();

#if (CRY_RENDERER_DIRECT3D >= 110) || (CRY_RENDERER_VULKAN >= 10)
	if (m_swapChain.GetSwapChain())
		m_swapChain.GetSwapChain()->SetFullscreenState(FALSE, 0);
#endif

#if CRY_PLATFORM_WINDOWS
	SAFE_RELEASE(m_pOutput);
#endif
}

void CSwapChainBackedRenderDisplayContext::ReleaseResources()
{
	if (gcpRendD3D.GetDeviceContext().IsValid())
		GetDeviceObjectFactory().GetCoreCommandList().GetGraphicsInterface()->ClearState(true);

	CRenderDisplayContext::ReleaseResources();

	m_pBackBufferPresented = nullptr;

	if (m_pBackBufferProxy)
	{
		m_pBackBufferProxy->RefDevTexture(nullptr);
		m_pBackBufferProxy = nullptr;
	}

	for (auto &t : m_backBuffersArray)
	{
		if (!t)
			continue;
		t->SetDevTexture(nullptr);
		t = nullptr;
	}

	m_bSwapProxy = true;
}

void CSwapChainBackedRenderDisplayContext::ReleaseBackBuffers()
{
	GetDeviceObjectFactory().GetCoreCommandList().GetGraphicsInterface()->ClearState(true);

	m_pBackBufferPresented = nullptr;

	if (m_pBackBufferProxy)
		m_pBackBufferProxy->RefDevTexture(nullptr);

	// Keep the CTextures for reuse, remove the device-resources and trigger dirty-handling
	for (auto &t : m_backBuffersArray)
	{
		if (t)
			t->SetDevTexture(nullptr);
	}

	m_bSwapProxy = true;
}

void CSwapChainBackedRenderDisplayContext::AllocateBackBuffers()
{
	unsigned indices = 1;
#if CRY_RENDERER_GNM
	CGnmSwapChain::SDesc desc = m_swapChain.GetSwapChain()->GnmGetDesc();
	indices = desc.numBuffers;
#elif (CRY_RENDERER_DIRECT3D >= 120) || (CRY_RENDERER_VULKAN >= 10)
	DXGI_SWAP_CHAIN_DESC scDesc;
	m_swapChain.GetSwapChain()->GetDesc(&scDesc);
	indices = scDesc.BufferCount;
#endif

	m_backBuffersArray.resize(indices, nullptr);
	m_pBackBufferPresented = nullptr;

	const uint32 renderTargetFlags = FT_NOMIPS | FT_DONT_STREAM | FT_USAGE_RENDERTARGET;
	const uint32 displayWidth = m_DisplayWidth;
	const uint32 displayHeight = m_DisplayHeight;
	const ETEX_Format displayFormat = DeviceFormats::ConvertToTexFormat(m_swapChain.GetSwapChainDesc().Format);

	char str[40];

	// ------------------------------------------------------------------------------
	if (!m_pBackBufferProxy)
	{
		sprintf(str, "$SwapChainBackBuffer %d:Current", m_uniqueId);

		m_pBackBufferProxy = CTexture::GetOrCreateTextureObject(str, displayWidth, displayHeight, 1, eTT_2D, renderTargetFlags, displayFormat);
		m_pBackBufferProxy->SRGBRead(DeviceFormats::ConvertToSRGB(DeviceFormats::ConvertFromTexFormat(displayFormat)) == m_swapChain.GetSwapChainDesc().Format);
	}

	if (!m_pBackBufferProxy->GetDevTexture())
	{
		m_pBackBufferProxy->Invalidate(displayWidth, displayHeight, displayFormat);
	}

	CRY_ASSERT(m_pBackBufferProxy->GetWidth() == displayWidth);
	CRY_ASSERT(m_pBackBufferProxy->GetHeight() == displayHeight);
	CRY_ASSERT(m_pBackBufferProxy->GetSrcFormat() == displayFormat);

	// ------------------------------------------------------------------------------
	for (int i = 0, n = indices; i < n; ++i)
	{
		// Prepare back-buffer texture(s)
		if (!m_backBuffersArray[i])
		{
			sprintf(str, "$SwapChainBackBuffer %d:Buffer %d", m_uniqueId, i);

			m_backBuffersArray[i] = CTexture::GetOrCreateTextureObject(str, displayWidth, displayHeight, 1, eTT_2D, renderTargetFlags, displayFormat);
			m_backBuffersArray[i]->SRGBRead(DeviceFormats::ConvertToSRGB(DeviceFormats::ConvertFromTexFormat(displayFormat)) == m_swapChain.GetSwapChainDesc().Format);
		}

		if (!m_backBuffersArray[i]->GetDevTexture())
		{
#if   CRY_RENDERER_GNM
			CGnmTexture* pBackBuffer = m_swapChain.GetSwapChain()->GnmGetBuffer(i);
#elif CRY_RENDERER_VULKAN
			D3DTexture* pBackBuffer = m_swapChain.GetSwapChain()->GetVKBuffer(i);
			assert(pBackBuffer != nullptr);
#else
			D3DTexture* pBackBuffer = nullptr;
			HRESULT hr = m_swapChain.GetSwapChain()->GetBuffer(i, __uuidof(D3DTexture), (void**)&pBackBuffer);
			assert(SUCCEEDED(hr) && pBackBuffer != nullptr);
#endif

			const auto &layout = m_backBuffersArray[i]->GetLayout();
			m_backBuffersArray[i]->Invalidate(displayWidth, displayHeight, displayFormat);
			m_backBuffersArray[i]->SetDevTexture(CDeviceTexture::Associate(layout, pBackBuffer));

			// Guarantee that the back-buffers are cleared on first use (e.g. Flash alpha-blends onto the back-buffer)
			// NOTE: GNM requires shaders to be initialized before issuing any draws/clears/copies/resolves. This is not yet the case here.
			// NOTE: Can only access current back-buffer on DX12
#if !(CRY_RENDERER_GNM || CRY_RENDERER_DIRECT3D >= 120)
			CClearSurfacePass::Execute(m_backBuffersArray[i], Clr_Transparent);
#endif
		}

		CRY_ASSERT(m_backBuffersArray[i]->GetWidth() == displayWidth);
		CRY_ASSERT(m_backBuffersArray[i]->GetHeight() == displayHeight);
		CRY_ASSERT(m_backBuffersArray[i]->GetSrcFormat() == displayFormat);
	}

	// Assign first back-buffer on initialization
	m_pBackBufferProxy->RefDevTexture(GetCurrentBackBuffer()->GetDevTexture());
	m_bSwapProxy = false;
}

void CSwapChainBackedRenderDisplayContext::CreateOutput()
{
#if CRY_PLATFORM_WINDOWS
	CRY_ASSERT(m_hWnd);
	HMONITOR hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);

	if (m_pOutput != nullptr)
	{
		unsigned long numRemainingReferences = m_pOutput->Release();
		CRY_ASSERT(numRemainingReferences == 0);
		m_pOutput = nullptr;
	}

	// Find the output that matches the monitor our window is currently on
	IDXGIAdapter1* pAdapter = gcpRendD3D->DevInfo().Adapter();
	IDXGIOutput* pOutput = nullptr;
	
	for (unsigned int i = 0; pAdapter->EnumOutputs(i, &pOutput) != DXGI_ERROR_NOT_FOUND && pOutput; ++i)
	{
		DXGI_OUTPUT_DESC outputDesc;
		if (SUCCEEDED(pOutput->GetDesc(&outputDesc)) && outputDesc.Monitor == hMonitor)
		{
			// Promote interfaces to the required level
			pOutput->QueryInterface(__uuidof(DXGIOutput), (void**)&m_pOutput);
			break;
		}
	}

	if (m_pOutput == nullptr)
	{
		if (SUCCEEDED(pAdapter->EnumOutputs(0, &pOutput)))
		{
			// Promote interfaces to the required level
			pOutput->QueryInterface(__uuidof(DXGIOutput), (void**)&m_pOutput);
		}
		else
		{
			CryWarning(VALIDATOR_MODULE_RENDERER, VALIDATOR_ERROR, "No display connected for rendering!");
			CRY_ASSERT(false);
		}
	}

	// Release the reference added by QueryInterface
	const unsigned long numRemainingReferences = m_pOutput->Release();
	CRY_ASSERT(numRemainingReferences == 1);
#endif
}

void CSwapChainBackedRenderDisplayContext::ChangeOutputIfNecessary(bool isFullscreen)
{
#if CRY_PLATFORM_WINDOWS
	HMONITOR hMonitor = MonitorFromWindow(reinterpret_cast<HWND>(m_hWnd), MONITOR_DEFAULTTONEAREST);

	bool isWindowOnExistingOutputMonitor = false;

	DXGI_OUTPUT_DESC outputDesc;
	if (m_pOutput != nullptr && SUCCEEDED(m_pOutput->GetDesc(&outputDesc)))
	{
		isWindowOnExistingOutputMonitor = outputDesc.Monitor == hMonitor;
	}

#if !CRY_PLATFORM_DURANGO && !CRY_RENDERER_GNM
	// Output will need to be recreated if we switched to fullscreen on a different monitor
	if (!isWindowOnExistingOutputMonitor && isFullscreen)
	{
		ReleaseBackBuffers();

		// Swap chain needs to be recreated with the new output in mind
		SetSwapChain(CSwapChain::CreateSwapChain(GetWindowHandle(), m_pOutput, GetDisplayResolution().x, GetDisplayResolution().y, IsMainContext(), isFullscreen));

		if (m_pOutput != nullptr)
		{
			unsigned long numRemainingReferences = m_pOutput->Release();
			CRY_ASSERT(numRemainingReferences == 0);
			m_pOutput = nullptr;
		}
	}
#endif

	CreateOutput();
#endif

#ifdef SUPPORT_DEVICE_INFO
	// Disable automatic DXGI alt + enter behavior
	gcpRendD3D->DevInfo().Factory()->MakeWindowAssociation(reinterpret_cast<HWND>(m_hWnd), DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
#endif

	if (m_fullscreen)
		SetFullscreenState(isFullscreen);
}
void CSwapChainBackedRenderDisplayContext::ChangeDisplayResolution(uint32_t displayWidth, uint32_t displayHeight, const SRenderViewport& vp)
{
	SetDisplayResolutionAndRecreateTargets(displayWidth, displayHeight, vp);

	if (m_swapChain.GetSwapChain())
	{
		ReleaseBackBuffers();

		m_swapChain.ResizeSwapChain(m_DisplayWidth, m_DisplayHeight, IsMainContext());
		// NOTE: Going fullscreen doesn't require freeing the back-buffers
		SetFullscreenState(m_fullscreen);

		AllocateBackBuffers();

		// Update swapchain dimensions
#if CRY_RENDERER_GNM
		CGnmSwapChain::SDesc desc = GetSwapChain().GetSwapChain()->GnmGetDesc();
		desc.width = displayWidth;
		desc.height = displayHeight;
		GetSwapChain().GetSwapChain()->GnmSetDesc(desc);
#elif CRY_PLATFORM_ORBIS
		GetSwapChain().GetSwapChain()->UpdateBackbufferDimensions(displayWidth, displayHeight);
#endif

		m_pRenderOutput->InitializeDisplayContext();
		m_pRenderOutput->ReinspectDisplayContext();
	}
}

void CSwapChainBackedRenderDisplayContext::SetFullscreenState(bool isFullscreen)
{
	if (!m_swapChain.GetSwapChain())
		return;

	m_fullscreen = isFullscreen;
#if (CRY_RENDERER_DIRECT3D >= 110) || (CRY_RENDERER_VULKAN >= 10)
	if (isFullscreen)
	{
		const int backbufferWidth = m_DisplayWidth;
		const int backbufferHeight = m_DisplayHeight;

		DXGI_SWAP_CHAIN_DESC scDesc;
		m_swapChain.GetSwapChain()->GetDesc(&scDesc);

#if (CRY_RENDERER_DIRECT3D >= 120)
		CRY_ASSERT_MESSAGE((scDesc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) == 0, "Fullscreen does not work with Waitable SwapChain");
#endif

		scDesc.BufferDesc.Width = backbufferWidth;
		scDesc.BufferDesc.Height = backbufferHeight;

#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
		UserOverrideDisplayProperties(scDesc.BufferDesc);
#endif

		HRESULT rr = m_swapChain.GetSwapChain()->ResizeTarget(&scDesc.BufferDesc);
		CRY_ASSERT(SUCCEEDED(rr));
	}

#if !CRY_PLATFORM_ORBIS && !CRY_PLATFORM_DURANGO
	m_swapChain.SetFullscreenState(isFullscreen, isFullscreen ? m_pOutput : nullptr);
#endif
#endif
}

Vec2_tpl<uint32_t> CSwapChainBackedRenderDisplayContext::FindClosestMatchingScreenResolution(const Vec2_tpl<uint32_t> &resolution) const
{
#if CRY_PLATFORM_WINDOWS
	if (m_swapChain.GetSwapChain() && m_pOutput)
	{
		DXGI_SWAP_CHAIN_DESC scDesc;
		m_swapChain.GetSwapChain()->GetDesc(&scDesc);

		scDesc.BufferDesc.Width = resolution.x;
		scDesc.BufferDesc.Height = resolution.y;

		DXGI_MODE_DESC match;
		if (SUCCEEDED(m_pOutput->FindClosestMatchingMode(&scDesc.BufferDesc, &match, gcpRendD3D.DevInfo().Device())))
		{
			return Vec2_tpl<uint32_t>{ match.Width, match.Height };
		}
	}
#endif

	return resolution;
}

#if CRY_PLATFORM_WINDOWS
void CSwapChainBackedRenderDisplayContext::EnforceFullscreenPreemption()
{
	if (IsMainContext() && CRenderer::CV_r_FullscreenPreemption && gcpRendD3D->IsFullscreen())
	{
		HRESULT hr = m_swapChain.Present(0, DXGI_PRESENT_TEST);
		if (hr == DXGI_STATUS_OCCLUDED)
		{
			if (::GetFocus() == reinterpret_cast<HWND>(m_hWnd))
				::BringWindowToTop(reinterpret_cast<HWND>(m_hWnd));
		}
	}
}
#endif // #if CRY_PLATFORM_WINDOWS

RectI CSwapChainBackedRenderDisplayContext::GetCurrentMonitorBounds() const
{
#ifdef CRY_PLATFORM_WINDOWS
	// When moving the window, update the preferred monitor dimensions so full-screen will pick the closest monitor
	HMONITOR hMonitor = MonitorFromWindow(reinterpret_cast<HWND>(m_hWnd), MONITOR_DEFAULTTONEAREST);

	MONITORINFO monitorInfo;
	monitorInfo.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(hMonitor, &monitorInfo);

	return RectI{ monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top, monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top };
#else
	CRY_ASSERT_MESSAGE(false, "Not implemented for platform!");
	return RectI();
#endif
}

CTexture* CSwapChainBackedRenderDisplayContext::GetCurrentBackBuffer() const
{
	uint32 index = 0;

#if (CRY_RENDERER_DIRECT3D >= 120) && defined(__dxgi1_4_h__) // Otherwise index front-buffer only
	IDXGISwapChain3* pSwapChain3;
	m_swapChain.GetSwapChain()->QueryInterface(IID_GFX_ARGS(&pSwapChain3));
	if (pSwapChain3)
	{
		index = pSwapChain3->GetCurrentBackBufferIndex();
		pSwapChain3->Release();
	}
#endif
#if (CRY_RENDERER_VULKAN >= 10)
	index = m_swapChain.GetSwapChain()->GetCurrentBackBufferIndex();
#endif
#if (CRY_RENDERER_GNM)
	// The GNM renderer uses the GPU to drive scan-out, so a command-list is required here.
	auto* pCommandList = GnmCommandList(GetDeviceObjectFactory().GetCoreCommandList().GetGraphicsInterfaceImpl());
	index = m_swapChain.GetSwapChain()->GnmGetCurrentBackBufferIndex(pCommandList);
#endif

	CRY_ASSERT(index < m_backBuffersArray.size());
	if (index >= m_backBuffersArray.size())
		return nullptr;

	return m_backBuffersArray[index];
}

CTexture* CSwapChainBackedRenderDisplayContext::GetStorableColorOutput()
{
	if (IsHighDynamicRange())
	{
		CRY_ASSERT(m_pColorTarget);
		return m_pColorTarget.get();
	}

	// Toggle current back-buffer if transitioning into LDR output
	PostPresent();
	return m_pBackBufferProxy;
}

void CSwapChainBackedRenderDisplayContext::PrePresent()
{
	m_pBackBufferPresented = GetCurrentBackBuffer();
	m_bSwapProxy = true;
}

void CSwapChainBackedRenderDisplayContext::PostPresent()
{
	if (!m_bSwapProxy || !m_backBuffersArray.size())
		return;

	// Substitute current swap-chain back-buffer
	CDeviceTexture* pNewDeviceTex = GetCurrentBackBuffer()->GetDevTexture();
	CDeviceTexture* pOldDeviceTex = m_pBackBufferProxy->GetDevTexture();

	// Trigger dirty-flag handling
	if (pNewDeviceTex != pOldDeviceTex)
		m_pBackBufferProxy->RefDevTexture(pNewDeviceTex);

	m_bSwapProxy = false;
}


//////////////////////////////////////////////////////////////////////////

CCustomRenderDisplayContext::CCustomRenderDisplayContext(IRenderer::SDisplayContextDescription desc, uint32 uniqueId, std::vector<TexSmartPtr> &&backBuffersArray, uint32_t initialSwapChainIndex)
	: CRenderDisplayContext(desc, uniqueId)
	, m_swapChainIndex(initialSwapChainIndex)
{
	CRY_ASSERT(backBuffersArray.size());
	this->m_backBuffersArray = std::move(backBuffersArray);

	const auto &tex = *this->m_backBuffersArray.begin();
	this->ChangeDisplayResolution(tex->GetWidth(), tex->GetHeight());
}

void CCustomRenderDisplayContext::SetSwapChainIndex(uint32_t index)
{
	CRY_ASSERT(index < m_backBuffersArray.size());
	m_swapChainIndex = index;
	m_pRenderOutput->ReinspectDisplayContext();
}

CTexture* CCustomRenderDisplayContext::GetStorableColorOutput()
{
	if (IsHighDynamicRange())
	{
		CRY_ASSERT(m_pColorTarget);
		return m_pColorTarget.get();
	}

	// Toggle current back-buffer if transitioning into LDR output
	PostPresent();
	return GetCurrentBackBuffer();
}

void CCustomRenderDisplayContext::PrePresent()
{
	m_pBackBufferPresented = GetCurrentBackBuffer();
}
