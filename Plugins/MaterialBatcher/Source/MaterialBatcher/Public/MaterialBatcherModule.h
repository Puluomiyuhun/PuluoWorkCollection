// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMaterialBatcherModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** 注册到关卡编辑器 Actor 右键菜单 */
	void RegisterMenus();

	/** 菜单点击入口：对当前选中的 Actor 执行合批 */
	void OnBatchSelectedActor();
};
