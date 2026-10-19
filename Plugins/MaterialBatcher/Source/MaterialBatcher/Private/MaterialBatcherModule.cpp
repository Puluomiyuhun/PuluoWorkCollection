// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialBatcherModule.h"
#include "MaterialBatchTool.h"

#include "ToolMenus.h"
#include "Selection.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "FMaterialBatcherModule"

void FMaterialBatcherModule::StartupModule()
{
	// ToolMenus 在编辑器启动后才可用，用回调延迟注册
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMaterialBatcherModule::RegisterMenus));
}

void FMaterialBatcherModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FMaterialBatcherModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// 挂到关卡视口里选中 Actor 的右键上下文菜单
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu");
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(
		"MaterialBatcher",
		LOCTEXT("MaterialBatcherSection", "Material Batcher"));

	Section.AddMenuEntry(
		"BatchSelectedActor",
		LOCTEXT("BatchSelectedActor", "合批材质与贴图"),
		LOCTEXT("BatchSelectedActorTip", "把选中物体上共享同一母材质的多个材质实例合并为一个，并将贴图拼成图集。"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMaterialBatcherModule::OnBatchSelectedActor)));
}

void FMaterialBatcherModule::OnBatchSelectedActor()
{
	if (!GEditor)
	{
		return;
	}

	// 取当前选中的第一个 StaticMeshActor
	AStaticMeshActor* TargetActor = nullptr;
	if (USelection* Selection = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(*It))
			{
				TargetActor = SMActor;
				break;
			}
		}
	}

	if (!TargetActor)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoActor", "请先在关卡中选中一个 Static Mesh Actor。"));
		return;
	}

	FMaterialBatchResult Result;
	FMaterialBatchTool::Run(TargetActor, Result);

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMaterialBatcherModule, MaterialBatcher)
