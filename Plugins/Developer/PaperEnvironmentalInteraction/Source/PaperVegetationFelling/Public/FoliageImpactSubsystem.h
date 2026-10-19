// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FoliageImpactTypes.h"
#include "FoliageImpactSubsystem.generated.h"

class AKnockedFoliageActor;

/**
 * 植被击飞管理子系统（通用于任意 ISM 小植被/道具）。
 *
 * 与砍伐子系统同处 PaperVegetationFelling 模块，但职责独立：
 * - 接收击飞请求，取池/Spawn 一个 KnockedFoliageActor 在原位置把实例整体打飞
 * - 管理 KnockedFoliageActor 对象池，避免频繁 Spawn/Destroy
 * - 控制同屏击飞上限
 *
 * 注意：本子系统不负责从 ISM 移除实例——调用方（如 BlueprintLibrary 的锥形范围
 * 查询）在采集到实例世界变换后自行批量 RemoveInstances，再把变换交给本子系统。
 * 这样飞出去的 Actor 与来源 ISM 完全解耦。
 */
UCLASS()
class PAPERVEGETATIONFELLING_API UFoliageImpactSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem Interface

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	/**
	 * 请求把一个（已从 ISM 移除的）植被实例整体击飞。
	 *
	 * @param Request  击飞请求（网格、材质、世界变换、方向、强度）
	 * @param OverrideActorClass 指定要 Spawn 的 KnockedFoliageActor 类（数据驱动分发时用映射表里的类）。
	 *                           为空则回退到子系统默认的 KnockedFoliageActorClass。
	 * @return 创建的 KnockedFoliageActor，因上限/配置缺失未能创建则返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category="Foliage|Impact")
	AKnockedFoliageActor* RequestImpact(
		const FFoliageImpactRequest& Request,
		TSubclassOf<AKnockedFoliageActor> OverrideActorClass = nullptr);

	/** 获取当前活跃的击飞 Actor 数量 */
	UFUNCTION(BlueprintPure, Category="Foliage|Impact")
	int32 GetActiveFoliageCount() const { return ActiveActors.Num(); }

	/** 同屏最大击飞数量（超出后回收最老的） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage|Config")
	int32 MaxActiveFoliage = 32;

	/** KnockedFoliageActor 蓝图类（用于 Spawn，可在此挂 Niagara/音效等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage|Config")
	TSubclassOf<AKnockedFoliageActor> KnockedFoliageActorClass;

	/** 默认击飞手感配置（RequestImpact 未显式指定时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage|Config")
	FFoliageImpactConfig DefaultImpactConfig;

protected:
	/** 从对象池获取或 Spawn 新的 KnockedFoliageActor（按类匹配复用） */
	AKnockedFoliageActor* AcquireActor(TSubclassOf<AKnockedFoliageActor> Class);

	/** 回收 Actor 到对象池 */
	void ReleaseActor(AKnockedFoliageActor* Actor);

	/** 清理已完成的 Actor */
	void CleanupFinishedActors();

private:
	/** 当前活跃的击飞 Actor */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AKnockedFoliageActor>> ActiveActors;

	/** 对象池（已完成、待复用的 Actor） */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AKnockedFoliageActor>> ActorPool;
};
