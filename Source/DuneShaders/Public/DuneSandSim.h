// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTextureRenderTarget2D;

/** Settings of one simulation step of the sand heightfield (already converted to texel units). */
struct FDuneSandSimStep
{
	float RangeCm = 32.f;
	float CellCm = 2.34f;		// world cm per texel
	float RecoverStepCm = 0.f;	// cm a trail fades in this step
	FVector2f WindCells = FVector2f::ZeroVector;	// wind transport in texels per step, |x|+|y| <= 0.45
	float SlopeTan = 0.4f;		// stable slope (rise per run)
	float SlopeRate = 0.f;		// 0 = no collapse
	float MaxTransferCm = 0.4f;	// most one neighbour pair exchanges per step
};

/**
 * Project DUNE sand heightfield on the GPU: two compute passes (fade, then slope collapse + wind transport) through the render graph,
 * reading one render target and writing another. The render targets must have been created with bSupportsUAV.
 */
namespace DuneSandSim
{
	/** Shaders are compiled for this platform / RHI (false in a -nullrhi run). */
	DUNESHADERS_API bool IsSupported();

	/** Queues one step on the render thread: Dst = step(Src). Both must stay alive until the queue has run (the caller flushes at shutdown). */
	DUNESHADERS_API void Dispatch(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, const FDuneSandSimStep& Step);

	/** Steps that ran on the render thread, and steps that could not (shader missing / texture without UAV). */
	DUNESHADERS_API int32 GetDispatchCount();
	DUNESHADERS_API int32 GetFailureCount();
}
