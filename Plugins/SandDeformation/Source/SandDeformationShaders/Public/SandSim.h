// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTextureRenderTarget2D;

/** Settings of one simulation step of the sand heightfield (already converted to texel units). */
struct FSandSimStep
{
	float RangeCm = 32.f;
	float CellCm = 2.34f;		// world cm per texel
	float RecoverStepCm = 0.f;	// cm a trail fades in this step
	FVector2f WindCells = FVector2f::ZeroVector;	// wind transport in texels per step, |x|+|y| <= 0.45
	float SlopeTan = 0.4f;		// stable slope (rise per run)
	float SlopeRate = 0.f;		// 0 = no collapse
	float MaxTransferCm = 0.4f;	// most one neighbour pair exchanges per step
};

/** One footprint or landing, see StampCS in SandDeformationSim.usf (lengths in texels, heights in cm). */
struct FSandStamp
{
	FVector4f A = FVector4f(0, 0, 1, 0);	// centre xy, direction xy
	FVector4f B = FVector4f(1, 1, 0, 0);	// half length, half width, depth, ridge height
	FVector4f C = FVector4f(0, 0, 0, 0);	// type (0 foot, 1 round, 2 segment, 3 box), noise seed, push (accumulate: max depth), flags (1 sphere cap, 2 accumulate)
};

constexpr int32 SandMaxStamps = 16;

/**
 * Sand heightfield on the GPU (render graph compute passes). Each call reads one render target and writes another (ping-pong), so the caller
 * swaps after every call. The render targets must have been created with bSupportsUAV.
 */
namespace SandSim
{
	/** Shaders are compiled for this platform / RHI (false in a -nullrhi run). */
	SANDDEFORMATIONSHADERS_API bool IsSupported();

	/** Fade, then slope collapse + wind transport: Dst = step(Src). Both targets must stay alive until the queue has run (the caller flushes at shutdown). */
	SANDDEFORMATIONSHADERS_API void Dispatch(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, const FSandSimStep& Step);

	/** Applies the stamps (at most SandMaxStamps) in order: Dst = stamps(Src). */
	SANDDEFORMATIONSHADERS_API void DispatchStamps(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, const TArray<FSandStamp>& Stamps, float RangeCm, float CellCm);

	/** Moves the heightfield: Dst(p) = Src(p + ShiftTexels), texels that come from outside Src become untouched sand. Used when the patch follows an actor. */
	SANDDEFORMATIONSHADERS_API void DispatchScroll(UTextureRenderTarget2D* Src, UTextureRenderTarget2D* Dst, FIntPoint ShiftTexels);

	/** Passes that ran on the render thread, and passes that could not (shader missing / texture without UAV). */
	SANDDEFORMATIONSHADERS_API int32 GetDispatchCount();
	SANDDEFORMATIONSHADERS_API int32 GetFailureCount();
}
