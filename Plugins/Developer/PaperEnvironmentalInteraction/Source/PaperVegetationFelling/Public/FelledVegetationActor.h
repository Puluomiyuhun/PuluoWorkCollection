// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VegetationFellingTypes.h"
#include "FelledVegetationActor.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;

/**
 * 被砍断的植被 Actor（物理模拟版本）。
 *
 * 从 ISKM 中 RemoveInstance 后在同位置 Spawn，包含两个 SkeletalMeshComponent：
 *   - LowerPartMesh：砍断点以下（树桩），静态或可选保留风动
 *   - UpperPartMesh：砍断点以上，启用物理模拟自然倒下
 *
 * 生命周期：Spawn → InitializeFelling（斩断瞬间）→ 物理模拟倒下（自斩断计时 TimeBeforeDissolve）→ Dissolving → Finished → 回池/Destroy
 */
UCLASS(BlueprintType, Blueprintable)
class PAPERVEGETATIONFELLING_API AFelledVegetationActor : public AActor
{
	GENERATED_BODY()

public:
	AFelledVegetationActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 初始化砍断状态。由 FellingSubsystem 在 Spawn 后立即调用。
	 *
	 * @param Request  砍断请求（包含源 ISKM、实例索引、砍断点、击打方向等）
	 * @param InstanceTransform  原始实例的世界变换
	 */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Felling")
	void InitializeFelling(const FVegetationFellingRequest& Request, const FTransform& InstanceTransform);

	/** 当前砍断阶段 */
	UFUNCTION(BlueprintPure, Category="Vegetation|Felling")
	EVegetationFellingPhase GetCurrentPhase() const { return CurrentPhase; }

	/** 是否已完成全部流程（可回收） */
	UFUNCTION(BlueprintPure, Category="Vegetation|Felling")
	bool IsFinished() const { return CurrentPhase == EVegetationFellingPhase::Finished; }

	/** 获取砍断配置 */
	UFUNCTION(BlueprintPure, Category="Vegetation|Felling")
	const FVegetationFellingConfig& GetFellingConfig() const { return FellingConfig; }

	/** 上半段溶解后是否应作为持久树桩保留（不回池） */
	UFUNCTION(BlueprintPure, Category="Vegetation|Felling")
	bool ShouldPersistStump() const { return FellingConfig.bPersistStump; }

	/** BP 上配置的斩断后骨骼网格体（见 CutSkeletalMesh 属性说明） */
	USkeletalMesh* GetCutSkeletalMesh() const { return CutSkeletalMesh; }

	/** 重置 Actor 状态（对象池回收时调用） */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Felling")
	void ResetForPooling();

protected:
	virtual void BeginPlay() override;

	/** 各阶段 Tick */
	void TickFalling(float DeltaSeconds);
	void TickDissolving(float DeltaSeconds);

	/** 进入下一阶段 */
	void TransitionToPhase(EVegetationFellingPhase NewPhase);

	/** 蓝图事件：阶段变化时触发 */
	UFUNCTION(BlueprintImplementableEvent, Category="Vegetation|Felling")
	void OnPhaseChanged(EVegetationFellingPhase OldPhase, EVegetationFellingPhase NewPhase);

	/** 蓝图事件：倒下完成，开始 Dissolve 前的回调（可在此播放 VFX/SFX） */
	UFUNCTION(BlueprintImplementableEvent, Category="Vegetation|Felling")
	void OnGroundImpact();

protected:
	// ---- 组件 ----

	/** 下半段骨骼网格（残桩，静态） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Felling|Components")
	TObjectPtr<USkeletalMeshComponent> LowerPartMesh;

	/** 上半段骨骼网格（倒下段，物理模拟） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Felling|Components")
	TObjectPtr<USkeletalMeshComponent> UpperPartMesh;

	// ---- 配置 ----

	/**
	 * Post-cut skeletal mesh (must ship with a Physics Asset). When set, the felled
	 * plant is displayed with THIS mesh regardless of the source instance type, so a
	 * plant placed in the scene as a static ISM (for WPO wind) can still be cut. When
	 * left null the actor falls back to the source ISKM's own skeletal mesh (large
	 * plants such as bamboo authored directly as ISKM for bone wind).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Felling|Config")
	TObjectPtr<USkeletalMesh> CutSkeletalMesh = nullptr;

	/** 砍断配置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Felling|Config")
	FVegetationFellingConfig FellingConfig;

	// ---- 运行时状态 ----

	/** 当前阶段 */
	EVegetationFellingPhase CurrentPhase = EVegetationFellingPhase::Falling;

	/** 砍断请求参数缓存 */
	FVegetationFellingRequest CachedRequest;

	/** 阶段内已经过的时间 */
	float PhaseElapsedTime = 0.f;

	/** Dissolve 进度（0-1） */
	float DissolveProgress = 0.f;
};
