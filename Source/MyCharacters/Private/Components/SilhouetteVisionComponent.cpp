// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/SilhouetteVisionComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/MeshComponent.h"
#include "BaseMonster.h"
#include "HolyItem.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

USilhouetteVisionComponent::USilhouetteVisionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Same translucent unlit engine debug material UHunterFocusComponent::MentalTrailMaterial already
	// uses — see SilhouetteMaterial's own header comment.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SilhouetteMaterialFinder(
		TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
	if (SilhouetteMaterialFinder.Succeeded())
	{
		SilhouetteMaterial = SilhouetteMaterialFinder.Object;
	}
}

void USilhouetteVisionComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return;
	}

	PostProcessComp = NewObject<UPostProcessComponent>(OwnerActor, TEXT("SilhouetteVisionPostProcess"));
	PostProcessComp->SetupAttachment(OwnerActor->GetRootComponent());
	PostProcessComp->RegisterComponent();
	PostProcessComp->bUnbound = true;
	PostProcessComp->Priority = 100.0f;
	PostProcessComp->bEnabled = false;

	const float D = DesaturationAmount;
	PostProcessComp->Settings.bOverride_ColorSaturation = true;
	PostProcessComp->Settings.ColorSaturation = FVector4(D, D, D, 1.0f);
	PostProcessComp->Settings.bOverride_ColorOffset = true;
	PostProcessComp->Settings.ColorOffset = FVector4(
		SilhouetteTintColor.R * TintIntensity,
		SilhouetteTintColor.G * TintIntensity,
		SilhouetteTintColor.B * TintIntensity,
		0.0f);

	PostProcessComp->Settings.bOverride_VignetteIntensity = true;
	PostProcessComp->Settings.VignetteIntensity = VignetteIntensity;

	PostProcessComp->Settings.bOverride_FilmGrainIntensity = true;
	PostProcessComp->Settings.FilmGrainIntensity = GrainIntensity;
	if (IsValid(FilmGrainTextureOverride))
	{
		PostProcessComp->Settings.bOverride_FilmGrainTexture = true;
		PostProcessComp->Settings.FilmGrainTexture = FilmGrainTextureOverride;
	}
}

void USilhouetteVisionComponent::ActivateVision(float Duration)
{
	bIsVisionActive = true;

	if (IsValid(PostProcessComp))
	{
		PostProcessComp->bEnabled = true;

		UE_LOG(LogTemp, Warning, TEXT("%s: ActivateVision() — Priority=%.1f, ColorOffset=%s, VignetteIntensity=%.2f"),
			*GetName(), PostProcessComp->Priority, *PostProcessComp->Settings.ColorOffset.ToString(), PostProcessComp->Settings.VignetteIntensity);
	}

	ApplySilhouettes();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoDeactivateTimerHandle);
		if (Duration > 0.0f)
		{
			World->GetTimerManager().SetTimer(AutoDeactivateTimerHandle, this, &USilhouetteVisionComponent::DeactivateVision, Duration, false);
		}
	}
}

void USilhouetteVisionComponent::DeactivateVision()
{
	bIsVisionActive = false;

	if (IsValid(PostProcessComp))
	{
		PostProcessComp->bEnabled = false;
	}

	RestoreSilhouettes();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoDeactivateTimerHandle);
	}
}

void USilhouetteVisionComponent::ApplySilhouettes()
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(OwnerActor) || !World || !IsValid(SilhouetteMaterial))
	{
		return;
	}

	const FVector OwnerLocation = OwnerActor->GetActorLocation();

	TArray<AActor*> NearbyActors;
	TArray<AActor*> Monsters;
	UGameplayStatics::GetAllActorsOfClass(World, ABaseMonster::StaticClass(), Monsters);
	NearbyActors.Append(Monsters);
	TArray<AActor*> Items;
	UGameplayStatics::GetAllActorsOfClass(World, AHolyItem::StaticClass(), Items);
	NearbyActors.Append(Items);

	for (AActor* Actor : NearbyActors)
	{
		if (!IsValid(Actor) || Actor == OwnerActor)
		{
			continue;
		}
		if (FVector::Dist(Actor->GetActorLocation(), OwnerLocation) > SilhouetteSearchRadius)
		{
			continue;
		}

		UMeshComponent* MeshComp = nullptr;
		if (ABaseMonster* Monster = Cast<ABaseMonster>(Actor))
		{
			MeshComp = Monster->GetMesh();
		}
		else if (AHolyItem* Item = Cast<AHolyItem>(Actor))
		{
			MeshComp = Item->ItemMesh;
		}

		if (!IsValid(MeshComp))
		{
			continue;
		}

		const int32 NumMaterials = MeshComp->GetNumMaterials();
		TArray<UMaterialInterface*> SavedMaterials;
		SavedMaterials.Reserve(NumMaterials);
		for (int32 SlotIndex = 0; SlotIndex < NumMaterials; ++SlotIndex)
		{
			SavedMaterials.Add(MeshComp->GetMaterial(SlotIndex));

			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SilhouetteMaterial, this))
			{
				MID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
				MeshComp->SetMaterial(SlotIndex, MID);
			}
		}

		SilhouettedMeshes.Add(MeshComp);
		OriginalMaterials.Add(SavedMaterials);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1007, 2.0f, FColor::White,
			FString::Printf(TEXT("Silhouette Vision: %d silhouette(s) within %.0f units"), SilhouettedMeshes.Num(), SilhouetteSearchRadius));
	}
}

void USilhouetteVisionComponent::RestoreSilhouettes()
{
	for (int32 Index = 0; Index < SilhouettedMeshes.Num(); ++Index)
	{
		UMeshComponent* MeshComp = SilhouettedMeshes[Index].Get();
		if (!IsValid(MeshComp))
		{
			// The actor died/was destroyed while the vision was active — nothing left to restore.
			continue;
		}

		const TArray<UMaterialInterface*>& SavedMaterials = OriginalMaterials[Index];
		for (int32 SlotIndex = 0; SlotIndex < SavedMaterials.Num(); ++SlotIndex)
		{
			MeshComp->SetMaterial(SlotIndex, SavedMaterials[SlotIndex]);
		}
	}

	SilhouettedMeshes.Reset();
	OriginalMaterials.Reset();
}

void USilhouetteVisionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoDeactivateTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}
