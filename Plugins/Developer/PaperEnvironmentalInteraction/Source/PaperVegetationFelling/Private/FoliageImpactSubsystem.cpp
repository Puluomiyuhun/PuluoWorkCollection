// Copyright PaperGame. All Rights Reserved.

#include "FoliageImpactSubsystem.h"
#include "KnockedFoliageActor.h"
#include "Engine/World.h"

DECLARE_STATS_GROUP(TEXT("FoliageImpact"), STATGROUP_FoliageImpact, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Foliage Impact Subsystem Tick"), STAT_FoliageImpactTick, STATGROUP_FoliageImpact);

// --------------------------------------------------------------------------
// USubsystem Interface
// --------------------------------------------------------------------------

void UFoliageImpactSubsystem::Deinitialize()
{
	for (AKnockedFoliageActor* Actor : ActiveActors)
	{
		if (Actor && !Actor->IsPendingKillPending())
		{
			Actor->Destroy();
		}
	}
	ActiveActors.Empty();

	for (AKnockedFoliageActor* Actor : ActorPool)
	{
		if (Actor && !Actor->IsPendingKillPending())
		{
			Actor->Destroy();
		}
	}
	ActorPool.Empty();

	Super::Deinitialize();
}

bool UFoliageImpactSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

TStatId UFoliageImpactSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFoliageImpactSubsystem, STATGROUP_FoliageImpact);
}

// --------------------------------------------------------------------------
// Tick
// --------------------------------------------------------------------------

void UFoliageImpactSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageImpactTick);

	CleanupFinishedActors();
}

// --------------------------------------------------------------------------
// 核心流程
// --------------------------------------------------------------------------

AKnockedFoliageActor* UFoliageImpactSubsystem::RequestImpact(
	const FFoliageImpactRequest& Request,
	TSubclassOf<AKnockedFoliageActor> OverrideActorClass)
{
	// Request.Mesh is only the source static mesh (used for optional material copy);
	// the knocked actor displays its own BP-configured skeletal mesh, so an ISKM
	// source (no static mesh) is valid. No guard on Mesh here.

	// 同屏上限：超出则回收最老的一个
	if (ActiveActors.Num() >= MaxActiveFoliage && ActiveActors.Num() > 0)
	{
		AKnockedFoliageActor* OldestActor = ActiveActors[0];
		ActiveActors.RemoveAt(0);
		ReleaseActor(OldestActor);
	}

	// 优先用映射表指定的类，否则回退默认类
	const TSubclassOf<AKnockedFoliageActor> SpawnClass = OverrideActorClass ? OverrideActorClass : KnockedFoliageActorClass;
	AKnockedFoliageActor* Actor = AcquireActor(SpawnClass);
	if (!Actor)
	{
		return nullptr;
	}

	Actor->InitializeImpact(Request, DefaultImpactConfig);

	ActiveActors.Add(Actor);
	return Actor;
}

// --------------------------------------------------------------------------
// 对象池
// --------------------------------------------------------------------------

AKnockedFoliageActor* UFoliageImpactSubsystem::AcquireActor(TSubclassOf<AKnockedFoliageActor> Class)
{
	UWorld* World = GetWorld();
	if (!World || !Class)
	{
		return nullptr;
	}

	// 优先从对象池获取一个同类的空闲 Actor（不同映射类各自复用，互不串用）
	for (int32 i = ActorPool.Num() - 1; i >= 0; --i)
	{
		AKnockedFoliageActor* Actor = ActorPool[i];
		if (Actor && !Actor->IsPendingKillPending() && Actor->GetClass() == Class)
		{
			ActorPool.RemoveAt(i);
			Actor->SetActorHiddenInGame(false);
			Actor->SetActorTickEnabled(true);
			return Actor;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AKnockedFoliageActor>(Class, FTransform::Identity, SpawnParams);
}

void UFoliageImpactSubsystem::ReleaseActor(AKnockedFoliageActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	Actor->ResetForPooling();
	ActorPool.Add(Actor);
}

void UFoliageImpactSubsystem::CleanupFinishedActors()
{
	for (int32 i = ActiveActors.Num() - 1; i >= 0; --i)
	{
		AKnockedFoliageActor* Actor = ActiveActors[i];
		if (Actor && Actor->IsFinished())
		{
			ActiveActors.RemoveAt(i);
			ReleaseActor(Actor);
		}
	}
}
