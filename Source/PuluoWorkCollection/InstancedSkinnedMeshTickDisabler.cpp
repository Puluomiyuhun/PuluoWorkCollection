// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedSkinnedMeshTickDisabler.h"
#include "Components/SkinnedMeshComponent.h"


UInstancedSkinnedMeshTickDisabler::UInstancedSkinnedMeshTickDisabler()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInstancedSkinnedMeshTickDisabler::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		DisableInstancedSkinnedMeshTicks(Owner);
	}
}

void UInstancedSkinnedMeshTickDisabler::DisableInstancedSkinnedMeshTicks(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	// 获取类类型（通过名称查找，避免直接 include 头文件）
	static UClass* ISMClass = nullptr;
	if (!ISMClass)
	{
		ISMClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.InstancedSkinnedMeshComponent"));
	}

	if (ISMClass)
	{
		TArray<UActorComponent*> Components;
		InActor->GetComponents(Components);

		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->IsA(ISMClass))
			{
				Comp->SetComponentTickEnabled(false);
			}
		}
	}

	// 递归处理所有子 Actor
	TArray<AActor*> ChildActors;
	InActor->GetAttachedActors(ChildActors);

	for (AActor* Child : ChildActors)
	{
		DisableInstancedSkinnedMeshTicks(Child);
	}
}
