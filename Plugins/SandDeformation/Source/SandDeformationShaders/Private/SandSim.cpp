// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandSim.h"
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

BEGIN_SHADER_PARAMETER_STRUCT(FSandParameters, )
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

BEGIN_SHADER_PARAMETER_STRUCT(FSandStampParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SrcTex)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTex)
	SHADER_PARAMETER(FIntPoint, Size)
	SHADER_PARAMETER(float, RangeCm)
	SHADER_PARAMETER(float, CellCm)
	SHADER_PARAMETER(int32, StampCount)
	SHADER_PARAMETER_ARRAY(FVector4f, StampData, [SandMaxStamps * 3])
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSandScrollParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SrcTex)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTex)
	SHADER_PARAMETER(FIntPoint, Size)
	SHADER_PARAMETER(FIntPoint, ScrollShift)
END_SHADER_PARAMETER_STRUCT()

#define SAND_SHADER(Name, ParamStruct) \
	class Name : public FGlobalShader \
	{ \
		DECLARE_GLOBAL_SHADER(Name); \
		using FParameters = ParamStruct; \
		SHADER_USE_PARAMETER_STRUCT(Name, FGlobalShader); \
	public: \
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
		{ \
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
		} \
	};

SAND_SHADER(FSandStampCS, FSandStampParameters)
SAND_SHADER(FSandRecoverCS, FSandParameters)
SAND_SHADER(FSandFlowCS, FSandParameters)
SAND_SHADER(FSandScrollCS, FSandScrollParameters)

