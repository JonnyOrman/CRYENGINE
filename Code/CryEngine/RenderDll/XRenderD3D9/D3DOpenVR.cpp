// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#if defined(INCLUDE_VR_RENDERING)

	#include "D3DOpenVR.h"
	#include "D3DPostProcess.h"
	#include "DeviceInfo.h"

	#include <Common/RenderDisplayContext.h>

	#include <CrySystem/VR/IHMDManager.h>
	#include <CrySystem/VR/IHMDDevice.h>
	#ifdef ENABLE_BENCHMARK_SENSOR
		#include <IBenchmarkFramework.h>
		#include <IBenchmarkRendererSensorManager.h>
	#endif

CD3DOpenVRRenderer::CD3DOpenVRRenderer(CD3D9Renderer* renderer, CD3DStereoRenderer* stereoRenderer)
	: m_pRenderer(renderer)
	, m_pStereoRenderer(stereoRenderer)
	, m_eyeWidth(~0L)
	, m_eyeHeight(~0L)
	, m_numFrames(0)
	, m_currentFrame(0)
{
	ZeroArray(m_scene3DRenderData);
	ZeroArray(m_quadLayerRenderData);
	ZeroArray(m_mirrorTextures);

	for (uint32 i = RenderLayer::eQuadLayers_0; i < RenderLayer::eQuadLayers_Headlocked_0; ++i)
	{
		m_quadLayerProperties[i].SetType(RenderLayer::eLayer_Quad);
		m_quadLayerProperties[i].SetPose(QuatTS(Quat(IDENTITY), Vec3(0.f, 0.f, -0.8f), 1.f));
		m_quadLayerProperties[i].SetId(i);
	}
	for (uint32 i = RenderLayer::eQuadLayers_Headlocked_0; i < RenderLayer::eQuadLayers_Total; ++i)
	{
		m_quadLayerProperties[i].SetType(RenderLayer::eLayer_Quad_HeadLocked);
		m_quadLayerProperties[i].SetPose(QuatTS(Quat(IDENTITY), Vec3(0.f, 0.f, -0.8f), 1.f));
		m_quadLayerProperties[i].SetId(i);
	}
}

bool CD3DOpenVRRenderer::Initialize(int initialWidth, int initialeight)
{
#if CRY_RENDERER_DIRECT3D >= 120
#else
	CRY_ASSERT(false, "Unsuported");
#endif

	m_eyeWidth  = initialWidth;
	m_eyeHeight = initialeight;

	// Create display contexts for eyes
	for (uint32 eye = 0; eye < eEyeType_NumEyes; ++eye)
	{
		std::vector<_smart_ptr<CTexture>> swapChain = { m_scene3DRenderData[eye].texture };
		m_pStereoRenderer->CreateEyeDisplayContext(CCamera::EEye(eye), std::move(swapChain));
	}
	// Create display contexts for quad layers
	for (uint32 quad = RenderLayer::eQuadLayers_0; quad < RenderLayer::eQuadLayers_Total; ++quad)
	{
		std::vector<_smart_ptr<CTexture>> swapChain = { m_quadLayerRenderData[quad].texture };
		m_pStereoRenderer->CreateVrQuadLayerDisplayContext(RenderLayer::EQuadLayers(quad), std::move(swapChain));
	}

	// Mirror texture
	for (uint32 eye = 0; eye < 2; ++eye)
	{
#if CRY_RENDERER_DIRECT3D >= 120
		// No mirror textures in D3D12
		// Request the resource-view from openVR
#elif CRY_RENDERER_DIRECT3D >= 110
		void* srv = nullptr;
		m_mirrorTextures[eye]->SetDefaultShaderResourceView(static_cast<D3DShaderResource*>(srv), false);
#endif
	}

	return true;
}

bool CD3DOpenVRRenderer::InitializeEyeTarget(D3DDevice* d3d11Device, EEyeType eye, const char* name)
{
	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;
	ID3D11Texture2D* texture;
	d3d11Device->CreateTexture2D(&textureDesc, nullptr, &texture);

	m_scene3DRenderData[eye].texture = m_pStereoRenderer->WrapD3DRenderTarget(static_cast<D3DTexture*>(texture), 0.f, 0.f, (DXGI_FORMAT)0.f, name, true);

	return true;
}

bool CD3DOpenVRRenderer::InitializeQuadLayer(D3DDevice* d3d11Device, RenderLayer::EQuadLayers quadLayer, const char* name)
{
	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	ID3D11Texture2D* texture;
	d3d11Device->CreateTexture2D(&textureDesc, nullptr, &texture);

	char textureName[16];
	cry_sprintf(textureName, name, quadLayer);

	m_quadLayerRenderData[quadLayer].texture = m_pStereoRenderer->WrapD3DRenderTarget(static_cast<D3DTexture*>(texture), 0.f, 0.f, (DXGI_FORMAT)0.f, textureName, true);

	return true;
}

