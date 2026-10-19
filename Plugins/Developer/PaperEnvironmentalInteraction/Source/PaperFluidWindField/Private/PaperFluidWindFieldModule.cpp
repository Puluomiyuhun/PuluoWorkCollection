#include "PaperFluidWindFieldModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#define LOCTEXT_NAMESPACE "FPaperFluidWindFieldModule"

void FPaperFluidWindFieldModule::StartupModule()
{
	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("PaperEnvironmentalInteraction"))->GetBaseDir(), TEXT("Shaders/FluidWindField"));
	AddShaderSourceDirectoryMapping(TEXT("/Shaders"), PluginShaderDir);
}

void FPaperFluidWindFieldModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FPaperFluidWindFieldModule, PaperFluidWindField)