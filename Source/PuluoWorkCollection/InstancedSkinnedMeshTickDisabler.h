// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InstancedSkinnedMeshTickDisabler.generated.h"


// 前向声明，避免直接 include InstancedSkinnedMeshComponent.h
class UInstancedSkinnedMeshComponent;

/**
 * 附加到此组件的 Actor 及其子 Actor 中的所有 UInstancedSkinnedMeshComponent 将被禁用 Tick
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PULUOWORKCOLLECTION_API UInstancedSkinnedMeshTickDisabler : public UActorComponent
{
	GENERATED_BODY()

public:
	UInstancedSkinnedMeshTickDisabler();

protected:
	virtual void BeginPlay() override;

private:
	/** 递归遍历 Actor 层级，禁用所有 InstancedSkinnedMeshComponent 的 Tick */
	void DisableInstancedSkinnedMeshTicks(AActor* InActor);
};