bool CD3DOpenVRRenderer::InitializeMirrorTexture(D3DDevice* d3d11Device, EEyeType eye, const char* name)
{
	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	ID3D11Texture2D* texture;
	d3d11Device->CreateTexture2D(&textureDesc, nullptr, &texture);

	m_mirrorTextures[eye] = m_pStereoRenderer->WrapD3DRenderTarget(static_cast<D3DTexture*>(texture), 0.f, 0.f, (DXGI_FORMAT)0.f, name, false);
	
	return true;
}

void CD3DOpenVRRenderer::Shutdown()
{
// 	m_pStereoRenderer->SetEyeTextures(nullptr, nullptr);

	// Scene3D layers
	for (uint32 eye = 0; eye < 2; ++eye)
	{
		m_scene3DRenderData[eye].texture = nullptr;
	}

	// Quad layers
	for (uint32 i = 0; i < RenderLayer::eQuadLayers_Total; ++i)
	{
		m_quadLayerRenderData[i].texture = nullptr;
	}

	// Mirror texture
	for (uint32 eye = 0; eye < 2; ++eye)
	{
		if (m_mirrorTextures[eye])
			m_mirrorTextures[eye] = nullptr;
	}
	
	ReleaseBuffers();
}

void CD3DOpenVRRenderer::OnResolutionChanged(int newWidth, int newHeight)
{
	if (m_eyeWidth  != newWidth ||
	    m_eyeHeight != newHeight)
	{
		Shutdown();
		Initialize(newWidth, newHeight);
	}
}

void CD3DOpenVRRenderer::PrepareFrame(uint64_t frameId)
{
}

void CD3DOpenVRRenderer::SubmitFrame()
{
#ifdef ENABLE_BENCHMARK_SENSOR
	gcpRendD3D->m_benchmarkRendererSensor->PreStereoFrameSubmit(m_scene3DRenderData[0].texture, m_scene3DRenderData[1].texture);
#endif

#if (CRY_RENDERER_DIRECT3D >= 120)
	{
		// Transition resources
		ID3D11Device* pD3d11Device = m_pRenderer->GetDevice();
		NCryDX12::CCommandList* pCL = ((CCryDX12Device*)pD3d11Device)->GetDeviceContext()->GetCoreGraphicsCommandList();

		CCryDX12RenderTargetView* lRV = (CCryDX12RenderTargetView*)m_scene3DRenderData[EEyeType::eEyeType_LeftEye].texture->GetSurface(-1, 0);
		CCryDX12RenderTargetView* rRV = (CCryDX12RenderTargetView*)m_scene3DRenderData[EEyeType::eEyeType_RightEye].texture->GetSurface(-1, 0);

		NCryDX12::CView& lV = lRV->GetDX12View();
		NCryDX12::CView& rV = rRV->GetDX12View();
		lV.GetDX12Resource().TransitionBarrier(pCL, lV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		rV.GetDX12Resource().TransitionBarrier(pCL, rV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// Quad layers
		for (uint32 i = 0; i < RenderLayer::eQuadLayers_Total; ++i)
		{
			const RenderLayer::CProperties* pQuadProperties = GetQuadLayerProperties(static_cast<RenderLayer::EQuadLayers>(i));
			if (!pQuadProperties->IsActive())
				continue;

			if (m_quadLayerRenderData[i].texture)
			{
				CCryDX12RenderTargetView* qRV = (CCryDX12RenderTargetView*)m_quadLayerRenderData[i].texture->GetSurface(-1, 0);
				if (qRV)
				{
					NCryDX12::CView& qV = qRV->GetDX12View();
					qV.GetDX12Resource().TransitionBarrier(pCL, qV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				}
			}
		}

		((CCryDX12Device*)pD3d11Device)->GetDeviceContext()->Finish();
	}
#endif

	// Quad layers
	for (uint32 i = 0; i < RenderLayer::eQuadLayers_Total; ++i)
	{
		RenderLayer::CProperties* pQuadProperties = GetQuadLayerProperties(static_cast<RenderLayer::EQuadLayers>(i));
		if (pQuadProperties->IsActive())
		{
			pQuadProperties->SetActive(false);
		}
	}

#ifdef ENABLE_BENCHMARK_SENSOR
	gcpRendD3D->m_benchmarkRendererSensor->AfterStereoFrameSubmit();
#endif
}

void CD3DOpenVRRenderer::OnPostPresent()
{
}

RenderLayer::CProperties* CD3DOpenVRRenderer::GetQuadLayerProperties(RenderLayer::EQuadLayers id)
{
	if (id < RenderLayer::eQuadLayers_Total)
		return &(m_quadLayerProperties[id]);
	return nullptr;
}

std::pair<CTexture*, Vec4> CD3DOpenVRRenderer::GetMirrorTexture(EEyeType eye) const
{
	return std::make_pair(m_mirrorTextures[eye], Vec4(0,0,1,1));
}

#endif //defined(INCLUDE_VR_RENDERING)
