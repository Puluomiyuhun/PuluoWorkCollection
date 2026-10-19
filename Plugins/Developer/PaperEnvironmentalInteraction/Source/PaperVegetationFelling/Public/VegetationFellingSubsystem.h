// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VegetationFellingTypes.h"
#include "VegetationFellingSubsystem.generated.h"

class AFelledVegetationActor;
class UInstancedSkinnedMeshComponent;
class USkeletalMesh;

/**
 * 植被砍伐管理子系统。
 *
 * 职责：
 * - 接收砍断请求，协调 ISKM RemoveInstance → Spawn FelledActor 的无缝切换
 * - 管理 FelledActor 对象池，避免频繁 Spawn/Destroy
 * - 控制同屏砍断上限
 * - 处理远距离降级（简化动画替代完整物理）
 */
UCLASS()
class PAPERVEGETATIONFELLING_API UVegetationFellingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem Interface

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	/**
	 * 请求砍断一个植被实例。
	 * 完成 ISKM RemoveInstance → Spawn FelledActor 的全流程。
	 *
	 * @param Request           砍断请求参数
	 * @param OverrideActorClass 指定要 Spawn 的 FelledActor 类（数据驱动分发时用映射表里的类）。
	 *                           为空则回退到子系统默认的 FelledActorClass。
	 * @return 创建的 FelledActor，如果因限制未能创建则返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Felling")
	AFelledVegetationActor* RequestFelling(
		const FVegetationFellingRequest& Request,
		TSubclassOf<AFelledVegetationActor> OverrideActorClass = nullptr);

	/**
	 * 在指定位置球形范围内砍断所有植被实例。
	 * 自动查询 ISKM 中的邻近实例，为每个实例找到最近的合法砍断骨骼。
	 *
	 * @param ISKM             目标 ISKM 组件
	 * @param WorldLocation    砍断中心位置（世界坐标）
	 * @param Radius           查询半径
	 * @param StrikeDirection  砍击方向（施加冲量方向）
	 * @param StrikeImpulse    砍击力度
	 * @return 成功砍断的 Actor 数组
	 */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Felling")
	TArray<AFelledVegetationActor*> RequestFellingAtLocation(
		UInstancedSkinnedMeshComponent* ISKM,
		const FVector& WorldLocation,
		float Radius,
		const FVector& StrikeDirection,
		float StrikeImpulse = 5000.f);

	/**
	 * 在 ValidCutBoneNames 中找到距离 CutWorldPosition 最近的合法骨骼。
	 *
	 * @param Mesh              骨骼网格资产
	 * @param InstanceTransform 实例世界变换
	 * @param CutWorldPosition  砍断点世界坐标
	 * @param ValidCutBoneNamesOverride 合法砍断骨骼名列表。非空时优先使用（数据驱动分发时
	 *                          传入映射类 CDO 自己的列表）；为空则回退到 DefaultFellingConfig。
	 * @return 最近的合法骨骼名称，未找到返回 NAME_None
	 */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Felling")
	FName FindClosestCutBone(
		const USkeletalMesh* Mesh,
		const FTransform& InstanceTransform,
		const FVector& CutWorldPosition,
		const TArray<FName>& ValidCutBoneNamesOverride);

	/** 获取当前活跃的砍断 Actor 数量 */
	UFUNCTION(BlueprintPure, Category="Vegetation|Felling")
	int32 GetActiveFelledActorCount() const { return ActiveActors.Num(); }

	/** 同屏最大砍断数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Felling|Config")
	int32 MaxActiveFelledActors = 8;

	/** 超过此距离的砍断使用简化效果（不 Spawn Actor） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Felling|Config")
	float SimplifiedEffectDistance = 3000.f;

	/** FelledActor 蓝图类（用于 Spawn） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Felling|Config")
	TSubclassOf<AFelledVegetationActor> FelledActorClass;

	/** 默认砍断配置（用于 RequestFellingAtLocation 自动查骨骼时） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Felling|Config")
	FVegetationFellingConfig DefaultFellingConfig;

protected:
	/** 从 ISKM 中移除实例并获取其世界变换 */
	bool RemoveInstanceFromISKM(
		UInstancedSkinnedMeshComponent* ISKM,
		FPrimitiveInstanceId InstanceId,
		FTransform& OutInstanceTransform);

	/** 从对象池获取或 Spawn 新的 FelledActor（按类匹配复用） */
	AFelledVegetationActor* AcquireFelledActor(TSubclassOf<AFelledVegetationActor> Class);

	/** 回收 FelledActor 到对象池 */
	void ReleaseFelledActor(AFelledVegetationActor* Actor);

	/** 判断砍断点距离摄像机是否过远 */
	bool ShouldUseSimplifiedEffect(const FVector& CutPosition) const;

	/** 简化效果：不 Spawn Actor，直接在 ISKM 上播一个消失效果 */
	void PlaySimplifiedFellingEffect(const FVegetationFellingRequest& Request);

	/** 清理已完成的 FelledActor */
	void CleanupFinishedActors();

private:
	/** 当前活跃的砍断 Actor */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFelledVegetationActor>> ActiveActors;

	/** 对象池（已完成、待复用的 FelledActor） */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFelledVegetationActor>> ActorPool;

	/**
	 * 持久保留的树桩 Actor（上半段已溶解隐藏，仅保留下半段树桩）。
	 * 这些 Actor 不回池、不复用，仅在子系统 Deinitialize 时统一销毁。
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFelledVegetationActor>> PersistentStumpActors;
};
