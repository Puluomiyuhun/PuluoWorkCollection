// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FoliageImpactTypes.h"
#include "KnockedFoliageActor.generated.h"

class USkeletalMeshComponent;

/** 击飞植被的生命周期阶段 */
UENUM(BlueprintType)
enum class EFoliageImpactPhase : uint8
{
	/** 弹道飞行中 */
	Flying,
	/** 淡出（Dissolve 溶解）中 */
	Fading,
	/** 完成，等待回收 */
	Finished,
};

/**
 * 被击飞的植被 Actor（骨骼 ragdoll 版本，用于需要落地弯折/瘫软的植被）。
 *
 * 从来源 ISM 移除某个实例后，在原位置 Spawn 本 Actor。本 Actor 用一个
 * SkeletalMeshComponent 显示【预先在 BP 上配好的骨骼版网格 + Physics Asset】，
 * 击飞时开启全身物理(ragdoll)并给一个初始速度 + 角速度，之后由引擎多 body +
 * 约束模拟飞行/翻滚/落地/弯折/瘫软。ISM 里放的仍是廉价静态网格，只用来提供落点；
 * 本 Actor 的骨骼网格与来源 ISM 完全解耦（设计方式 a：每种植被一个 BP 子类）。
 * 不做切断。飞行计时结束后通过 DissolveProgress 材质参数溶解消失并回池。
 *
 * 生命周期：Spawn → InitializeImpact（击飞瞬间，开 ragdoll）→ Flying（FlyDuration，物理模拟）
 *          → Fading（FadeDuration 淡出，物理继续/自然沉睡）→ Finished → 回池
 */
UCLASS(BlueprintType, Blueprintable)
class PAPERVEGETATIONFELLING_API AKnockedFoliageActor : public AActor
{
	GENERATED_BODY()

public:
	AKnockedFoliageActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 初始化击飞状态。由 FoliageImpactSubsystem 在 Spawn/取池后立即调用。
	 *
	 * @param Request  击飞请求（网格、材质、世界变换、方向、强度）
	 * @param Config   击飞手感配置
	 */
	UFUNCTION(BlueprintCallable, Category="Foliage|Impact")
	void InitializeImpact(const FFoliageImpactRequest& Request, const FFoliageImpactConfig& Config);

	/** 当前阶段 */
	UFUNCTION(BlueprintPure, Category="Foliage|Impact")
	EFoliageImpactPhase GetCurrentPhase() const { return CurrentPhase; }

	/** 是否已完成（可回收） */
	UFUNCTION(BlueprintPure, Category="Foliage|Impact")
	bool IsFinished() const { return CurrentPhase == EFoliageImpactPhase::Finished; }

	/** 重置状态（对象池回收时调用） */
	UFUNCTION(BlueprintCallable, Category="Foliage|Impact")
	void ResetForPooling();

protected:
	/** 各阶段 Tick */
	void TickFlying(float DeltaSeconds);
	void TickFading(float DeltaSeconds);

	/** 进入下一阶段 */
	void TransitionToPhase(EFoliageImpactPhase NewPhase);

	/** 蓝图事件：阶段变化时触发（可在此播放 VFX/SFX） */
	UFUNCTION(BlueprintImplementableEvent, Category="Foliage|Impact")
	void OnPhaseChanged(EFoliageImpactPhase OldPhase, EFoliageImpactPhase NewPhase);

protected:
	// ---- 组件 ----

	/** 显示用骨骼网格（根组件，开启全身 ragdoll 物理）。骨骼网格 + Physics Asset 在 BP 上配置 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Foliage|Components")
	TObjectPtr<USkeletalMeshComponent> MeshComponent;

	// ---- 配置 ----

	/** 击飞手感配置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Foliage|Config")
	FFoliageImpactConfig ImpactConfig;

	// ---- 运行时状态 ----

	/** 当前阶段 */
	EFoliageImpactPhase CurrentPhase = EFoliageImpactPhase::Flying;

	/** 阶段内已经过时间（秒） */
	float PhaseElapsedTime = 0.f;
};
