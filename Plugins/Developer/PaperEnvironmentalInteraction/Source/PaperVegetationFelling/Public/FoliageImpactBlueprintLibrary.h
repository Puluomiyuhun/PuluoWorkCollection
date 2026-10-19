// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FoliageImpactBlueprintLibrary.generated.h"

class APlayerController;

/**
 * Blueprint 入口：把角色前方锥形范围内的 ISM 植被实例整体击飞。
 */
UCLASS()
class PAPERVEGETATIONFELLING_API UFoliageImpactBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 击飞角色前方锥形范围内的植被实例（近战横扫感）。
	 * 遍历世界中所有 UInstancedStaticMeshComponent（含 ISM/HISM/Foliage），
	 * 命中的实例从 ISM 中移除，并在原位置 Spawn 一个飞出去的 KnockedFoliageActor。
	 *
	 * @param WorldContextObject  世界上下文
	 * @param PlayerController     发起攻击的玩家
	 * @param Distance             作用距离（cm，水平）
	 * @param HalfAngleDegrees     锥形半角（度），如 60 = 120° 扇形
	 * @param ImpactStrength       击飞强度倍率（乘到子系统 Config 的速度/翻滚上）
	 * @return 被击飞的实例数量
	 */
	UFUNCTION(BlueprintCallable, Category="Foliage|Impact", meta=(WorldContext="WorldContextObject"))
	static int32 KnockFoliageInFrontOfCharacter(
		const UObject* WorldContextObject,
		APlayerController* PlayerController,
		float Distance = 300.f,
		float HalfAngleDegrees = 60.f,
		float ImpactStrength = 1.0f);
};
