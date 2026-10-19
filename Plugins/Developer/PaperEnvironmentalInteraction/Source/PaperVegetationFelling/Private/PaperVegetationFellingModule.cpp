#include "PaperVegetationFellingModule.h"

#define LOCTEXT_NAMESPACE "FPaperVegetationFellingModule"

void FPaperVegetationFellingModule::StartupModule()
{
	// 物理模拟方案不需要 Shader
}

void FPaperVegetationFellingModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPaperVegetationFellingModule, PaperVegetationFelling)
