// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FoliageImpactTypes.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UPhysicalMaterial;

/**
 * Tuning for how a knocked-away foliage instance flies and disappears.
 *
 * The motion is driven by a skeletal-mesh RAGDOLL: the spawned actor turns its
 * skeletal mesh into an articulated rigid body (physics asset) and is given an
 * initial launch velocity + spin, then the engine's bodies + constraints handle
 * the arc, tumbling, ground collision AND folding/flopping - so it actually
 * deforms on impact instead of staying a stiff block. This requires the foliage
 * to have a skeletal mesh + physics asset (assigned on the KnockedFoliageActor
 * Blueprint). Damping here is applied uniformly on top of the physics asset.
 */
USTRUCT(BlueprintType)
struct PAPERVEGETATIONFELLING_API FFoliageImpactConfig
{
	GENERATED_BODY()

	/** 沿击飞方向（水平）的初速度（cm/s），作为刚体初速度施加 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float LaunchSpeed = 600.f;

	/** 竖直方向的初速度（cm/s），让它先弹起来再由物理重力落下 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float UpwardLaunchSpeed = 300.f;

	/**
	 * 翻滚角速度（度/秒），作为刚体初始角速度，绕"垂直于击飞方向的水平轴"翻转，
	 * 像被踢飞一样前翻。之后由物理接管（碰撞/阻尼会改变它）。0 = 不主动翻滚。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float SpinSpeed = 540.f;

	/**
	 * 翻滚速度的随机浮动比例（0~1）。让一片植被飞起来时各不相同，不至于整齐划一。
	 * 例如 0.3 表示每个实例的翻滚速度在 [0.7,1.3]×SpinSpeed 间随机。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float SpinRandomVariance = 0.35f;

	/**
	 * 击飞方向的随机水平散射角（度）。让一片植被朝略微不同的方向飞散，更自然。
	 * 0 = 全部严格沿击飞方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="90.0"))
	float DirectionSpreadDegrees = 20.f;

	/** 刚体线性阻尼（空气阻力感），越大飞得越"钝"。0 = 无阻尼 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float LinearDamping = 0.5f;

	/** 刚体角阻尼，越大翻滚衰减越快 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float AngularDamping = 0.5f;

	/**
	 * 物理材质覆盖（可选）。用来集中控制落地手感，不必逐个网格资产去设：
	 * - Restitution 调低（0）去掉"弹跳"感；
	 * - Friction 调高让它落地就贴住、少打滑；
	 * 留空则用网格 BodySetup 自带的物理材质。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UPhysicalMaterial> PhysicalMaterialOverride = nullptr;

	/**
	 * 飞行（物理模拟）阶段时长（秒）。到时后冻结刚体并进入淡出阶段。
	 * 取一个略大于"飞出去落稳"的值即可。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float FlyDuration = 1.5f;

	/** 淡出时长（秒）。期间物理已冻结，用 DissolveProgress 材质参数从 0→1 溶解消失 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05"))
	float FadeDuration = 0.6f;
};

/**
 * A single knock-away request handed to the subsystem. The instance has usually
 * already been removed from its source ISM by the caller; this carries just what
 * the spawned actor needs to look right and where/how to fly. Deliberately does
 * NOT reference the source ISM, so the flying actor is fully decoupled from it.
 */
USTRUCT(BlueprintType)
struct PAPERVEGETATIONFELLING_API FFoliageImpactRequest
{
	GENERATED_BODY()

	/** 要显示的静态网格（从来源 ISM 拷贝） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** 各材质槽覆盖（从来源 ISM 拷贝，可为空表示用网格默认材质） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** 该实例被击飞时的世界变换（位置/旋转/缩放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform InstanceWorldTransform = FTransform::Identity;

	/** 击飞方向（会自动取水平分量并归一化） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector ImpactDirection = FVector::ForwardVector;

	/** 击飞强度倍率，乘到 Config 的速度/翻滚上（1 = 原样） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float ImpactStrength = 1.0f;
};
