// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "SiblingCharacter.generated.h"

class UFlashlightComponent;
class USpotLightComponent;

// The Sibling: carries a Flashlight (ordinary light — vision only, does NOT repel monsters — see
// UFlashlightComponent's own comment). No other hero-specific mechanics.
UCLASS()
class MYCHARACTERS_API ASiblingCharacter : public ABaseCharacter
{
	GENERATED_BODY()

public:
	ASiblingCharacter();

	// Battery/toggle logic — see UFlashlightComponent's own header comment. The actual light is
	// FlashlightLight below; this component is assigned a reference to it in the constructor.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sibling")
	TObjectPtr<UFlashlightComponent> FlashlightComponent;

	// The actual light — created here (not by UFlashlightComponent itself; see its header comment on
	// why) and attached to the mesh.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sibling")
	TObjectPtr<USpotLightComponent> FlashlightLight;

	// Bone on the mesh the flashlight attaches to — see ABaseCharacter::RightHandSocket's own comment on
	// why this is the "hand_r" BONE name, not the old "Socket_Hand_R" custom socket (which doesn't exist
	// on the SKM_Manny_Simple mesh this character actually uses).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sibling")
	FName FlashlightSocket = TEXT("hand_r");

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	// Bound to a dedicated "Flashlight" action/T key — thin wrapper so SetupPlayerInputComponent() can
	// bind a UFUNCTION directly.
	UFUNCTION()
	void OnFlashlightPressed();
};
