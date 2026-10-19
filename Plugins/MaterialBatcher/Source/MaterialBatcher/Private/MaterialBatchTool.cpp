// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialBatchTool.h"

#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "ImageUtils.h"
#include "ImageCore.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MaterialBatchTool"

namespace
{
	/** 每个纹理参数对应的一组 slot 贴图（index 与 slot index 对齐，可能为 null） */
	struct FTextureParamData
	{
		FName ParamName;
		TArray<UTexture2D*> PerSlotTextures;
		// 从首个有效贴图继承的属性
		bool bSRGB = true;
		TEnumAsByte<TextureCompressionSettings> Compression = TC_Default;
	};

	/** 把纹理的编辑器源数据读成 BGRA8 FColor 数组。支持任意源格式（G8/G16/BGRA8/RGBA16 等）。 */
	bool ReadTextureBGRA(UTexture2D* Tex, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
	{
		if (!Tex)
		{
			return false;
		}

		FTextureSource& Src = Tex->Source;
		if (!Src.IsValid())
		{
			return false;
		}

		// GetMipImage 能解出任意源格式的 mip0；灰度图/法线/HDR 都可读
		FImage SrcImage;
		if (!Src.GetMipImage(SrcImage, 0))
		{
			return false;
		}

		OutW = SrcImage.SizeX;
		OutH = SrcImage.SizeY;

		// 转成 BGRA8。目标 gamma 沿用源图 gamma，保持编码值不变：
		//  - sRGB 彩色图(BaseColor) -> BGRA8 sRGB
		//  - 线性灰度图(Roughness/Metallic/AO) -> BGRA8 Linear，值复制到 RGB
		//  - 法线 -> BGRA8 Linear
		FImage BgraImage;
		SrcImage.CopyTo(BgraImage, ERawImageFormat::BGRA8, SrcImage.GetGammaSpace());

		TArrayView64<const FColor> Colors = BgraImage.AsBGRA8();
		OutPixels.SetNumUninitialized(Colors.Num());
		FMemory::Memcpy(OutPixels.GetData(), Colors.GetData(), Colors.Num() * sizeof(FColor));
		return true;
	}

	/** 创建一个资产存放用的包，返回长包名（/Game/...） */
	FString MakeUniqueAssetPath(const FString& BasePackagePath, const FString& BaseName, FString& OutAssetName)
	{
		const FAssetToolsModule& AssetToolsModule =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

		FString PackageName;
		AssetToolsModule.Get().CreateUniqueAssetName(
			BasePackagePath / BaseName, TEXT(""), PackageName, OutAssetName);
		return PackageName;
	}

	/** 用 FColor 像素生成一个新的 UTexture2D 资产 */
	UTexture2D* CreateTextureAsset(const FString& PackagePath, const FString& BaseName,
		int32 W, int32 H, const TArray<FColor>& Pixels, bool bSRGB, TextureCompressionSettings Compression)
	{
		FString AssetName;
		const FString PackageName = MakeUniqueAssetPath(PackagePath, BaseName, AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		UTexture2D* NewTex = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
		NewTex->Source.Init(W, H, 1, 1, TSF_BGRA8, (const uint8*)Pixels.GetData());
		NewTex->SRGB = bSRGB;
		NewTex->CompressionSettings = Compression;
		NewTex->MipGenSettings = TMGS_FromTextureGroup;
		NewTex->UpdateResource();
		NewTex->PostEditChange();

		FAssetRegistryModule::AssetCreated(NewTex);
		Package->MarkPackageDirty();
		return NewTex;
	}
}

void FMaterialBatchTool::Run(AStaticMeshActor* Actor, FMaterialBatchResult& OutResult)
{
	OutResult.bSuccess = false;

	// ---- 1) 基础校验 ----
	if (!Actor || !Actor->GetStaticMeshComponent())
	{
		OutResult.Message = TEXT("无效的 Actor。");
		return;
	}

	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	UStaticMesh* SrcMesh = Comp->GetStaticMesh();
	if (!SrcMesh)
	{
		OutResult.Message = TEXT("该 Actor 没有 Static Mesh。");
		return;
	}

	const TArray<FStaticMaterial>& SrcMaterials = SrcMesh->GetStaticMaterials();
	const int32 NumSlots = SrcMaterials.Num();
	if (NumSlots < 2)
	{
		OutResult.Message = TEXT("材质槽少于 2 个，无需合批。");
		return;
	}

	// ---- 2) 校验共享同一母材质，且都是材质实例 ----
	UMaterial* SharedBase = nullptr;
	TArray<UMaterialInstance*> SlotInstances;
	SlotInstances.SetNum(NumSlots);

	for (int32 i = 0; i < NumSlots; ++i)
	{
		UMaterialInterface* MatIf = SrcMaterials[i].MaterialInterface;
		UMaterialInstance* MI = Cast<UMaterialInstance>(MatIf);
		if (!MI)
		{
			OutResult.Message = FString::Printf(
				TEXT("槽 %d 的材质不是材质实例(Material Instance)，无法合批。"), i);
			return;
		}
		UMaterial* Base = MI->GetBaseMaterial();
		if (!SharedBase)
		{
			SharedBase = Base;
		}
		else if (SharedBase != Base)
		{
			OutResult.Message = TEXT("并非所有材质实例都共享同一个母材质，无法合批。");
			return;
		}
		SlotInstances[i] = MI;
	}

	if (!SharedBase)
	{
		OutResult.Message = TEXT("未能确定母材质。");
		return;
	}

	// ---- 3) 枚举母材质的纹理参数，逐槽收集贴图 ----
	TArray<FMaterialParameterInfo> TexParamInfos;
	TArray<FGuid> TexParamIds;
	SharedBase->GetAllTextureParameterInfo(TexParamInfos, TexParamIds);

	if (TexParamInfos.Num() == 0)
	{
		OutResult.Message = TEXT("母材质没有可合批的纹理参数(Texture Parameter)。");
		return;
	}

	TArray<FTextureParamData> ParamDatas;
	for (const FMaterialParameterInfo& Info : TexParamInfos)
	{
		FTextureParamData PD;
		PD.ParamName = Info.Name;
		PD.PerSlotTextures.SetNum(NumSlots);

		bool bAnyValid = false;
		for (int32 s = 0; s < NumSlots; ++s)
		{
			UTexture* Tex = nullptr;
			SlotInstances[s]->GetTextureParameterValue(Info, Tex);
			UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
			PD.PerSlotTextures[s] = Tex2D;
			if (Tex2D && !bAnyValid)
			{
				PD.bSRGB = Tex2D->SRGB;
				PD.Compression = Tex2D->CompressionSettings;
				bAnyValid = true;
			}
		}

		if (bAnyValid)
		{
			ParamDatas.Add(PD);
		}
	}

	if (ParamDatas.Num() == 0)
	{
		OutResult.Message = TEXT("材质实例上没有覆盖任何纹理，无需合批。");
		return;
	}

	// ---- 4) 计算图集网格 ----
	const int32 GridDim = FMath::CeilToInt(FMath::Sqrt((float)NumSlots)); // NxN 能容纳 NumSlots 个 cell
	const int32 CellSize = 1024; // 每个 cell 的分辨率（可按需调整）
	const int32 AtlasSize = GridDim * CellSize;

	// 资产落地路径：与源 Mesh 同目录下的 Batched 子目录
	const FString SrcPackagePath = FPackageName::GetLongPackagePath(SrcMesh->GetOutermost()->GetName());
	const FString OutPath = SrcPackagePath / TEXT("Batched");

	// ---- 5) 为每个纹理参数生成图集贴图 ----
	// SlotCell: slot s 放在网格 (s % GridDim, s / GridDim)
	auto SlotToCell = [GridDim](int32 Slot, int32& CX, int32& CY)
	{
		CX = Slot % GridDim;
		CY = Slot / GridDim;
	};

	// 新材质实例先创建，随后填纹理
	UMaterialInstanceConstantFactoryNew* MICFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
	MICFactory->InitialParent = SharedBase;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FString MICAssetName;
	const FString MICPackageName = MakeUniqueAssetPath(OutPath, SrcMesh->GetName() + TEXT("_Batched_MIC"), MICAssetName);

	UMaterialInstanceConstant* NewMIC = Cast<UMaterialInstanceConstant>(
		AssetToolsModule.Get().CreateAsset(MICAssetName, FPackageName::GetLongPackagePath(MICPackageName),
			UMaterialInstanceConstant::StaticClass(), MICFactory));
	if (!NewMIC)
	{
		OutResult.Message = TEXT("创建新材质实例失败。");
		return;
	}

	// 从首个 slot 复制标量/向量覆盖参数，并检测各槽是否一致
	bool bParamMismatch = false;
	{
		TArray<FMaterialParameterInfo> ScalarInfos; TArray<FGuid> ScalarIds;
		SharedBase->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
		for (const FMaterialParameterInfo& Info : ScalarInfos)
		{
			float V0 = 0.f; bool bHas0 = SlotInstances[0]->GetScalarParameterValue(Info, V0);
			for (int32 s = 1; s < NumSlots; ++s)
			{
				float Vs = 0.f; SlotInstances[s]->GetScalarParameterValue(Info, Vs);
				if (!FMath::IsNearlyEqual(V0, Vs)) { bParamMismatch = true; }
			}
			if (bHas0) { NewMIC->SetScalarParameterValueEditorOnly(Info, V0); }
		}

		TArray<FMaterialParameterInfo> VecInfos; TArray<FGuid> VecIds;
		SharedBase->GetAllVectorParameterInfo(VecInfos, VecIds);
		for (const FMaterialParameterInfo& Info : VecInfos)
		{
			FLinearColor V0; bool bHas0 = SlotInstances[0]->GetVectorParameterValue(Info, V0);
			for (int32 s = 1; s < NumSlots; ++s)
			{
				FLinearColor Vs; SlotInstances[s]->GetVectorParameterValue(Info, Vs);
				if (!V0.Equals(Vs)) { bParamMismatch = true; }
			}
			if (bHas0) { NewMIC->SetVectorParameterValueEditorOnly(Info, V0); }
		}
	}

	// 逐纹理参数生成图集
	for (const FTextureParamData& PD : ParamDatas)
	{
		TArray<FColor> Atlas;
		Atlas.SetNumZeroed((int64)AtlasSize * AtlasSize);

		for (int32 s = 0; s < NumSlots; ++s)
		{
			UTexture2D* Tex = PD.PerSlotTextures[s];
			TArray<FColor> CellPixels;
			CellPixels.SetNumZeroed(CellSize * CellSize);

			if (Tex)
			{
				TArray<FColor> SrcPixels; int32 SW = 0, SH = 0;
				if (ReadTextureBGRA(Tex, SrcPixels, SW, SH))
				{
					// 最后一个参数 bForceOpaqueOutput=false：保留 alpha，避免破坏 opacity/mask 贴图
					FImageUtils::ImageResize(SW, SH, SrcPixels, CellSize, CellSize, CellPixels, PD.bSRGB, /*bForceOpaqueOutput*/ false);
				}
			}

			int32 CX, CY; SlotToCell(s, CX, CY);
			const int32 DstX0 = CX * CellSize;
			const int32 DstY0 = CY * CellSize;
			for (int32 y = 0; y < CellSize; ++y)
			{
				FColor* DstRow = &Atlas[(int64)(DstY0 + y) * AtlasSize + DstX0];
				const FColor* SrcRow = &CellPixels[(int64)y * CellSize];
				FMemory::Memcpy(DstRow, SrcRow, CellSize * sizeof(FColor));
			}
		}

		UTexture2D* AtlasTex = CreateTextureAsset(OutPath,
			SrcMesh->GetName() + TEXT("_Atlas_") + PD.ParamName.ToString(),
			AtlasSize, AtlasSize, Atlas, PD.bSRGB, PD.Compression);

		if (AtlasTex)
		{
			NewMIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(PD.ParamName), AtlasTex);
		}
	}
	NewMIC->PostEditChange();

	// ---- 6) 复制 Mesh，重映射 UV 到 cell，并合并材质槽 ----
	FString NewMeshAssetName;
	const FString NewMeshPackageName = MakeUniqueAssetPath(OutPath, SrcMesh->GetName() + TEXT("_Batched"), NewMeshAssetName);
	UPackage* MeshPackage = CreatePackage(*NewMeshPackageName);

	UStaticMesh* NewMesh = DuplicateObject<UStaticMesh>(SrcMesh, MeshPackage, *NewMeshAssetName);
	NewMesh->SetFlags(RF_Public | RF_Standalone);

	FMeshDescription* MD = NewMesh->GetMeshDescription(0);
	if (!MD)
	{
		OutResult.Message = TEXT("无法取得 Mesh Description。");
		return;
	}

	FStaticMeshAttributes Attributes(*MD);
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> GroupSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	// 建立 polygon group -> slot index 映射
	// MeshDescription 存的是 ImportedMaterialSlotName，优先按它匹配，再退回 MaterialSlotName
	auto SlotIndexFromName = [&SrcMaterials](FName SlotName) -> int32
	{
		for (int32 i = 0; i < SrcMaterials.Num(); ++i)
		{
			if (SrcMaterials[i].ImportedMaterialSlotName == SlotName ||
				SrcMaterials[i].MaterialSlotName == SlotName)
			{
				return i;
			}
		}
		return INDEX_NONE;
	};

	// 遍历每个多边形，按其所属 group 的 slot 重映射该多边形所有顶点实例的 UV0
	for (const FPolygonGroupID GroupID : MD->PolygonGroups().GetElementIDs())
	{
		const FName SlotName = GroupSlotNames[GroupID];
		int32 SlotIdx = SlotIndexFromName(SlotName);
		if (SlotIdx == INDEX_NONE)
		{
			SlotIdx = GroupID.GetValue(); // 兜底：用 group 序号
		}

		int32 CX, CY; SlotToCell(SlotIdx, CX, CY);
		const float Scale = 1.0f / (float)GridDim;
		const FVector2f Offset((float)CX * Scale, (float)CY * Scale);

		for (const FPolygonID PolyID : MD->GetPolygonGroupPolygonIDs(GroupID))
		{
			for (const FVertexInstanceID VIID : MD->GetPolygonVertexInstances(PolyID))
			{
				FVector2f UV = UVs.Get(VIID, 0);
				// 假设原 UV 在 [0,1]；缩放并平移到 cell
				UV = UV * Scale + Offset;
				UVs.Set(VIID, 0, UV);
			}
		}
	}

	// 合并所有 polygon group 到一个：把所有多边形移动到第一个 group，再删空 group
	{
		TArray<FPolygonGroupID> AllGroups;
		for (const FPolygonGroupID GroupID : MD->PolygonGroups().GetElementIDs())
		{
			AllGroups.Add(GroupID);
		}

		if (AllGroups.Num() > 0)
		{
			const FPolygonGroupID KeepGroup = AllGroups[0];
			for (int32 g = 1; g < AllGroups.Num(); ++g)
			{
				const FPolygonGroupID SrcGroup = AllGroups[g];
				// 先把该 group 的多边形收集到临时数组（遍历时会改动归属，避免迭代失效）
				TArray<FPolygonID> Polys;
				for (const FPolygonID PolyID : MD->GetPolygonGroupPolygonIDs(SrcGroup))
				{
					Polys.Add(PolyID);
				}
				for (const FPolygonID PolyID : Polys)
				{
					MD->SetPolygonPolygonGroup(PolyID, KeepGroup);
				}
			}
			for (int32 g = AllGroups.Num() - 1; g >= 1; --g)
			{
				MD->DeletePolygonGroup(AllGroups[g]);
			}
		}
	}

	NewMesh->CommitMeshDescription(0);

	// 设置单一材质槽
	TArray<FStaticMaterial> NewStaticMaterials;
	NewStaticMaterials.Add(FStaticMaterial(NewMIC, FName(TEXT("BatchedMat"))));
	NewMesh->SetStaticMaterials(NewStaticMaterials);

	// 重建 section 映射 -> 全部指向 slot 0
	if (NewMesh->GetRenderData())
	{
		// PostEditChange 会依据 MeshDescription 重建；单 group 情况下自然是 1 个 section
	}
	NewMesh->PostEditChange();

	FAssetRegistryModule::AssetCreated(NewMesh);
	MeshPackage->MarkPackageDirty();

	// ---- 7) 让选中 Actor 指向新 Mesh ----
	{
		const FScopedTransaction Transaction(LOCTEXT("BatchApply", "应用材质合批"));
		Comp->Modify();
		Comp->SetStaticMesh(NewMesh);
		Comp->SetMaterial(0, NewMIC);
		Comp->MarkRenderStateDirty();
	}

	OutResult.bSuccess = true;
	OutResult.Message = FString::Printf(
		TEXT("合批完成！\n- 材质槽 %d -> 1\n- 图集网格 %dx%d, 每格 %dpx\n- 新资产位于: %s\n%s"),
		NumSlots, GridDim, GridDim, CellSize, *OutPath,
		bParamMismatch ? TEXT("\n⚠ 警告：各材质实例的标量/向量参数不一致，已采用第一个槽的值，效果可能有差异。") : TEXT(""));
}

#undef LOCTEXT_NAMESPACE
