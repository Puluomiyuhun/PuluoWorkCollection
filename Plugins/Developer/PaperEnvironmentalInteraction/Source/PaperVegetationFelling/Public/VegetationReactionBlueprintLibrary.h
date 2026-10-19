// Copyright PaperGame. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VegetationReactionBlueprintLibrary.generated.h"

class APlayerController;
class UVegetationReactionTable;

/**
 * Blueprint 入口：数据驱动的植被破坏分发。
 *
 * 单一入口扫描角色前方锥形范围内的所有 ISM/ISKM 实例，按每个实例的源网格查映射表
 * （UVegetationReactionTable）决定触发哪种反应：映射到 AFelledVegetationActor 子类
 * 则斩断，映射到 AKnockedFoliageActor 子类则击飞。未在表中登记的网格保持不动。
 * 破坏方式与实例类型（ISM/ISKM）彻底解耦，四象限组合均可配置。
 */
UCLASS()
class PAPERVEGETATIONFELLING_API UVegetationReactionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 破坏角色前方锥形范围内的植被实例，按映射表逐实例分发斩断/击飞。
	 *
	 * @param WorldContextObject  世界上下文
	 * @param PlayerController     发起攻击的玩家
	 * @param ReactionTable        源网格 -> 反应 BP 映射表
	 * @param Distance             作用距离（cm，水平）
	 * @param HalfAngleDegrees     锥形半角（度），如 60 = 120° 扇形
	 * @param StrikeImpulse        斩断冲量（传给斩断请求）
	 * @param ImpactStrength       击飞强度倍率（传给击飞请求）
	 * @param CutHeightOffset      斩断点相对角色的高度偏移（cm）
	 * @return 触发反应的实例总数
	 */
	UFUNCTION(BlueprintCallable, Category="Vegetation|Reaction", meta=(WorldContext="WorldContextObject"))
	static int32 DestroyVegetationInFrontOfCharacter(
		const UObject* WorldContextObject,
		APlayerController* PlayerController,
		const UVegetationReactionTable* ReactionTable,
		float Distance = 300.f,
		float HalfAngleDegrees = 60.f,
		float StrikeImpulse = 5000.f,
		float ImpactStrength = 1.0f,
		float CutHeightOffset = 50.f);
};
