// Copyright PaperGame. All Rights Reserved.

#include "VegetationFellingSubsystem.h"
#include "FelledVegetationActor.h"

// UInstancedSkinnedMeshComponent 标记了 UE_EXPERIMENTAL，
// 在 UE_VALIDATE_EXPERIMENTAL_API=1 时展开为 [[deprecated(...)]]，
// MSVC 在该上下文中报 C3837 错误，需要临时取消该宏的展开
#pragma push_macro("UE_EXPERIMENTAL")
#undef UE_EXPERIMENTAL
#define UE_EXPERIMENTAL(Version, Message)
#include "Components/InstancedSkinnedMeshComponent.h"
#pragma pop_macro("UE_EXPERIMENTAL")

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

DECLARE_STATS_GROUP(TEXT("VegetationFelling"), STATGROUP_VegetationFelling, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Felling Subsystem Tick"), STAT_FellingSubsystemTick, STATGROUP_VegetationFelling);

// --------------------------------------------------------------------------
// USubsystem Interface
// --------------------------------------------------------------------------

void UVegetationFellingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UVegetationFellingSubsystem::Deinitialize()
{
	// 清理所有活跃 Actor 和对象池
	for (AFelledVegetationActor* Actor : ActiveActors)
	{
		if (Actor && !Actor->IsPendingKillPending())
		{
			Actor->Destroy();
		}
	}
	ActiveActors.Empty();

	for (AFelledVegetationActor* Actor : ActorPool)
	{
		if (Actor && !Actor->IsPendingKillPending())
		{
			Actor->Destroy();
		}
	}
	ActorPool.Empty();

	for (AFelledVegetationActor* Actor : PersistentStumpActors)
	{
		if (Actor && !Actor->IsPendingKillPending())
		{
			Actor->Destroy();
		}
	}
	PersistentStumpActors.Empty();

	Super::Deinitialize();
}

bool UVegetationFellingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 只在游戏世界中创建
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

TStatId UVegetationFellingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVegetationFellingSubsystem, STATGROUP_VegetationFelling);
}

// --------------------------------------------------------------------------
// Tick
// --------------------------------------------------------------------------

void UVegetationFellingSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_FellingSubsystemTick);

	CleanupFinishedActors();
}

// --------------------------------------------------------------------------
// 核心流程
// --------------------------------------------------------------------------

AFelledVegetationActor* UVegetationFellingSubsystem::RequestFelling(
	const FVegetationFellingRequest& Request,
	TSubclassOf<AFelledVegetationActor> OverrideActorClass)
{
	// SourceISKM valid: subsystem removes the instance and reads its transform.
	// SourceISKM null: caller (e.g. static ISM path) already removed the instance
	// and supplies InstanceWorldTransform directly. In the ISKM case the InstanceId
	// must be valid.
	if (Request.SourceISKM && !Request.InstanceId.IsValid())
	{
		return nullptr;
	}

	// 距离判断：远距离使用简化效果
	if (ShouldUseSimplifiedEffect(Request.CutWorldPosition))
	{
		PlaySimplifiedFellingEffect(Request);
		return nullptr;
	}

	// 同屏上限检查
	if (ActiveActors.Num() >= MaxActiveFelledActors)
	{
		// 回收最老的一个
		if (ActiveActors.Num() > 0)
		{
			AFelledVegetationActor* OldestActor = ActiveActors[0];
			ActiveActors.RemoveAt(0);
			ReleaseFelledActor(OldestActor);
		}
	}

	// Step 1: 解析实例世界变换。ISKM 由子系统移除并读取变换；ISM 由调用方
	// 提前移除并通过 InstanceWorldTransform 直接传入。
	FTransform InstanceTransform;
	if (Request.SourceISKM)
	{
		if (!RemoveInstanceFromISKM(Request.SourceISKM, Request.InstanceId, InstanceTransform))
		{
			return nullptr;
		}
	}
	else
	{
		InstanceTransform = Request.InstanceWorldTransform;
	}

	// Step 2: 获取或创建 FelledActor（优先用映射表指定的类，否则回退默认类）
	const TSubclassOf<AFelledVegetationActor> SpawnClass = OverrideActorClass ? OverrideActorClass : FelledActorClass;
	AFelledVegetationActor* FelledActor = AcquireFelledActor(SpawnClass);
	if (!FelledActor)
	{
		return nullptr;
	}

	// Step 3: 初始化砍断状态
	FelledActor->InitializeFelling(Request, InstanceTransform);

	ActiveActors.Add(FelledActor);
	return FelledActor;
}

