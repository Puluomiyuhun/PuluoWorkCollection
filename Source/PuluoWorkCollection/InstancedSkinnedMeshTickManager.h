// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "InstancedSkinnedMeshTickManager.generated.h"


/**
 * 全局子系统：自动禁用所有 UInstancedSkinnedMeshComponent 的 Tick
 * 无需手动添加组件，游戏启动时自动生效，并持续监控新创建的 Actor
 */
UCLASS()
class PULUOWORKCOLLECTION_API UInstancedSkinnedMeshTickManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 手动触发扫描（可在蓝图中调用） */
	UFUNCTION(BlueprintCallable, Category = "ISM Tick Manager")
	void ScanAndDisableAllISMComponents();

private:
	/** 禁用指定 Actor 及其子 Actor 中所有 ISM 组件的 Tick */
	void DisableISMComponentsInActor(AActor* InActor);

	/** 定时扫描回调 */
	UFUNCTION()
	void OnTimerScan();

	/** 缓存的 ISM 类引用 */
	static UClass* CachedISMClass;

	/** 定时器句柄 */
	FTimerHandle TimerHandle;
};
