// Copyright PaperGame. All Rights Reserved.

#include "VegetationFellingBlueprintLibrary.h"
#include "VegetationFellingSubsystem.h"
#include "FelledVegetationActor.h"

#pragma push_macro("UE_EXPERIMENTAL")
#undef UE_EXPERIMENTAL
#define UE_EXPERIMENTAL(Version, Message)
#include "Components/InstancedSkinnedMeshComponent.h"
#pragma pop_macro("UE_EXPERIMENTAL")

#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"

int32 UVegetationFellingBlueprintLibrary::FellVegetationInFrontOfCharacter(
	const UObject* WorldContextObject,
	APlayerController* PlayerController,
	float Distance,
	float HalfAngleDegrees,
	float StrikeImpulse,
	float CutHeightOffset)
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

	UVegetationFellingSubsystem* Subsystem = World->GetSubsystem<UVegetationFellingSubsystem>();
	if (!Subsystem)
	{
		return 0;
	}

	// Character position and forward direction (yaw only, ignore pitch)
	const FVector CharLocation = Pawn->GetActorLocation();
	const FVector CharForward = Pawn->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
	const float DistanceSq = FMath::Square(Distance);

	// Cut world position = character location + height offset + forward * half distance
	// (approximate cut point in front of the character)
	const FVector CutWorldPosition = CharLocation + FVector(0, 0, CutHeightOffset) + CharForward * (Distance * 0.5f);
	const FVector StrikeDirection = CharForward;

	int32 TotalFelled = 0;

	// Post-cut skeletal mesh authored on the FelledActor CDO. Static ISM instances
	// carry no skeleton, so the cut-bone search runs against this mesh instead.
	const USkeletalMesh* CutMesh = nullptr;
	if (Subsystem->FelledActorClass)
	{
		if (const AFelledVegetationActor* CDO = Subsystem->FelledActorClass.GetDefaultObject())
		{
			CutMesh = CDO->GetCutSkeletalMesh();
		}
	}

	const FVector CharForwardFlatShared = FVector(CharForward.X, CharForward.Y, 0.f).GetSafeNormal();

	// Iterate all actors looking for ISKM components
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UInstancedSkinnedMeshComponent*> ISKMs;
		It->GetComponents<UInstancedSkinnedMeshComponent>(ISKMs);

		for (UInstancedSkinnedMeshComponent* ISKM : ISKMs)
		{
			if (!ISKM || ISKM->GetInstanceCount() == 0)
			{
				continue;
			}

			const USkeletalMesh* Mesh = Cast<USkeletalMesh>(ISKM->GetSkinnedAsset());
			const int32 InstanceCount = ISKM->GetInstanceCount();

			// Collect instances within the cone
			TArray<FPrimitiveInstanceId> InstancesToFell;

			for (int32 i = 0; i < InstanceCount; ++i)
			{
				FPrimitiveInstanceId Id = ISKM->GetInstanceId(i);
				FTransform InstanceTransform;
				if (!ISKM->GetInstanceTransform(Id, InstanceTransform, /*bWorldSpace=*/ true))
				{
					continue;
				}

				const FVector InstanceLocation = InstanceTransform.GetLocation();
				const FVector ToInstance = InstanceLocation - CharLocation;

				// Distance check (horizontal only to avoid height differences rejecting nearby trees)
				const FVector ToInstanceFlat = FVector(ToInstance.X, ToInstance.Y, 0.f);
				if (ToInstanceFlat.SizeSquared() > DistanceSq)
				{
					continue;
				}

				// Cone angle check (in 2D, horizontal plane)
				const FVector CharForwardFlat = FVector(CharForward.X, CharForward.Y, 0.f).GetSafeNormal();
				const FVector ToInstanceDir = ToInstanceFlat.GetSafeNormal();
				const float Dot = FVector::DotProduct(CharForwardFlat, ToInstanceDir);

				if (Dot < CosHalfAngle)
				{
					continue;
				}

				InstancesToFell.Add(Id);
			}

			// Fell each instance in the cone
			for (const FPrimitiveInstanceId& InstanceId : InstancesToFell)
			{
				FTransform InstanceTransform;
				ISKM->GetInstanceTransform(InstanceId, InstanceTransform, true);

				FName CutBone = Subsystem->FindClosestCutBone(Mesh, InstanceTransform, CutWorldPosition, TArray<FName>());

				FVegetationFellingRequest Request;
				Request.CutWorldPosition = CutWorldPosition;
				Request.StrikeDirection = StrikeDirection;
				Request.StrikeImpulse = StrikeImpulse;
				Request.SourceISKM = ISKM;
				Request.InstanceId = InstanceId;
				Request.CutBoneName = CutBone;

				AFelledVegetationActor* FelledActor = Subsystem->RequestFelling(Request);
				if (FelledActor)
				{
					TotalFelled++;
				}
			}
		}

		// Also scan static instance components (ISM). Felling is source-agnostic:
		// the felled actor swaps in its own BP-authored post-cut skeletal mesh, so
		// a static ISM instance only needs to supply the world transform. The caller
		// removes the instance here and passes the transform explicitly.
		TArray<UInstancedStaticMeshComponent*> ISMs;
		It->GetComponents<UInstancedStaticMeshComponent>(ISMs);

		for (UInstancedStaticMeshComponent* ISM : ISMs)
		{
			if (!ISM || ISM->GetInstanceCount() == 0)
			{
				continue;
			}

			// Collect instances within the cone. Capture transforms now (indices are
			// valid) and defer removal to a single batch call.
			TArray<int32> IndicesToRemove;
			TArray<FTransform> HitTransforms;

			const int32 ISMInstanceCount = ISM->GetInstanceCount();
			for (int32 i = 0; i < ISMInstanceCount; ++i)
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
				if (FVector::DotProduct(CharForwardFlatShared, ToInstanceDir) < CosHalfAngle)
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

			// Fell each hit (transforms already captured).
			for (const FTransform& HitTransform : HitTransforms)
			{
				const FName CutBone = Subsystem->FindClosestCutBone(CutMesh, HitTransform, CutWorldPosition, TArray<FName>());

				FVegetationFellingRequest Request;
				Request.CutWorldPosition = CutWorldPosition;
				Request.StrikeDirection = StrikeDirection;
				Request.StrikeImpulse = StrikeImpulse;
				Request.SourceISKM = nullptr;
				Request.InstanceWorldTransform = HitTransform;
				Request.CutBoneName = CutBone;

				AFelledVegetationActor* FelledActor = Subsystem->RequestFelling(Request);
				if (FelledActor)
				{
					TotalFelled++;
				}
			}

			// Remove the hit instances from the source ISM in one batch.
			ISM->RemoveInstances(IndicesToRemove);
		}
	}

	return TotalFelled;
}