IMPLEMENT_GLOBAL_SHADER(FSandRecoverCS, "/Plugin/SandDeformation/Private/SandDeformationSim.usf", "RecoverCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandFlowCS, "/Plugin/SandDeformation/Private/SandDeformationSim.usf", "FlowCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandStampCS, "/Plugin/SandDeformation/Private/SandDeformationSim.usf", "StampCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandScrollCS, "/Plugin/SandDeformation/Private/SandDeformationSim.usf", "ScrollCS", SF_Compute);

namespace SandSim
{
	bool IsSupported()
	{
		return FApp::CanEverRender() && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
	}

	int32 GetDispatchCount() { return GDispatches.load(); }
	int32 GetFailureCount() { return GFailures.load(); }

	void Dispatch(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, const FSandSimStep& Step)
	{
		FTextureRenderTargetResource* SrcRes = Src ? Src->GameThread_GetRenderTargetResource() : nullptr;
		FTextureRenderTargetResource* DstRes = Dst ? Dst->GameThread_GetRenderTargetResource() : nullptr;
		if (!SrcRes || !DstRes)
		{
			++GFailures;
			return;
		}
		ENQUEUE_RENDER_COMMAND(SandDeformationStep)([SrcRes, DstRes, Step](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SrcRHI = SrcRes->GetRenderTargetTexture();
			FRHITexture* DstRHI = DstRes->GetRenderTargetTexture();
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FSandRecoverCS> Recover(ShaderMap);
			TShaderMapRef<FSandFlowCS> Flow(ShaderMap);
			if (!SrcRHI || !DstRHI || !EnumHasAnyFlags(DstRHI->GetDesc().Flags, TexCreate_UAV) || !Recover.IsValid() || !Flow.IsValid())
			{
				++GFailures;
				return;
			}
			const FIntPoint Size(DstRHI->GetDesc().Extent);

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SrcTex = RegisterExternalTexture(GraphBuilder, SrcRHI, TEXT("SandSrc"));
			FRDGTextureRef DstTex = RegisterExternalTexture(GraphBuilder, DstRHI, TEXT("SandDst"));
			FRDGTextureRef TmpTex = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Size, DstRHI->GetDesc().Format, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("SandTmp"));

			auto Fill = [&](FSandParameters* P, FRDGTextureRef From, FRDGTextureRef To)
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
			FSandParameters* RecoverParams = GraphBuilder.AllocParameters<FSandParameters>();
			Fill(RecoverParams, SrcTex, TmpTex);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SandRecover"), ERDGPassFlags::Compute, Recover, RecoverParams, Groups);
			FSandParameters* FlowParams = GraphBuilder.AllocParameters<FSandParameters>();
			Fill(FlowParams, TmpTex, DstTex);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SandFlow"), ERDGPassFlags::Compute, Flow, FlowParams, Groups);
			GraphBuilder.SetTextureAccessFinal(SrcTex, ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(DstTex, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			++GDispatches;
		});
	}

	void DispatchStamps(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, const TArray<FSandStamp>& Stamps, float RangeCm, float CellCm)
	{
		FTextureRenderTargetResource* SrcRes = Src ? Src->GameThread_GetRenderTargetResource() : nullptr;
		FTextureRenderTargetResource* DstRes = Dst ? Dst->GameThread_GetRenderTargetResource() : nullptr;
		if (!SrcRes || !DstRes || Stamps.Num() == 0)
		{
			++GFailures;
			return;
		}
		TArray<FSandStamp> Copy(Stamps);
		Copy.SetNum(FMath::Min(Copy.Num(), SandMaxStamps));
		ENQUEUE_RENDER_COMMAND(SandDeformationStamps)([SrcRes, DstRes, Copy, RangeCm, CellCm](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SrcRHI = SrcRes->GetRenderTargetTexture();
			FRHITexture* DstRHI = DstRes->GetRenderTargetTexture();
			TShaderMapRef<FSandStampCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			if (!SrcRHI || !DstRHI || !EnumHasAnyFlags(DstRHI->GetDesc().Flags, TexCreate_UAV) || !Shader.IsValid())
			{
				++GFailures;
				return;
			}
			const FIntPoint Size(DstRHI->GetDesc().Extent);
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SrcTex = RegisterExternalTexture(GraphBuilder, SrcRHI, TEXT("SandSrc"));
			FRDGTextureRef DstTex = RegisterExternalTexture(GraphBuilder, DstRHI, TEXT("SandDst"));
			FSandStampParameters* P = GraphBuilder.AllocParameters<FSandStampParameters>();
			P->SrcTex = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SrcTex));
			P->DstTex = GraphBuilder.CreateUAV(DstTex);
			P->Size = Size;
			P->RangeCm = RangeCm;
			P->CellCm = CellCm;
			P->StampCount = Copy.Num();
			for (int32 i = 0; i < SandMaxStamps; ++i)
			{
				const bool bUsed = i < Copy.Num();
				P->StampData[i * 3 + 0] = bUsed ? Copy[i].A : FVector4f::Zero();
				P->StampData[i * 3 + 1] = bUsed ? Copy[i].B : FVector4f::Zero();
				P->StampData[i * 3 + 2] = bUsed ? Copy[i].C : FVector4f::Zero();
			}
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SandStamps"), ERDGPassFlags::Compute, Shader, P, FComputeShaderUtils::GetGroupCount(Size, 8));
			GraphBuilder.SetTextureAccessFinal(SrcTex, ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(DstTex, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			++GDispatches;
		});
	}

	void DispatchScroll(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, FIntPoint ShiftTexels)
	{
		FTextureRenderTargetResource* SrcRes = Src ? Src->GameThread_GetRenderTargetResource() : nullptr;
		FTextureRenderTargetResource* DstRes = Dst ? Dst->GameThread_GetRenderTargetResource() : nullptr;
		if (!SrcRes || !DstRes)
		{
			++GFailures;
			return;
		}
		ENQUEUE_RENDER_COMMAND(SandDeformationScroll)([SrcRes, DstRes, ShiftTexels](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SrcRHI = SrcRes->GetRenderTargetTexture();
			FRHITexture* DstRHI = DstRes->GetRenderTargetTexture();
			TShaderMapRef<FSandScrollCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			if (!SrcRHI || !DstRHI || !EnumHasAnyFlags(DstRHI->GetDesc().Flags, TexCreate_UAV) || !Shader.IsValid())
			{
				++GFailures;
				return;
			}
			const FIntPoint Size(DstRHI->GetDesc().Extent);
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SrcTex = RegisterExternalTexture(GraphBuilder, SrcRHI, TEXT("SandSrc"));
			FRDGTextureRef DstTex = RegisterExternalTexture(GraphBuilder, DstRHI, TEXT("SandDst"));
			FSandScrollParameters* P = GraphBuilder.AllocParameters<FSandScrollParameters>();
			P->SrcTex = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SrcTex));
			P->DstTex = GraphBuilder.CreateUAV(DstTex);
			P->Size = Size;
			P->ScrollShift = ShiftTexels;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SandScroll"), ERDGPassFlags::Compute, Shader, P, FComputeShaderUtils::GetGroupCount(Size, 8));
			GraphBuilder.SetTextureAccessFinal(SrcTex, ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(DstTex, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			++GDispatches;
		});
	}
}
