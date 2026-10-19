// Copyright PaperGame. All Rights Reserved.

#include "VegetationReactionBlueprintLibrary.h"
#include "VegetationReactionTable.h"

#include "VegetationFellingSubsystem.h"
#include "FelledVegetationActor.h"
#include "FoliageImpactSubsystem.h"
#include "KnockedFoliageActor.h"

#include "Components/InstancedStaticMeshComponent.h"

// UInstancedSkinnedMeshComponent 标记了 UE_EXPERIMENTAL，
// 在 UE_VALIDATE_EXPERIMENTAL_API=1 时展开为 [[deprecated(...)]]，
// MSVC 在该上下文中报 C3837 错误，需要临时取消该宏的展开
#pragma push_macro("UE_EXPERIMENTAL")
#undef UE_EXPERIMENTAL
#define UE_EXPERIMENTAL(Version, Message)
#include "Components/InstancedSkinnedMeshComponent.h"
#pragma pop_macro("UE_EXPERIMENTAL")

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"

int32 UVegetationReactionBlueprintLibrary::DestroyVegetationInFrontOfCharacter(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	const UVegetationReactionTable* ReactionTable,
	float Distance,
	float HalfAngleDegrees,
	float StrikeImpulse,
	float ImpactStrength,
	float CutHeightOffset)
{
	if (!WorldContextObject || !PlayerController || !ReactionTable)
	{
		return 0;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return 0;
	}

	APawn* Pawn = PlayerController->GetPawn();
	if (!Pawn)
	{
		return 0;
	}

	UVegetationFellingSubsystem* FellingSubsystem = World->GetSubsystem<UVegetationFellingSubsystem>();
	UFoliageImpactSubsystem* ImpactSubsystem = World->GetSubsystem<UFoliageImpactSubsystem>();
	if (!FellingSubsystem || !ImpactSubsystem)
	{
		return 0;
	}

	const FVector CharLocation = Pawn->GetActorLocation();
	const FVector CharForward = Pawn->GetActorForwardVector();
	const FVector CharForwardFlat = FVector(CharForward.X, CharForward.Y, 0.f).GetSafeNormal();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
	const float DistanceSq = FMath::Square(Distance);

	const FVector CutWorldPosition = CharLocation + FVector(0, 0, CutHeightOffset) + CharForward * (Distance * 0.5f);
	const FVector StrikeDirection = CharForward;

	int32 TotalDestroyed = 0;

	// Small helper: is this hit (world transform) inside the horizontal cone?
	auto IsInCone = [&](const FVector& InstanceLocation) -> bool
	{
		const FVector ToInstanceFlat = FVector(InstanceLocation.X - CharLocation.X, InstanceLocation.Y - CharLocation.Y, 0.f);
		if (ToInstanceFlat.SizeSquared() > DistanceSq)
		{
			return false;
		}
		return FVector::DotProduct(CharForwardFlat, ToInstanceFlat.GetSafeNormal()) >= CosHalfAngle;
	};

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		// ---- Static instances (ISM) ----
		TArray<UInstancedStaticMeshComponent*> ISMs;
		It->GetComponents<UInstancedStaticMeshComponent>(ISMs);

		for (UInstancedStaticMeshComponent* ISM : ISMs)
		{
			if (!ISM || ISM->GetInstanceCount() == 0)
			{
				continue;
			}

			UStaticMesh* SourceMesh = ISM->GetStaticMesh();
			const TSubclassOf<AActor> ReactionClass = ReactionTable->FindReactionClass(SourceMesh);
			if (!ReactionClass)
			{
				continue; // Not registered -> leave untouched.
			}

			const bool bFell = ReactionClass->IsChildOf(AFelledVegetationActor::StaticClass());
			const bool bKnock = ReactionClass->IsChildOf(AKnockedFoliageActor::StaticClass());
			if (!bFell && !bKnock)
			{
				UE_LOG(LogTemp, Warning, TEXT("VegetationReaction: ReactionClass %s is neither a felled nor a knocked actor"), *ReactionClass->GetName());
				continue;
			}

			// For felling a static instance we need the class's authored post-cut mesh.
			const USkeletalMesh* CutMesh = nullptr;
			const TArray<FName>* ValidBones = nullptr;
			if (bFell)
			{
				if (const AFelledVegetationActor* CDO = Cast<AFelledVegetationActor>(ReactionClass->GetDefaultObject()))
				{
					CutMesh = CDO->GetCutSkeletalMesh();
					ValidBones = &CDO->GetFellingConfig().ValidCutBoneNames;
				}
				if (!CutMesh)
				{
					UE_LOG(LogTemp, Warning, TEXT("VegetationReaction: fell class %s has no CutSkeletalMesh; cannot fell static mesh %s"),
						*ReactionClass->GetName(), SourceMesh ? *SourceMesh->GetName() : TEXT("NULL"));
					continue;
				}
			}

			// Cache material overrides once for knock display.
			TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
			if (bKnock)
			{
				const int32 NumMaterials = ISM->GetNumMaterials();
				MaterialOverrides.Reserve(NumMaterials);
				for (int32 SlotIndex = 0; SlotIndex < NumMaterials; ++SlotIndex)
				{
					MaterialOverrides.Add(ISM->GetMaterial(SlotIndex));
				}
			}

			// Collect cone hits (capture transforms now; batch-remove after dispatch).
			TArray<int32> IndicesToRemove;
			TArray<FTransform> HitTransforms;

			const int32 InstanceCount = ISM->GetInstanceCount();
			for (int32 i = 0; i < InstanceCount; ++i)
			{
				FTransform InstanceTransform;
				if (!ISM->GetInstanceTransform(i, InstanceTransform, /*bWorldSpace=*/ true))
				{
					continue;
				}
				if (!IsInCone(InstanceTransform.GetLocation()))
				{
					continue;
				}
				IndicesToRemove.Add(i);
				HitTransforms.Add(InstanceTransform);
			}

			if (HitTransforms.Num() == 0)
			{
				continue;
			}

			for (const FTransform& HitTransform : HitTransforms)
			{
				if (bFell)
				{
					const FName CutBone = FellingSubsystem->FindClosestCutBone(CutMesh, HitTransform, CutWorldPosition, *ValidBones);

					FVegetationFellingRequest Request;
					Request.CutWorldPosition = CutWorldPosition;
					Request.StrikeDirection = StrikeDirection;
					Request.StrikeImpulse = StrikeImpulse;
					Request.SourceISKM = nullptr;
					Request.InstanceWorldTransform = HitTransform;
					Request.CutBoneName = CutBone;

					if (FellingSubsystem->RequestFelling(Request, ReactionClass.Get()))
					{
						++TotalDestroyed;
					}
				}
				else // bKnock
				{
					FFoliageImpactRequest Request;
					Request.Mesh = SourceMesh;
					Request.MaterialOverrides = MaterialOverrides;
					Request.InstanceWorldTransform = HitTransform;
					Request.ImpactDirection = CharForwardFlat;
					Request.ImpactStrength = ImpactStrength;

					if (ImpactSubsystem->RequestImpact(Request, ReactionClass.Get()))
					{
						++TotalDestroyed;
					}
				}
			}

			// ISM instances are never removed by the subsystems, so remove them here.
			ISM->RemoveInstances(IndicesToRemove);
		}

		// ---- Skinned instances (ISKM) ----
		TArray<UInstancedSkinnedMeshComponent*> ISKMs;
		It->GetComponents<UInstancedSkinnedMeshComponent>(ISKMs);

		for (UInstancedSkinnedMeshComponent* ISKM : ISKMs)
		{
			if (!ISKM || ISKM->GetInstanceCount() == 0)
			{
				continue;
			}

			USkeletalMesh* SourceMesh = Cast<USkeletalMesh>(ISKM->GetSkinnedAsset());
			const TSubclassOf<AActor> ReactionClass = ReactionTable->FindReactionClass(SourceMesh);
			if (!ReactionClass)
			{
				continue;
			}

			const bool bFell = ReactionClass->IsChildOf(AFelledVegetationActor::StaticClass());
			const bool bKnock = ReactionClass->IsChildOf(AKnockedFoliageActor::StaticClass());
			if (!bFell && !bKnock)
			{
				UE_LOG(LogTemp, Warning, TEXT("VegetationReaction: ReactionClass %s is neither a felled nor a knocked actor"), *ReactionClass->GetName());
				continue;
			}

			// For felling: the working mesh mirrors AFelledVegetationActor::InitializeFelling
			// (BP-authored CutSkeletalMesh wins, else the source skeletal mesh itself).
			const USkeletalMesh* WorkingMesh = SourceMesh;
			const TArray<FName>* ValidBones = nullptr;
			if (bFell)
			{
				if (const AFelledVegetationActor* CDO = Cast<AFelledVegetationActor>(ReactionClass->GetDefaultObject()))
				{
					if (USkeletalMesh* ClassCutMesh = CDO->GetCutSkeletalMesh())
					{
						WorkingMesh = ClassCutMesh;
					}
					ValidBones = &CDO->GetFellingConfig().ValidCutBoneNames;
				}
			}

			// Cache material overrides once for knock display.
			TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
			if (bKnock)
			{
				const int32 NumMaterials = ISKM->GetNumMaterials();
				MaterialOverrides.Reserve(NumMaterials);
				for (int32 SlotIndex = 0; SlotIndex < NumMaterials; ++SlotIndex)
				{
					MaterialOverrides.Add(ISKM->GetMaterial(SlotIndex));
				}
			}

			// Collect cone hits by stable instance id (removal swaps indices).
			TArray<TPair<FPrimitiveInstanceId, FTransform>> Hits;
			const int32 InstanceCount = ISKM->GetInstanceCount();
			for (int32 i = 0; i < InstanceCount; ++i)
			{
				const FPrimitiveInstanceId Id = ISKM->GetInstanceId(i);
				FTransform InstanceTransform;
				if (!ISKM->GetInstanceTransform(Id, InstanceTransform, /*bWorldSpace=*/ true))
				{
					continue;
				}
				if (!IsInCone(InstanceTransform.GetLocation()))
				{
					continue;
				}
				Hits.Add({Id, InstanceTransform});
			}

			for (const auto& [InstanceId, HitTransform] : Hits)
			{
				if (bFell)
				{
					const FName CutBone = FellingSubsystem->FindClosestCutBone(
						WorkingMesh, HitTransform, CutWorldPosition, ValidBones ? *ValidBones : TArray<FName>());

					FVegetationFellingRequest Request;
					Request.CutWorldPosition = CutWorldPosition;
					Request.StrikeDirection = StrikeDirection;
					Request.StrikeImpulse = StrikeImpulse;
					Request.SourceISKM = ISKM; // subsystem reads transform AND removes this instance
					Request.InstanceId = InstanceId;
					Request.CutBoneName = CutBone;

					if (FellingSubsystem->RequestFelling(Request, ReactionClass.Get()))
					{
						++TotalDestroyed;
					}
					// Do NOT remove here: RequestFelling already removed the ISKM instance.
				}
				else // bKnock
				{
					FFoliageImpactRequest Request;
					Request.Mesh = nullptr; // skinned source has no static mesh
					Request.MaterialOverrides = MaterialOverrides;
					Request.InstanceWorldTransform = HitTransform;
					Request.ImpactDirection = CharForwardFlat;
					Request.ImpactStrength = ImpactStrength;

					if (ImpactSubsystem->RequestImpact(Request, ReactionClass.Get()))
					{
						++TotalDestroyed;
					}
					// Impact subsystem never removes; caller removes the ISKM instance.
					ISKM->RemoveInstance(InstanceId);
				}
			}
		}
	}

	return TotalDestroyed;
}
