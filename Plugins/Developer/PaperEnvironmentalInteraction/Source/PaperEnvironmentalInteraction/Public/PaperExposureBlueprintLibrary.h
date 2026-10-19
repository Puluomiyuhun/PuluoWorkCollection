// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PaperExposureBlueprintLibrary.generated.h"

class APlayerController;

/**
 * Blueprint helpers for querying the runtime auto-exposure state of a view.
 *
 * The values returned here come from the persistent scene view state that the
 * renderer updates every frame. They mirror the numbers shown by the HDR
 * visualization overlay (ShowFlag.VisualizeHDR), so the reported EV100 matches
 * what artists see in the on-screen exposure debug display.
 */
UCLASS()
class UPaperExposureBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Reads the average-scene EV100 of the local player's current view.
	 *
	 * Uses the previous frame's converged auto-exposure result, so the value has
	 * one frame of latency (this is inherent to eye adaptation and expected).
	 *
	 * Requires auto-exposure to be active. In manual metering mode, or during the
	 * first frames before adaptation converges, the average luminance is invalid
	 * and the function returns false.
	 *
	 * @param PlayerController     Local player controller whose view is queried.
	 * @param OutEV100             Average-scene EV100, matching the HDR debug overlay.
	 * @param OutAverageLuminance  Average scene luminance in cd/m^2 (0 when invalid).
	 * @param OutExposure          Final eye-adaptation exposure multiplier applied to scene color.
	 * @return                     true when a valid exposure reading was produced.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper|Rendering|Exposure",
		meta = (DisplayName = "Get View Average EV100", ReturnDisplayName = "Valid"))
	static bool GetViewAverageEV100(
		APlayerController* PlayerController,
		float& OutEV100,
		float& OutAverageLuminance,
		float& OutExposure);
};
