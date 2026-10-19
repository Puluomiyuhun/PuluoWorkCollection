// Copyright PaperGame. All Rights Reserved.

#include "FoliageImpactBlueprintLibrary.h"
#include "FoliageImpactSubsystem.h"

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
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"

int32 UFoliageImpactBlueprintLibrary::KnockFoliageInFrontOfCharacter(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	float Distance,
	float HalfAngleDegrees,
	float ImpactStrength)
{
	if (!WorldContextObject || !PlayerController)
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

	UFoliageImpactSubsystem* Subsystem = World->GetSubsystem<UFoliageImpactSubsystem>();
	if (!Subsystem)
	{
		return 0;
	}

	const FVector CharLocation = Pawn->GetActorLocation();
	const FVector CharForwardFlat = FVector(Pawn->GetActorForwardVector().X, Pawn->GetActorForwardVector().Y, 0.f).GetSafeNormal();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
	const float DistanceSq = FMath::Square(Distance);

	int32 TotalKnocked = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> ISMs;
		It->GetComponents<UInstancedStaticMeshComponent>(ISMs);

		for (UInstancedStaticMeshComponent* ISM : ISMs)
		{
			if (!ISM || ISM->GetInstanceCount() == 0)
			{
				continue;
			}

			UStaticMesh* Mesh = ISM->GetStaticMesh();
			if (!Mesh)
			{
				continue;
			}

			// Cache the ISM's material overrides once for this component.
			TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
			const int32 NumMaterials = ISM->GetNumMaterials();
			MaterialOverrides.Reserve(NumMaterials);
			for (int32 SlotIndex = 0; SlotIndex < NumMaterials; ++SlotIndex)
			{
				MaterialOverrides.Add(ISM->GetMaterial(SlotIndex));
			}

			// Collect instances inside the horizontal cone. Capture transforms NOW
			// (indices are valid) and defer the actual removal to a single batch
			// call so shifting indices never bite us.
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

				const FVector ToInstance = InstanceTransform.GetLocation() - CharLocation;
				const FVector ToInstanceFlat = FVector(ToInstance.X, ToInstance.Y, 0.f);

				if (ToInstanceFlat.SizeSquared() > DistanceSq)
				{
					continue;
				}

				const FVector ToInstanceDir = ToInstanceFlat.GetSafeNormal();
				if (FVector::DotProduct(CharForwardFlat, ToInstanceDir) < CosHalfAngle)
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

			// Spawn a flying actor for each hit (transforms already captured).
			for (const FTransform& HitTransform : HitTransforms)
			{
				FFoliageImpactRequest Request;
				Request.Mesh = Mesh;
				Request.MaterialOverrides = MaterialOverrides;
				Request.InstanceWorldTransform = HitTransform;
				Request.ImpactDirection = CharForwardFlat;
				Request.ImpactStrength = ImpactStrength;

				if (Subsystem->RequestImpact(Request))
				{
					++TotalKnocked;
				}
			}

			// Remove the hit instances from the source ISM in one batch.
			ISM->RemoveInstances(IndicesToRemove);
		}

		// Also scan skinned instance components (ISKM). Knocking is source-agnostic:
		// the flying actor shows its own BP-configured skeletal mesh, so an ISKM
		// source only needs to supply the instance transform.
		TArray<UInstancedSkinnedMeshComponent*> ISKMs;
		It->GetComponents<UInstancedSkinnedMeshComponent>(ISKMs);

		for (UInstancedSkinnedMeshComponent* ISKM : ISKMs)
		{
			if (!ISKM || ISKM->GetInstanceCount() == 0)
			{
				continue;
			}

			// Cache the ISKM's material overrides once for this component.
			TArray<TObjectPtr<UMaterialInterface>> ISKMMaterials;
			const int32 NumISKMMaterials = ISKM->GetNumMaterials();
			ISKMMaterials.Reserve(NumISKMMaterials);
			for (int32 SlotIndex = 0; SlotIndex < NumISKMMaterials; ++SlotIndex)
			{
				ISKMMaterials.Add(ISKM->GetMaterial(SlotIndex));
			}

			// Collect instances inside the horizontal cone by stable instance id
			// (removal swaps indices, so ids are the safe handle).
			TArray<TPair<FPrimitiveInstanceId, FTransform>> ISKMHits;

			const int32 ISKMInstanceCount = ISKM->GetInstanceCount();
			for (int32 i = 0; i < ISKMInstanceCount; ++i)
			{
				const FPrimitiveInstanceId Id = ISKM->GetInstanceId(i);
				FTransform InstanceTransform;
				if (!ISKM->GetInstanceTransform(Id, InstanceTransform, /*bWorldSpace=*/ true))
				{
					continue;
				}

				const FVector ToInstance = InstanceTransform.GetLocation() - CharLocation;
				const FVector ToInstanceFlat = FVector(ToInstance.X, ToInstance.Y, 0.f);

				if (ToInstanceFlat.SizeSquared() > DistanceSq)
				{
					continue;
				}

				const FVector ToInstanceDir = ToInstanceFlat.GetSafeNormal();
				if (FVector::DotProduct(CharForwardFlat, ToInstanceDir) < CosHalfAngle)
				{
					continue;
				}

				ISKMHits.Add({Id, InstanceTransform});
			}

			// Spawn a flying actor for each hit, then remove the source instance.
			for (const auto& [InstanceId, HitTransform] : ISKMHits)
			{
				FFoliageImpactRequest Request;
				Request.Mesh = nullptr;
				Request.MaterialOverrides = ISKMMaterials;
				Request.InstanceWorldTransform = HitTransform;
				Request.ImpactDirection = CharForwardFlat;
				Request.ImpactStrength = ImpactStrength;

				if (Subsystem->RequestImpact(Request))
				{
					++TotalKnocked;
				}

				ISKM->RemoveInstance(InstanceId);
			}
		}
	}

	return TotalKnocked;
}
