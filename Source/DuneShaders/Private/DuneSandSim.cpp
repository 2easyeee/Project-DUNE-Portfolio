// Copyright Epic Games, Inc. All Rights Reserved.

#include "DuneSandSim.h"
#include <atomic>
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

namespace
{
	std::atomic<int32> GDispatches{ 0 };
	std::atomic<int32> GFailures{ 0 };
}

BEGIN_SHADER_PARAMETER_STRUCT(FDuneSandParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SrcTex)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTex)
	SHADER_PARAMETER(FIntPoint, Size)
	SHADER_PARAMETER(float, RangeCm)
	SHADER_PARAMETER(float, CellCm)
	SHADER_PARAMETER(float, RecoverStepCm)
	SHADER_PARAMETER(FVector2f, WindCells)
	SHADER_PARAMETER(float, SlopeTan)
	SHADER_PARAMETER(float, SlopeRate)
	SHADER_PARAMETER(float, MaxTransferCm)
END_SHADER_PARAMETER_STRUCT()

class FDuneSandRecoverCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDuneSandRecoverCS);
	using FParameters = FDuneSandParameters;
	SHADER_USE_PARAMETER_STRUCT(FDuneSandRecoverCS, FGlobalShader);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FDuneSandFlowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDuneSandFlowCS);
	using FParameters = FDuneSandParameters;
	SHADER_USE_PARAMETER_STRUCT(FDuneSandFlowCS, FGlobalShader);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDuneSandRecoverCS, "/Project/DUNE/DuneSandSim.usf", "RecoverCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDuneSandFlowCS, "/Project/DUNE/DuneSandSim.usf", "FlowCS", SF_Compute);

namespace DuneSandSim
{
	bool IsSupported()
	{
		return FApp::CanEverRender() && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
	}

	int32 GetDispatchCount() { return GDispatches.load(); }
	int32 GetFailureCount() { return GFailures.load(); }

	void Dispatch(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, const FDuneSandSimStep& Step)
	{
		FTextureRenderTargetResource* SrcRes = Src ? Src->GameThread_GetRenderTargetResource() : nullptr;
		FTextureRenderTargetResource* DstRes = Dst ? Dst->GameThread_GetRenderTargetResource() : nullptr;
		if (!SrcRes || !DstRes)
		{
			++GFailures;
			return;
		}
		ENQUEUE_RENDER_COMMAND(DuneSandSimStep)([SrcRes, DstRes, Step](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SrcRHI = SrcRes->GetRenderTargetTexture();
			FRHITexture* DstRHI = DstRes->GetRenderTargetTexture();
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FDuneSandRecoverCS> Recover(ShaderMap);
			TShaderMapRef<FDuneSandFlowCS> Flow(ShaderMap);
			if (!SrcRHI || !DstRHI || !EnumHasAnyFlags(DstRHI->GetDesc().Flags, TexCreate_UAV) || !Recover.IsValid() || !Flow.IsValid())
			{
				++GFailures;
				return;
			}
			const FIntPoint Size(DstRHI->GetDesc().Extent);

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SrcTex = RegisterExternalTexture(GraphBuilder, SrcRHI, TEXT("DuneSandSrc"));
			FRDGTextureRef DstTex = RegisterExternalTexture(GraphBuilder, DstRHI, TEXT("DuneSandDst"));
			FRDGTextureRef TmpTex = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Size, DstRHI->GetDesc().Format, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("DuneSandTmp"));

			auto Fill = [&](FDuneSandParameters* P, FRDGTextureRef From, FRDGTextureRef To)
			{
				P->SrcTex = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(From));
				P->DstTex = GraphBuilder.CreateUAV(To);
				P->Size = Size;
				P->RangeCm = Step.RangeCm;
				P->CellCm = Step.CellCm;
				P->RecoverStepCm = Step.RecoverStepCm;
				P->WindCells = Step.WindCells;
				P->SlopeTan = Step.SlopeTan;
				P->SlopeRate = Step.SlopeRate;
				P->MaxTransferCm = Step.MaxTransferCm;
			};
			const FIntVector Groups = FComputeShaderUtils::GetGroupCount(Size, 8);
			FDuneSandParameters* RecoverParams = GraphBuilder.AllocParameters<FDuneSandParameters>();
			Fill(RecoverParams, SrcTex, TmpTex);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DuneSandRecover"), ERDGPassFlags::Compute, Recover, RecoverParams, Groups);
			FDuneSandParameters* FlowParams = GraphBuilder.AllocParameters<FDuneSandParameters>();
			Fill(FlowParams, TmpTex, DstTex);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DuneSandFlow"), ERDGPassFlags::Compute, Flow, FlowParams, Groups);
			GraphBuilder.SetTextureAccessFinal(SrcTex, ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(DstTex, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			++GDispatches;
		});
	}
}
