// Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#include "D3DStereo.h"

#include "D3DREBreakableGlassBuffer.h"

#include <CryCore/CryCustomTypes.h>

#include "Gpu/Particles/GpuParticleManager.h"
#include <CrySystem/VR/IHMDManager.h>

#include "D3D_SVO.h"
#include <Common/RenderDisplayContext.h>
//=======================================================================

bool CD3D9Renderer::RT_CreateDevice()
{
	CRY_PROFILE_FUNCTION(PROFILE_LOADING_ONLY);
	MEMSTAT_CONTEXT(EMemStatContextType::D3D, "Renderer CreateDevice");

#if CRY_PLATFORM_WINDOWS && !defined(SUPPORT_DEVICE_INFO)
	if (!m_bShaderCacheGen && !SetWindow(m_width, m_height))
		return false;
#endif

	return CreateDevice();
}

void CD3D9Renderer::RT_Init()
{
	EF_Init();
}

void CD3D9Renderer::RT_ReleaseRenderResources(uint32 nFlags)
{
	CRY_PROFILE_SECTION(PROFILE_RENDERER, "CD3D9Renderer::RT_ReleaseRenderResources");

	if (nFlags & FRR_FLUSH_TEXTURESTREAMING)
	{
		CTexture::RT_FlushStreaming(true);
	}

	if (nFlags & FRR_PERMANENT_RENDER_OBJECTS)
	{
		for (int i = 0; i < RT_COMMAND_BUF_COUNT; i++)
		{
			FreePermanentRenderObjects(i);
		}
	}

	if (nFlags & FRR_DELETED_MESHES)
	{
		CRenderMesh::Tick(MAX_RELEASED_MESH_FRAMES);
	}

	if (nFlags & FRR_POST_EFFECTS)
	{
		if (m_pPostProcessMgr)
			m_pPostProcessMgr->ReleaseResources();
	}

	if (nFlags & FRR_SYSTEM_RESOURCES)
	{
		// 1) Make sure all high level objects (CRenderMesh, CRenderElement,..) are gone
		RT_DelayedDeleteResources(true);

		CREBreakableGlassBuffer::RT_ReleaseInstance();

		CRenderMesh::Tick(MAX_RELEASED_MESH_FRAMES);
		CRenderElement::Cleanup();

		// 2) Release renderer created high level stuff (CStandardGraphicsPipeline, CPrimitiveRenderPass, CSceneRenderPass,..)

		// Drop stereo resources
		if (gRenDev->GetIStereoRenderer())
			gRenDev->GetIStereoRenderer()->ReleaseRenderResources();

		CREParticle::ResetPool();

		if (m_pStereoRenderer)
			m_pStereoRenderer->ReleaseBuffers();

#if defined(ENABLE_RENDER_AUX_GEOM)
		if (m_pRenderAuxGeomD3D)
			m_pRenderAuxGeomD3D->ReleaseResources();
#endif //ENABLE_RENDER_AUX_GEOM

		if (m_pGpuParticleManager)
			m_pGpuParticleManager->ReleaseResources();

		if (!(nFlags & FRR_TEXTURES))
		{
			m_pActiveGraphicsPipeline->GetPipelineResources().Clear();
		}

		m_pBaseGraphicsPipeline.reset();
		m_pActiveGraphicsPipeline.reset();
		m_graphicsPipelines.clear();
		m_renderToTexturePipelineKey = SGraphicsPipelineKey::InvalidGraphicsPipelineKey;

		if (nFlags == FRR_SHUTDOWN)
			CRenderMesh::ShutDown();

#if defined(FEATURE_SVO_GI)
		// TODO: GraphicsPipeline-Stage shutdown with ShutDown()
		if (auto pSvoRenderer = CSvoRenderer::GetInstance(false))
			pSvoRenderer->Release();
#endif

		// 3) At this point all device objects should be gone and we can safely reset PSOs, ResourceLayouts,..
		CDeviceObjectFactory::ResetInstance();

		// 4) Now release textures and shaders
		m_cEF.mfReleaseSystemShaders();
		m_cEF.m_Bin.InvalidateCache();

		EF_Exit();
		CRendererResources::DestroySystemTargets();
		CRendererResources::UnloadDefaultSystemTextures();
	}

	if (nFlags & FRR_TEXTURES)
	{
		// Must also delete back buffers from Display Contexts
		for (auto& pCtx : m_displayContexts)
			pCtx.second->ReleaseResources();

		CTexture::ShutDown();
		CRendererResources::ShutDown();
	}

	// sync dev buffer only once per frame, to prevent syncing to the currently rendered frame
	// which would result in a deadlock
	if (nFlags & (FRR_SYSTEM_RESOURCES | FRR_DELETED_MESHES))
	{
		gRenDev->m_DevBufMan.Sync(gRenDev->GetRenderFrameID());
	}
}

void CD3D9Renderer::RT_CreateRenderResources()
{
	CRY_PROFILE_SECTION(PROFILE_RENDERER, "CD3D9Renderer::RT_CreateRenderResources");

	CRendererResources::LoadDefaultSystemTextures();
	CRendererResources::CreateSystemTargets(0, 0);

	EF_Init();

#if defined(FEATURE_SVO_GI)
	// TODO: GraphicsPipeline-Stage bootstrapped with Init()
	CSvoRenderer::GetInstance(true);
#endif

	// Create BaseGraphicsPipeline
	if (!m_pBaseGraphicsPipeline)
	{
		SGraphicsPipelineDescription pipelineDesc;
		pipelineDesc.type = (CRenderer::CV_r_GraphicsPipelineMobile) ? EGraphicsPipelineType::Mobile : EGraphicsPipelineType::Standard;
		pipelineDesc.shaderFlags = SHDF_ZPASS | SHDF_ALLOWHDR | SHDF_ALLOWPOSTPROCESS | SHDF_ALLOW_WATER | SHDF_ALLOW_AO | SHDF_ALLOW_SKY | SHDF_ALLOW_RENDER_DEBUG;

		SGraphicsPipelineKey key = RT_CreateGraphicsPipeline(pipelineDesc);
		m_pBaseGraphicsPipeline = m_graphicsPipelines[key];
		m_pActiveGraphicsPipeline = m_pBaseGraphicsPipeline;

		CRY_ASSERT(m_pBaseGraphicsPipeline);
	}


	if (m_pPostProcessMgr)
	{
		m_pPostProcessMgr->CreateResources();
	}
}

void CD3D9Renderer::RT_PrecacheDefaultShaders()
{
}

void CD3D9Renderer::SetRendererCVar(ICVar* pCVar, const char* pArgText, const bool bSilentMode)
{
	if (!pCVar)
		return;

	string argText = pArgText;
	ExecuteRenderThreadCommand(
		[=]
		{
			pCVar->SetFromString(argText.c_str());

			if (!bSilentMode)
			{
				if (gEnv->IsEditor())
					gEnv->pLog->LogWithType(ILog::eInputResponse, "%s = %s (Renderer CVar)", pCVar->GetName(), pCVar->GetString());
				else
					gEnv->pLog->LogWithType(ILog::eInputResponse, "    $3%s = $6%s $5(Renderer CVar)", pCVar->GetName(), pCVar->GetString());
			}
		},
		ERenderCommandFlags::None
	);
}