// Copyright Epic Games, Inc. All Rights Reserved.

#include "PaperExposureBlueprintLibrary.h"

#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PaperExposureBlueprintLibrary)

bool UPaperExposureBlueprintLibrary::GetViewAverageEV100(
	APlayerController* PlayerController,
	float& OutEV100,
	float& OutAverageLuminance,
	float& OutExposure)
{
	OutEV100 = 0.0f;
	OutAverageLuminance = 0.0f;
	OutExposure = 0.0f;

	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer || !LocalPlayer->ViewportClient)
	{
		return false;
	}

	// CalcSceneViewInitOptions asserts on a valid World internally and dereferences
	// WorldSettings, so guard here to stay safe when called during teardown or level
	// transitions (PlayerController still alive but its World already cleared).
	if (!PlayerController->GetWorld())
	{
		return false;
	}

	FViewport* Viewport = LocalPlayer->ViewportClient->Viewport;
	if (!Viewport)
	{
		return false;
	}

	// CalcSceneViewInitOptions fills SceneViewStateInterface from the local player's
	// persistent per-view state (ViewStates[0]), which the renderer keeps up to date.
	// This is the only public path to that state, since ViewStates itself is private.
	FSceneViewInitOptions ViewInitOptions;
	if (!LocalPlayer->CalcSceneViewInitOptions(ViewInitOptions, Viewport))
	{
		return false;
	}

	const FSceneViewStateInterface* ViewState = ViewInitOptions.SceneViewStateInterface;
	if (!ViewState)
	{
		return false;
	}

	const float AverageLuminance = ViewState->GetLastAverageSceneLuminance();
	if (AverageLuminance <= 0.0f)
	{
		// Auto-exposure inactive (e.g. manual metering) or not yet converged.
		return false;
	}

	OutAverageLuminance = AverageLuminance;
	OutExposure = ViewState->GetLastEyeAdaptationExposure();

	// Match the renderer's average-scene EV100 (as shown by the HDR visualization
	// overlay): convert average luminance to EV100, then offset by log2(1/0.18) to
	// go from average luminance to saturation luminance. See
	// PostProcessEyeAdaptation.cpp: LuminanceToEV100(LuminanceMax, L) + Log2(1/0.18).
	OutEV100 = LuminanceToEV100(AverageLuminance) + FMath::Log2(1.0f / 0.18f);

	return true;
}
