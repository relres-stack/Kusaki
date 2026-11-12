// Fill out your copyright notice in the Description page of Project Settings.

#include "SiblingCharacter.h"
#include "ControlHint.h"
#include "FlashlightComponent.h"
#include "InventoryComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"

ASiblingCharacter::ASiblingCharacter()
{
	FlashlightLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightLight"));
	FlashlightLight->SetupAttachment(GetMesh(), FlashlightSocket);
	FlashlightLight->Intensity = 8000.0f;
	FlashlightLight->OuterConeAngle = 30.0f;
	FlashlightLight->AttenuationRadius = 2000.0f;
	FlashlightLight->SetVisibility(true);

	FlashlightComponent = CreateDefaultSubobject<UFlashlightComponent>(TEXT("FlashlightComponent"));
	FlashlightComponent->LightComponent = FlashlightLight;
}

void ASiblingCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// "Flashlight" action mapping bound to T. BindKey() directly too, same fallback pattern this
	// project's other actions already use.
	PlayerInputComponent->BindAction("Flashlight", IE_Pressed, this, &ASiblingCharacter::OnFlashlightPressed);
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &ASiblingCharacter::OnFlashlightPressed);
}

void ASiblingCharacter::GetControlHints(TArray<FControlHint>& OutHints) const
{
	// Keep in sync with SetupPlayerInputComponent() above.
	OutHints.Add(FControlHint::Section(INVTEXT("БРАТ / СЕСТРА")));
	OutHints.Emplace(INVTEXT("T"), INVTEXT("фонарик вкл / выкл"));

	Super::GetControlHints(OutHints);
}

void ASiblingCharacter::OnFlashlightPressed()
{
	if (IsValid(FlashlightComponent))
	{
		FlashlightComponent->Toggle();

		// Immediate HUD refresh — see ARedneckCharacter::OnFlashlightPressed()'s matching call for why.
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
	}
}
