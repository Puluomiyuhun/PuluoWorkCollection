// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VegetationReactionTable.generated.h"

/**
 * One source-mesh -> reaction-actor mapping.
 *
 * SourceMesh is the asset placed in the scene: a UStaticMesh (instanced via ISM)
 * or a USkeletalMesh (instanced via ISKM). ReactionActorClass decides BOTH what to
 * spawn and which behavior to run - its type is inspected at dispatch:
 *   - a AFelledVegetationActor subclass -> the instance is felled (斩断)
 *   - a AKnockedFoliageActor  subclass -> the instance is knocked away (击飞)
 * so an ISKM plant can be knocked and an ISM plant can be felled, fully decoupled.
 */
USTRUCT(BlueprintType)
struct PAPERVEGETATIONFELLING_API FVegetationReactionEntry
{
	GENERATED_BODY()

	/** Source asset placed in the scene (UStaticMesh for ISM, USkeletalMesh for ISKM). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reaction")
	TObjectPtr<UObject> SourceMesh = nullptr;

	/**
	 * Reaction actor to spawn. A AFelledVegetationActor subclass -> fell;
	 * a AKnockedFoliageActor subclass -> knock.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reaction")
	TSubclassOf<AActor> ReactionActorClass;
};

/**
 * Data-driven dispatch table: maps each vegetation source mesh to the reaction
 * actor (and thus behavior) it should trigger when destroyed. Instances whose
 * source mesh is not listed are left untouched.
 */
UCLASS(BlueprintType)
class PAPERVEGETATIONFELLING_API UVegetationReactionTable : public UDataAsset
{
	GENERATED_BODY()

public:
	/** All configured mesh -> reaction mappings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reaction")
	TArray<FVegetationReactionEntry> Entries;

	/** Return the reaction class mapped to Mesh, or nullptr when the mesh is not listed. */
	TSubclassOf<AActor> FindReactionClass(const UObject* Mesh) const
	{
		if (!Mesh)
		{
			return nullptr;
		}
		for (const FVegetationReactionEntry& Entry : Entries)
		{
			if (Entry.SourceMesh == Mesh)
			{
				return Entry.ReactionActorClass;
			}
		}
		return nullptr;
	}
};
