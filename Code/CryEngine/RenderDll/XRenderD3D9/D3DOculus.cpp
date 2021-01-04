// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#include "D3DOculus.h"
#include "D3DPostProcess.h"

#include <Common/RenderDisplayContext.h>

#include <CrySystem/VR/IHMDManager.h>
#include <CrySystem/VR/IHMDDevice.h>
#ifdef ENABLE_BENCHMARK_SENSOR
	#include <IBenchmarkFramework.h>
	#include <IBenchmarkRendererSensorManager.h>
#endif
#if defined(INCLUDE_VR_RENDERING)

	#if (CRY_RENDERER_DIRECT3D >= 120)
		#include "DX12/Resource/Texture/CCryDX12Texture2D.hpp"
		#if defined(DX12_LINKEDADAPTER)
			#include "DX12/API/Redirections/D3D12Device.inl"
			#include "DX12/API/Workarounds/OculusMultiGPU.inl"
		#endif
	#endif

CD3DOculusRenderer::SLayersManager::SLayersManager()
{
	// Init layers properties
	for (uint32 i = RenderLayer::eSceneLayers_0; i < RenderLayer::eSceneLayers_Total; ++i)
	{
		m_scene3DLayerProperties[i].SetType(RenderLayer::eLayer_Scene3D);
		m_scene3DLayerProperties[i].SetPose(QuatTS(Quat(IDENTITY), Vec3(0.f, 0.f, -.8f), 1.f));
		m_scene3DLayerProperties[i].SetId(i);
	}

	for (uint32 i = RenderLayer::eQuadLayers_0; i < RenderLayer::eQuadLayers_Headlocked_0; ++i)
	{
		m_quadLayerProperties[i].SetType(RenderLayer::eLayer_Quad);
		m_quadLayerProperties[i].SetPose(QuatTS(Quat(IDENTITY), Vec3(0.f, 0.f, -.8f), 1.f));
		m_quadLayerProperties[i].SetId(i);
	}
	for (uint32 i = RenderLayer::eQuadLayers_Headlocked_0; i < RenderLayer::eQuadLayers_Total; ++i)
	{
		m_quadLayerProperties[i].SetType(RenderLayer::eLayer_Quad_HeadLocked);
		m_quadLayerProperties[i].SetPose(QuatTS(Quat(IDENTITY), Vec3(0.f, 0.f, -.8f), 1.f));
		m_quadLayerProperties[i].SetId(i);
	}
}

void CD3DOculusRenderer::SLayersManager::UpdateSwapChainData(CD3DStereoRenderer* pStereoRenderer, const STextureSwapChainRenderData* scene3DRenderData, const STextureSwapChainRenderData* quadRenderData)
{
	for (uint32 i = 0; i < RenderLayer::eQuadLayers_Total; ++i)
	{
		const RenderLayer::EQuadLayers layerId = RenderLayer::EQuadLayers(i);
		const RenderLayer::CProperties& quadLayerPropertiesRT = m_quadLayerProperties[layerId];

		if (ITexture* pTexture = quadLayerPropertiesRT.GetTexture())
		{
			const auto *quadDc = pStereoRenderer->GetVrQuadLayerDisplayContext(layerId).first;
			if (quadDc)
			{
				CTexture* pQuadTex = quadDc->GetCurrentBackBuffer();
				GetUtils().StretchRect(static_cast<CTexture*>(pTexture), pQuadTex);
			}
		}
	}
}

// -------------------------------------------------------------------------

CD3DOculusRenderer::CD3DOculusRenderer(CD3D9Renderer* renderer, CD3DStereoRenderer* stereoRenderer)
	: m_pRenderer(renderer)
	, m_pStereoRenderer(stereoRenderer)
	, m_eyeWidth(~0L)
	, m_eyeHeight(~0L)
{
}

