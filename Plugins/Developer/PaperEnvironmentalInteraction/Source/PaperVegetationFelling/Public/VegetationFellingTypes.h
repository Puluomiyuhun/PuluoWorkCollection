// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InstanceDataTypes.h"
#include "VegetationFellingTypes.generated.h"

/** 砍断实例的运行时状态 */
UENUM(BlueprintType)
enum class EVegetationFellingPhase : uint8
{
	/** 物理模拟倒下中 */
	Falling,
	/** 保留值：旧的"已落地"阶段。现流程不再做落地判断，Falling 到时直接进 Dissolving，不会进入此状态 */
	Grounded,
	/** Dissolve 消失中 */
	Dissolving,
	/** 完成，等待回收 */
	Finished,
};

/** 砍断请求参数（游戏线程） */
USTRUCT(BlueprintType)
struct PAPERVEGETATIONFELLING_API FVegetationFellingRequest
{
	GENERATED_BODY()

	/** 砍断点世界坐标 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector CutWorldPosition = FVector::ZeroVector;

	/** 砍击方向（用于施加初始冲量） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector StrikeDirection = FVector::ForwardVector;

	/** 砍击力度（冲量大小） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float StrikeImpulse = 5000.f;

	/**
	 * Source ISKM component. Optional: when set (with a valid InstanceId) the
	 * subsystem removes that instance from the ISKM and reads its transform. When
	 * null the instance is treated as already removed by the caller (e.g. a static
	 * ISM instance) and InstanceWorldTransform is used instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UInstancedSkinnedMeshComponent> SourceISKM = nullptr;

	/** 被砍断的实例 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FPrimitiveInstanceId InstanceId;

	/**
	 * Explicit world transform for the felled instance, used when SourceISKM is null
	 * (the caller already removed a static ISM instance). Ignored when SourceISKM is
	 * valid - the transform is then read straight from the ISKM.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform InstanceWorldTransform = FTransform::Identity;

	/** 砍断骨骼名称（在此骨骼处截断并启用物理） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName CutBoneName = NAME_None;
};

/** 砍断配置数据 */
USTRUCT(BlueprintType)
struct PAPERVEGETATIONFELLING_API FVegetationFellingConfig
{
	GENERATED_BODY()

	/** 可被砍断的骨骼名称列表（预定义的合法砍断点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> ValidCutBoneNames;

	/**
	 * 从斩断瞬间起到开始溶解的时间（秒）。物理会全程模拟倒下过程，但溶解不做任何
	 * 落地判断/落地计时——从斩断那一刻直接计时，到点就开始溶解，无论上半段是否已停稳。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float TimeBeforeDissolve = 2.5f;

	/** Dissolve 持续时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1"))
	float DissolveDuration = 1.0f;

	/**
	 * 是否长期保留树桩（下半段）。为 true 时上半段溶解消失后，
	 * 该 Actor 不回收，仅隐藏上半段、树桩持续留在场景中。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bPersistStump = true;

	/** 物理模拟的线性阻尼（防止倒下过快） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="10.0"))
	float LinearDamping = 0.5f;

	/** 物理模拟的角阻尼（防止旋转过快） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="10.0"))
	float AngularDamping = 0.5f;

	/**
	 * 砍断瞬间施加的倒伏角速度（度/秒），绕"垂直于砍击方向的水平轴"旋转，
	 * 让上半段像伐木一样朝砍击方向翻倒，而不是原地直上直下掉落后依旧站立。
	 * 0 = 不施加倒伏旋转（退回纯线性推）。竹子较高可适当加大。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float ToppleAngularSpeed = 150.f;

	/**
	 * 砍断瞬间沿砍击方向（水平）把整段竹子击飞的初速度（cm/s）。
	 * 施加到上半段的所有物理 body 上，让它整体朝砍击方向飞出去，而不是原地掉落。
	 * 0 = 不击飞。想要更明显的击飞感就调大（如 600~1200）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float LaunchSpeed = 500.f;

	/**
	 * 裁剪面相对砍断骨骼局部 Z 的上抬量（单位：竹子局部空间），纯手调。
	 * 裁剪面 = CutBoneLocalZ + CutClipHeightBias。
	 * 往上调直到过渡带（连接处的拉伸缝）完全被裁掉；刚性蒙皮可设 0。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float CutClipHeightBias = 0.0f;

	/**
	 * 关闭碰撞的判定余量（单位：竹子局部空间），与渲染裁剪解耦。
	 * 上部整体作为一个刚体在原地模拟。判定按碰撞形状的最高点 ShapeMaxZ：
	 * 只有整个形状都在切面之下（ShapeMaxZ < 裁剪面 + CutCollisionMargin）的碰撞体
	 * 才关闭碰撞——这种体完全不可见，留着只会在站立时撑地。
	 * 任何有可见部分（跨切面或在切面之上）的体都保留碰撞，否则倒下躺平后
	 * 切口附近的可见躯干下方没有支撑会穿地。
	 * 默认 0：只关完全不可见的体。若某个跨切面的体在站立时仍撑地，可略微调大
	 * 把判定面上移、把它也关掉。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0"))
	float CutCollisionMargin = 0.0f;
};
