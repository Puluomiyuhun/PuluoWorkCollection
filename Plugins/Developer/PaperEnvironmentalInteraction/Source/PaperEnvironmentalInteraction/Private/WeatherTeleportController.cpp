#include "WeatherTeleportController.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"

AWeatherTeleportController::AWeatherTeleportController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AWeatherTeleportController::BeginPlay()
{
	Super::BeginPlay();

	EnableLocalInput();

	if (!TargetCharacter)
	{
		TargetCharacter = UGameplayStatics::GetPlayerCharacter(this, 0);
	}
	Weather_O();
}

void AWeatherTeleportController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!UDSActor) return;
	if (bTimeRun)
	{
		TimeOfDay = TimeOfDay + TimeRunSpeed;
		if (TimeOfDay > 2400.0f)
		{
			TimeOfDay = 0.0f;
		}
	}
	if (FDoubleProperty* TimeProp = FindFProperty<FDoubleProperty>(UDSActor->GetClass(), TEXT("Time of Day")))
		TimeProp->SetPropertyValue_InContainer(UDSActor, TimeOfDay);
}


void AWeatherTeleportController::EnableLocalInput()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	EnableInput(PC);

	if (!InputComponent)
	{
		return;
	}

	// 天气切换
	InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AWeatherTeleportController::Weather_U);
	InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AWeatherTeleportController::Weather_I);
	InputComponent->BindKey(EKeys::O, IE_Pressed, this, &AWeatherTeleportController::Weather_O);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AWeatherTeleportController::Weather_P);

	// 传送
	InputComponent->BindKey(EKeys::Zero,  IE_Pressed, this, &AWeatherTeleportController::Teleport_0);
	InputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AWeatherTeleportController::Teleport_1);
	InputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AWeatherTeleportController::Teleport_2);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AWeatherTeleportController::Teleport_3);
	InputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AWeatherTeleportController::Teleport_4);
	InputComponent->BindKey(EKeys::Five,  IE_Pressed, this, &AWeatherTeleportController::Teleport_5);
	InputComponent->BindKey(EKeys::Six,   IE_Pressed, this, &AWeatherTeleportController::Teleport_6);
	InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AWeatherTeleportController::Teleport_7);
	InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AWeatherTeleportController::Teleport_8);
	InputComponent->BindKey(EKeys::Nine,  IE_Pressed, this, &AWeatherTeleportController::Teleport_9);
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AWeatherTeleportController::Teleport_Z);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &AWeatherTeleportController::Teleport_X);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AWeatherTeleportController::Teleport_C);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AWeatherTeleportController::Teleport_V);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AWeatherTeleportController::Teleport_B);
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AWeatherTeleportController::Teleport_N);
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AWeatherTeleportController::Teleport_M);
}

void AWeatherTeleportController::TeleportTargetToTag(FName TagName)
{
	if (!TargetCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetCharacter is null."));
		return;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), TagName, FoundActors);

	if (FoundActors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No actor found with tag: %s"), *TagName.ToString());
		return;
	}

	AActor* DestActor = FoundActors[0];
	if (!DestActor)
	{
		return;
	}

	FVector DestLocation = DestActor->GetActorLocation();

	FRotator DestRotation = DestActor->GetActorRotation();

	// 直接瞬移
	const bool bTeleportSucceeded = TargetCharacter->TeleportTo(DestLocation, DestRotation);
	if (AController* Controller = TargetCharacter->GetController())
	{
		Controller->SetControlRotation(DestRotation);
	}
	if (!bTeleportSucceeded)
	{
		// fallback
		TargetCharacter->SetActorLocationAndRotation(DestLocation, DestRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	UE_LOG(LogTemp, Log, TEXT("Teleported to tag: %s"), *TagName.ToString());
}

// -------- 天气快捷键 --------
void AWeatherTeleportController::Weather_U()
{
	bTimeRun = false;
	TimeOfDay = 960.0f;
}

void AWeatherTeleportController::Weather_I()
{
	bTimeRun = !bTimeRun;
}

void AWeatherTeleportController::Weather_O()
{
	if (!UDSActor || !LookdevActor) return;
	UDSActor->SetActorHiddenInGame(true);
	LookdevActor->SetActorHiddenInGame(false);
}

void AWeatherTeleportController::Weather_P()
{
	if (!UDSActor || !LookdevActor) return;
	UDSActor->SetActorHiddenInGame(false);
	LookdevActor->SetActorHiddenInGame(true);
}

// -------- 传送快捷键 --------

void AWeatherTeleportController::Teleport_0() { TeleportTargetToTag(TEXT("0")); }
void AWeatherTeleportController::Teleport_1() { TeleportTargetToTag(TEXT("1")); }
void AWeatherTeleportController::Teleport_2() { TeleportTargetToTag(TEXT("2")); }
void AWeatherTeleportController::Teleport_3() { TeleportTargetToTag(TEXT("3")); }
void AWeatherTeleportController::Teleport_4() { TeleportTargetToTag(TEXT("4")); }
void AWeatherTeleportController::Teleport_5() { TeleportTargetToTag(TEXT("5")); }
void AWeatherTeleportController::Teleport_6() { TeleportTargetToTag(TEXT("6")); }
void AWeatherTeleportController::Teleport_7() { TeleportTargetToTag(TEXT("7")); }
void AWeatherTeleportController::Teleport_8() { TeleportTargetToTag(TEXT("8")); }
void AWeatherTeleportController::Teleport_9() { TeleportTargetToTag(TEXT("9")); }
void AWeatherTeleportController::Teleport_Z() { TeleportTargetToTag(TEXT("Z")); }
void AWeatherTeleportController::Teleport_X() { TeleportTargetToTag(TEXT("X")); }
void AWeatherTeleportController::Teleport_C() { TeleportTargetToTag(TEXT("C")); }
void AWeatherTeleportController::Teleport_V() { TeleportTargetToTag(TEXT("V")); }
void AWeatherTeleportController::Teleport_B() { TeleportTargetToTag(TEXT("B")); }
void AWeatherTeleportController::Teleport_N() { TeleportTargetToTag(TEXT("N")); }
void AWeatherTeleportController::Teleport_M() { TeleportTargetToTag(TEXT("M")); }