bool CD3DOculusRenderer::Initialize(int initialWidth, int initialeight)
{
	// Create display contexts for eyes
	for (uint32 eye = 0; eye < eEyeType_NumEyes; ++eye)
	{
		std::vector<_smart_ptr<CTexture>> swapChain;
		for (const auto& tex : m_scene3DRenderData[eye].textures)
			swapChain.emplace_back(tex.get());
		m_pStereoRenderer->CreateEyeDisplayContext(CCamera::EEye(eye), std::move(swapChain));
	}
	// Create display contexts for quad layers
	for (uint32 quad = 0; quad < RenderLayer::eQuadLayers_Total; ++quad)
	{
		std::vector<_smart_ptr<CTexture>> swapChain;
		for (const auto& tex : m_quadLayerRenderData[quad].textures)
			swapChain.emplace_back(tex.get());
		m_pStereoRenderer->CreateVrQuadLayerDisplayContext(RenderLayer::EQuadLayers(quad), std::move(swapChain));
	}

	SetupRenderTargets();

	m_deviceLostFlag = false;

	return true;
}

bool CD3DOculusRenderer::InitializeTextureSwapSet(ID3D11Device* pD3d11Device, EEyeType eye, STextureSwapChainRenderData& eyeRenderData, const std::string& name)
{
	return false;
}

bool CD3DOculusRenderer::InitializeTextureSwapSet(ID3D11Device* pD3d11Device, EEyeType eye, const std::string& name)
{
	return InitializeTextureSwapSet(pD3d11Device, eye, m_scene3DRenderData[eye], name);
}

bool CD3DOculusRenderer::InitializeQuadTextureSwapSet(ID3D11Device* d3dDevice, RenderLayer::EQuadLayers id, const std::string& name)
{
	return InitializeTextureSwapSet(d3dDevice, static_cast<EEyeType>(-1), m_quadLayerRenderData[id], name);
}

bool CD3DOculusRenderer::InitializeMirrorTexture(ID3D11Device* pD3d11Device, const std::string& name)
{
	{
		return m_mirrorData.pMirrorTexture != nullptr;
	}

	return m_mirrorData.pMirrorTextureNative != nullptr;
}

void CD3DOculusRenderer::Shutdown()
{
	for (uint32 eye = 0; eye < eEyeType_NumEyes; ++eye)
		m_pStereoRenderer->CreateEyeDisplayContext(CCamera::EEye(eye), {});
	for (uint32 quad = 0; quad < RenderLayer::eQuadLayers_Total; ++quad)
		m_pStereoRenderer->CreateVrQuadLayerDisplayContext(RenderLayer::EQuadLayers(quad), {});

	// Scene3D layers
	for (uint32 eye = 0; eye < 2; ++eye)
	{
		m_scene3DRenderData[eye].textures = {};
	}

	// Quad layers
	for (uint32 i = 0; i < RenderLayer::eQuadLayers_Total; ++i)
	{
		m_quadLayerRenderData[i].textures = {};
	}

	// Mirror texture
	m_mirrorData = {};

	ReleaseBuffers();

	m_deviceLostFlag = true;
}

void CD3DOculusRenderer::OnResolutionChanged(int newWidth, int newHeight)
{
	if (m_eyeWidth  != newWidth ||
		m_eyeHeight != newHeight)
	{
		Shutdown();
		Initialize(newWidth, newHeight);
	}
}

void CD3DOculusRenderer::PrepareFrame(uint64_t frameId)
{
	SetupRenderTargets();

	GetSceneLayerProperties(RenderLayer::eSceneLayers_0)->SetActive(true);

	// Recreate device if devicelost error was raised
	if (m_deviceLostFlag.load())
	{
		Shutdown();
		Initialize(m_eyeWidth, m_eyeHeight);
	}
}

void CD3DOculusRenderer::SetupRenderTargets()
{
	{
		std::array<uint32_t, eEyeType_NumEyes> indices = {}; // texture index in various swap chains
		m_pStereoRenderer->SetCurrentEyeSwapChainIndices(indices);
	}

	{
		// Quad layers
		std::array<uint32_t, RenderLayer::eQuadLayers_Total> indices = {}; // texture index in various swap chains
		m_pStereoRenderer->SetCurrentQuadLayerSwapChainIndices(indices);
	}
}