// --------------------------------------------------------------------------
// ISKM 实例管理
// --------------------------------------------------------------------------

bool UVegetationFellingSubsystem::RemoveInstanceFromISKM(
	UInstancedSkinnedMeshComponent* ISKM,
	FPrimitiveInstanceId InstanceId,
	FTransform& OutInstanceTransform)
{
	if (!ISKM || !InstanceId.IsValid())
	{
		return false;
	}

	// 获取实例变换
	ISKM->GetInstanceTransform(InstanceId, OutInstanceTransform, /*bWorldSpace=*/ true);

	// 移除实例（使用 RemoveInstance，最后一个实例会交换到被移除的位置）
	ISKM->RemoveInstance(InstanceId);

	return true;
}

// --------------------------------------------------------------------------
// 对象池
// --------------------------------------------------------------------------

AFelledVegetationActor* UVegetationFellingSubsystem::AcquireFelledActor(TSubclassOf<AFelledVegetationActor> Class)
{
	UWorld* World = GetWorld();
	if (!World || !Class)
	{
		return nullptr;
	}

	// 优先从对象池获取一个同类的空闲 Actor（不同映射类各自复用，互不串用）
	for (int32 i = ActorPool.Num() - 1; i >= 0; --i)
	{
		AFelledVegetationActor* Actor = ActorPool[i];
		if (Actor && !Actor->IsPendingKillPending() && Actor->GetClass() == Class)
		{
			ActorPool.RemoveAt(i);
			Actor->SetActorHiddenInGame(false);
			Actor->SetActorTickEnabled(true);
			return Actor;
		}
	}

	// 池中没有同类可用的，Spawn 新的
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AFelledVegetationActor>(Class, FTransform::Identity, SpawnParams);
}

void UVegetationFellingSubsystem::ReleaseFelledActor(AFelledVegetationActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	// 重置 Actor 状态（停止物理、清空数据）
	Actor->ResetForPooling();

	ActorPool.Add(Actor);
}

// --------------------------------------------------------------------------
// 辅助
// --------------------------------------------------------------------------

bool UVegetationFellingSubsystem::ShouldUseSimplifiedEffect(const FVector& CutPosition) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return true;
	}

	const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	const float DistSq = FVector::DistSquared(CameraLocation, CutPosition);
	return DistSq > FMath::Square(SimplifiedEffectDistance);
}

void UVegetationFellingSubsystem::PlaySimplifiedFellingEffect(const FVegetationFellingRequest& Request)
{
	// TODO: 远距离简化效果
	//   - 直接在 ISKM 上将该实例隐藏
	//   - 播放一个 Niagara 粒子效果模拟倒下
	//   - 延迟后恢复实例（可选）
}

void UVegetationFellingSubsystem::CleanupFinishedActors()
{
	for (int32 i = ActiveActors.Num() - 1; i >= 0; --i)
	{
		AFelledVegetationActor* Actor = ActiveActors[i];
		if (Actor && Actor->IsFinished())
		{
			ActiveActors.RemoveAt(i);
			if (Actor->ShouldPersistStump())
			{
				// 上半段已溶解隐藏，仅保留树桩。不回池（回池会 Reset 并隐藏整个
				// Actor，连树桩一起藏掉），改为放入持久列表长期保留。
				PersistentStumpActors.Add(Actor);
			}
			else
			{
				ReleaseFelledActor(Actor);
			}
		}
	}
}

// --------------------------------------------------------------------------
// RequestFellingAtLocation: sphere query nearby ISKM instances
// --------------------------------------------------------------------------

