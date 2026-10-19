// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UTexture2D;

/** 合批执行结果 */
struct FMaterialBatchResult
{
	bool bSuccess = false;
	FString Message;
};

/**
 * 材质/贴图合批的核心逻辑。
 *
 * 前提约束（不满足会中止并给出提示）：
 *  - 选中的是 StaticMeshActor，且 StaticMesh 有效；
 *  - 该 Mesh 使用了 2 个以上材质槽；
 *  - 所有材质均为共享同一“母材质”(base UMaterial) 的 UMaterialInstance；
 *  - 各材质实例的标量/向量参数一致（否则无法用单一材质表达，只处理贴图差异）。
 *
 * 流程：
 *  1) 收集每个材质槽引用的贴图（按母材质的纹理参数名归类）；
 *  2) 计算图集网格（NxN cell），把每个槽的贴图缩放后写入对应 cell，生成新 UTexture2D；
 *  3) 创建一个新的 UMaterialInstanceConstant，绑定图集贴图；
 *  4) 复制 StaticMesh，将所有面重映射到单一材质槽，并把每个面的 UV 缩放/平移到其原槽对应的 cell；
 *  5) 让选中的 Actor 指向新 Mesh + 新材质。
 */
class FMaterialBatchTool
{
public:
	static void Run(AStaticMeshActor* Actor, FMaterialBatchResult& OutResult);
};
