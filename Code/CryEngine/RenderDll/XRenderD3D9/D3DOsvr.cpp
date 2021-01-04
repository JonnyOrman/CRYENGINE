// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#ifdef INCLUDE_VR_RENDERING

	#include "D3DOsvr.h"
	#include <DriverD3D.h>
	#include <CrySystem/VR/IHMDManager.h>
	#ifdef ENABLE_BENCHMARK_SENSOR
		#include <IBenchmarkFramework.h>
		#include <IBenchmarkRendererSensorManager.h>
	#endif
	#include "D3DPostProcess.h"

CD3DOsvrRenderer::CD3DOsvrRenderer(CD3D9Renderer* pRenderer, CD3DStereoRenderer* pStereoRenderer)
	: m_pRenderer(pRenderer)
	, m_pStereoRenderer(pStereoRenderer)
	, m_eyeWidth(0)
	, m_eyeHeight(0)
	, m_swapSetCount(0)
	, m_currentFrame(0)
{

}
CD3DOsvrRenderer::~CD3DOsvrRenderer()
{

}

bool CD3DOsvrRenderer::Initialize(int initialWidth, int initialHeight)
{
	m_eyeWidth  = initialWidth;
	m_eyeHeight = initialHeight;

	CreateTextureSwapSets(m_eyeWidth, m_eyeHeight, 2);

	m_currentFrame = 0;

	return true;
}

void CD3DOsvrRenderer::CreateTextureSwapSets(uint32 width, uint32 height, uint32 swapSetCount)
{
	ReleaseTextureSwapSets();

	m_swapSetCount = swapSetCount;

	char textureName[16];
	const char* textureNameTemplate = "$OsvrEyeTex_%d_%d";

	for (uint32 i = 0; i < swapSetCount; ++i)
	{
		for (uint32 eye = 0; eye < EyeCount; ++eye)
		{
			sprintf_s(textureName, textureNameTemplate, eye, i);

			CTexture* tex = CTexture::GetOrCreateRenderTarget(textureName, width, height, Clr_Transparent, eTT_2D, FT_DONT_STREAM | FT_USAGE_RENDERTARGET, eTF_R8G8B8A8);
			m_scene3DRenderData[eye].textures.Add(tex);
		}

	}
}
void CD3DOsvrRenderer::ReleaseTextureSwapSets()
{
	for (uint32 eye = 0; eye < EyeCount; ++eye)
	{
		for (uint32 j = 0; j < m_scene3DRenderData[eye].textures.Num(); ++j)
		{
			SAFE_RELEASE(m_scene3DRenderData[eye].textures[j]);
		}
		m_scene3DRenderData[eye].textures.SetUse(0);
	}

	m_swapSetCount = 0;
}

void CD3DOsvrRenderer::Shutdown()
{
	ReleaseTextureSwapSets();
}

void CD3DOsvrRenderer::OnResolutionChanged(int newWidth, int newHeight)
{
	if (m_eyeWidth  != newWidth ||
	    m_eyeHeight != newHeight)
	{
		Shutdown();
		Initialize(newWidth, newHeight);
	}
}

void CD3DOsvrRenderer::PrepareFrame(uint64_t frameId){}

void CD3DOsvrRenderer::SubmitFrame()
{
	#ifdef ENABLE_BENCHMARK_SENSOR
	gcpRendD3D->m_benchmarkRendererSensor->PreStereoFrameSubmit(m_scene3DRenderData[0].textures[m_currentFrame], m_scene3DRenderData[1].textures[m_currentFrame]);
	#endif

	#ifdef ENABLE_BENCHMARK_SENSOR
	gcpRendD3D->m_benchmarkRendererSensor->AfterStereoFrameSubmit();
	#endif

	m_currentFrame = (m_currentFrame + 1) % m_swapSetCount;
}

#endif