void CD3DOculusRenderer::SubmitFrame()
{
	if (m_deviceLostFlag.load())
		return;

	FUNCTION_PROFILER_RENDERER();

	// Update swap chain info with the latest render and layer properties
	m_layerManager.UpdateSwapChainData(m_pStereoRenderer, m_scene3DRenderData, m_quadLayerRenderData);

#ifdef ENABLE_BENCHMARK_SENSOR
	const auto *leftDc = m_pStereoRenderer->GetEyeDisplayContext(CCamera::eEye_Left).first;
	const auto *rightDc = m_pStereoRenderer->GetEyeDisplayContext(CCamera::eEye_Right).first;
	if (leftDc && rightDc)
		gcpRendD3D->m_benchmarkRendererSensor->PreStereoFrameSubmit(leftDc->GetCurrentBackBuffer(), rightDc->GetCurrentBackBuffer());
#endif

#if (CRY_RENDERER_DIRECT3D >= 120)
	ID3D11Device* pD3d11Device = m_pRenderer->GetDevice();
	NCryDX12::CCommandList* pCL = ((CCryDX12Device*)pD3d11Device)->GetDeviceContext()->GetCoreGraphicsCommandList();

#if DX12_LINKEDADAPTER
	// NOTE: Workaround for missing MultiGPU-support in the Oculus library
	if (m_pRenderer->GetDevice()->GetNodeCount() > 1)
	{
		CopyMultiGPUFrameData();
	}
	else
#endif
	{
		// Scene3D layer
		const auto *leftDc = m_pStereoRenderer->GetEyeDisplayContext(CCamera::eEye_Left).first;
		const auto *rightDc = m_pStereoRenderer->GetEyeDisplayContext(CCamera::eEye_Right).first;
		if (!leftDc || !rightDc)
			return;

		CCryDX12RenderTargetView* lRV = (CCryDX12RenderTargetView*)leftDc->GetCurrentBackBuffer()->GetSurface(-1, 0);
		CCryDX12RenderTargetView* rRV = (CCryDX12RenderTargetView*)rightDc->GetCurrentBackBuffer()->GetSurface(-1, 0);

		NCryDX12::CView& lV = lRV->GetDX12View();
		NCryDX12::CView& rV = rRV->GetDX12View();

		lV.GetDX12Resource().TransitionBarrier(pCL, lV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		rV.GetDX12Resource().TransitionBarrier(pCL, rV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		((CCryDX12Device*)pD3d11Device)->GetDeviceContext()->Finish();
	}
#endif


#ifdef ENABLE_BENCHMARK_SENSOR
	gcpRendD3D->m_benchmarkRendererSensor->AfterStereoFrameSubmit();
#endif

	// Deactivate scene layers
	GetSceneLayerProperties(RenderLayer::eSceneLayers_0)->SetActive(false);
	for (uint32 i = 0; i < RenderLayer::eQuadLayers_Total; ++i)
		GetQuadLayerProperties(static_cast<RenderLayer::EQuadLayers>(i))->SetActive(false);
}

RenderLayer::CProperties* CD3DOculusRenderer::GetQuadLayerProperties(RenderLayer::EQuadLayers id)
{
	if (id < RenderLayer::eQuadLayers_Total)
	{
		return &(m_layerManager.m_quadLayerProperties[id]);
	}
	return nullptr;
}

RenderLayer::CProperties* CD3DOculusRenderer::GetSceneLayerProperties(RenderLayer::ESceneLayers id)
{
	if (id < RenderLayer::eQuadLayers_Total)
	{
		return &(m_layerManager.m_scene3DLayerProperties[id]);
	}
	return nullptr;
}

std::pair<CTexture*, Vec4> CD3DOculusRenderer::GetMirrorTexture(EEyeType eye) const 
{
	Vec4 tc = Vec4(0.0f, 0.0f, 0.5f, 1.0f);
	if (eye == eEyeType_RightEye)
		tc.x = 0.5f;

	return std::make_pair(m_mirrorData.pMirrorTexture, tc);
}

#endif // defined(INCLUDE_VR_RENDERING)