TArray<AFelledVegetationActor*> UVegetationFellingSubsystem::RequestFellingAtLocation(
	UInstancedSkinnedMeshComponent* ISKM,
	const FVector& WorldLocation,
	float Radius,
	const FVector& StrikeDirection,
	float StrikeImpulse)
{
	TArray<AFelledVegetationActor*> Results;

	if (!ISKM)
	{
		return Results;
	}

	// If DefaultFellingConfig has no ValidCutBoneNames, try reading from FelledActorClass CDO
	if (DefaultFellingConfig.ValidCutBoneNames.Num() == 0 && FelledActorClass)
	{
		if (const AFelledVegetationActor* CDO = FelledActorClass.GetDefaultObject())
		{
			DefaultFellingConfig.ValidCutBoneNames = CDO->GetFellingConfig().ValidCutBoneNames;
		}
	}

	const float RadiusSq = FMath::Square(Radius);
	const int32 InstanceCount = ISKM->GetInstanceCount();
	const USkeletalMesh* Mesh = Cast<USkeletalMesh>(ISKM->GetSkinnedAsset());

	// Collect instance IDs within radius (iterate backwards since RemoveInstance swaps)
	TArray<TPair<FPrimitiveInstanceId, FTransform>> Hits;
	for (int32 i = 0; i < InstanceCount; ++i)
	{
		FPrimitiveInstanceId Id = ISKM->GetInstanceId(i);
		FTransform InstanceTransform;
		if (!ISKM->GetInstanceTransform(Id, InstanceTransform, /*bWorldSpace=*/ true))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(InstanceTransform.GetLocation(), WorldLocation);
		if (DistSq <= RadiusSq)
		{
			Hits.Add({Id, InstanceTransform});
		}
	}

	// Process each hit instance
	for (const auto& [InstanceId, InstanceTransform] : Hits)
	{
		// Find the best cut bone for this instance
		FName CutBone = FindClosestCutBone(Mesh, InstanceTransform, WorldLocation, TArray<FName>());
		if (CutBone == NAME_None && DefaultFellingConfig.ValidCutBoneNames.Num() > 0)
		{
			// Fallback: use the first valid bone
			CutBone = DefaultFellingConfig.ValidCutBoneNames[0];
		}

		FVegetationFellingRequest Request;
		Request.CutWorldPosition = WorldLocation;
		Request.StrikeDirection = StrikeDirection;
		Request.StrikeImpulse = StrikeImpulse;
		Request.SourceISKM = ISKM;
		Request.InstanceId = InstanceId;
		Request.CutBoneName = CutBone;

		AFelledVegetationActor* Actor = RequestFelling(Request);
		if (Actor)
		{
			Results.Add(Actor);
		}
	}

	return Results;
}

// --------------------------------------------------------------------------
// FindClosestCutBone: find nearest valid bone to cut position
// --------------------------------------------------------------------------

FName UVegetationFellingSubsystem::FindClosestCutBone(
	const USkeletalMesh* Mesh,
	const FTransform& InstanceTransform,
	const FVector& CutWorldPosition,
	const TArray<FName>& ValidCutBoneNamesOverride)
{
	if (!Mesh)
	{
		return NAME_None;
	}

	// Resolve the valid-bone list: an explicit override wins (data-driven dispatch
	// passes the mapped FelledActor class's own list). Otherwise lazily populate
	// from the default FelledActor CDO so legacy callers keep working.
	const TArray<FName>* ValidBones = &ValidCutBoneNamesOverride;
	if (ValidBones->Num() == 0)
	{
		if (DefaultFellingConfig.ValidCutBoneNames.Num() == 0 && FelledActorClass)
		{
			if (const AFelledVegetationActor* CDO = FelledActorClass.GetDefaultObject())
			{
				DefaultFellingConfig.ValidCutBoneNames = CDO->GetFellingConfig().ValidCutBoneNames;
			}
		}
		ValidBones = &DefaultFellingConfig.ValidCutBoneNames;
	}

	if (ValidBones->Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FelledVegetation FindClosestCutBone: ValidCutBoneNames is empty (check FelledActorClass CDO config)"));
		return NAME_None;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	FName ClosestBone = NAME_None;
	float ClosestDistSq = FLT_MAX;

	for (const FName& BoneName : *ValidBones)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		// Get bone position in component space from ref pose, then transform to world.
		// Walk up the hierarchy to compute the component-space transform.
		FTransform BoneCS = FTransform::Identity;
		int32 CurrentIndex = BoneIndex;
		while (CurrentIndex != INDEX_NONE)
		{
			BoneCS = BoneCS * FTransform(RefSkeleton.GetRefBonePose()[CurrentIndex]);
			CurrentIndex = RefSkeleton.GetParentIndex(CurrentIndex);
		}

		const FVector BoneWorldPos = InstanceTransform.TransformPosition(BoneCS.GetLocation());
		const float DistSq = FVector::DistSquared(BoneWorldPos, CutWorldPosition);

		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			ClosestBone = BoneName;
		}
	}

	return ClosestBone;
}
