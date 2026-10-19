// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VegetationFellingBlueprintLibrary.generated.h"

class UInstancedSkinnedMeshComponent;
class AFelledVegetationActor;

/**
 * Blueprint utility functions for vegetation felling.
 */
UCLASS()
class PAPERVEGETATIONFELLING_API UVegetationFellingBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Fell vegetation instances in a cone-shaped volume in front of the character.
	 * Simulates a melee slash: checks all ISKM instances within Distance in the
	 * character's forward direction, within HalfAngle degrees of the facing direction.
	 *
	 * @param WorldContextObject  World context
	 * @param PlayerController    The player performing the action
	 * @param Distance            Max reach distance (cm) from character
	 * @param HalfAngleDegrees    Half-angle of the cone (degrees), e.g. 45 = 90 degree arc
	 * @param StrikeImpulse       Force applied to the falling part
	 * @param CutHeightOffset     Height offset from character root to determine cut point (cm)
	 * @return Number of instances felled
	 */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Felling", meta=(WorldContext="WorldContextObject"))
	static int32 FellVegetationInFrontOfCharacter(
		const UObject* WorldContextObject,
		APlayerController* PlayerController,
		float Distance = 300.f,
		float HalfAngleDegrees = 60.f,
		float StrikeImpulse = 5000.f,
		float CutHeightOffset = 100.f);
};
