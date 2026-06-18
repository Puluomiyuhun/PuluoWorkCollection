// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedSkinnedMeshTickManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Components/SkinnedMeshComponent.h"


UClass* UInstancedSkinnedMeshTickManager::CachedISMClass = nullptr;


void UInstancedSkinnedMeshTickManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 缓存 ISM 类引用
	if (!CachedISMClass)
	{
		CachedISMClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.InstancedSkinnedMeshComponent"));
	}

	if (!CachedISMClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("UInstancedSkinnedMeshTickManager: Failed to find InstancedSkinnedMeshComponent class"));
		return;
	}

	// 立即扫描一次
	ScanAndDisableAllISMComponents();

	// 设置定时器，每 0.5 秒扫描一次新创建的 Actor
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(TimerHandle, this, &UInstancedSkinnedMeshTickManager::OnTimerScan, 0.5f, true);
	}

	UE_LOG(LogTemp, Log, TEXT("UInstancedSkinnedMeshTickManager: Initialized, scanning every 0.5s"));
}

void UInstancedSkinnedMeshTickManager::Deinitialize()
{
	// 清除定时器
	UWorld* World = GetWorld();
	if (World && TimerHandle.IsValid())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}

	Super::Deinitialize();
}

void UInstancedSkinnedMeshTickManager::ScanAndDisableAllISMComponents()
{
	UWorld* World = GetWorld();
	if (!World || !CachedISMClass)
	{
		return;
	}

	int32 DisabledCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UActorComponent*> Components;
		It->GetComponents(Components);

		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->IsA(CachedISMClass) && Comp->IsComponentTickEnabled())
			{
				Comp->SetComponentTickEnabled(false);
				DisabledCount++;
			}
		}
	}

	if (DisabledCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("UInstancedSkinnedMeshTickManager: Disabled tick for %d ISM components"), DisabledCount);
	}
}

void UInstancedSkinnedMeshTickManager::OnTimerScan()
{
	ScanAndDisableAllISMComponents();
}

void UInstancedSkinnedMeshTickManager::DisableISMComponentsInActor(AActor* InActor)
{
	if (!InActor || !CachedISMClass)
	{
		return;
	}

	TArray<UActorComponent*> Components;
	InActor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (Comp && Comp->IsA(CachedISMClass))
		{
			Comp->SetComponentTickEnabled(false);
		}
	}

	// 递归处理子 Actor
	TArray<AActor*> ChildActors;
	InActor->GetAttachedActors(ChildActors);

	for (AActor* Child : ChildActors)
	{
		DisableISMComponentsInActor(Child);
	}
}
