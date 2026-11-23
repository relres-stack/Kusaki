// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/ScriptedKnockdownComponent.h"

#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "BaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

UScriptedKnockdownComponent::UScriptedKnockdownComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

ABaseCharacter* UScriptedKnockdownComponent::GetHero() const
{
	return Cast<ABaseCharacter>(GetOwner());
}

bool UScriptedKnockdownComponent::KnockDown(UAnimSequence* FallAnimation, bool bUntouchableWhileDown)
{
	ABaseCharacter* Hero = GetHero();
	if (!IsValid(Hero) || Hero->bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s is dead - he cannot fall"), *GetNameSafe(GetOwner()));
		return false;
	}
	if (Hero->IsMounted())
	{
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s rides a monster - dismount him first"), *GetNameSafe(Hero));
		return false;
	}
	// The latest call decides the protection, also when he is already lying.
	bDownIsUntouchable = bUntouchableWhileDown;
	if (State == EScriptedKnockdownState::Lying)
	{
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s is already lying"), *GetNameSafe(Hero));
		return true;
	}

	State = EScriptedKnockdownState::Lying;
	LyingAnimation = FallAnimation ? FallAnimation : Hero->KnockdownFallAnimation;

	if (AAIController* AI = Cast<AAIController>(Hero->GetController()))
	{
		AI->StopMovement();
	}

	USkeletalMeshComponent* Mesh = Hero->GetMesh();
	if (IsValid(Mesh) && LyingAnimation)
	{
		PlayPose(*Mesh, LyingAnimation, false);
		const UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance();
		if (!Single || Single->GetAnimationAsset() != LyingAnimation)
		{
			UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s could not play %s (wrong skeleton?) - he only stops"),
				*GetNameSafe(Hero), *GetNameSafe(LyingAnimation));
			LyingAnimation = nullptr;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s has no fall animation - he only stops where he is"), *GetNameSafe(Hero));
	}

	HoldDown(*Hero);
	Notify(FString::Printf(TEXT("%s falls (%s)"), *GetNameSafe(Hero), LyingAnimation ? *LyingAnimation->GetName() : TEXT("no animation")), FColor::Orange);
	return true;
}

bool UScriptedKnockdownComponent::GetUp(UAnimSequence* GetUpAnimation)
{
	ABaseCharacter* Hero = GetHero();
	if (State == EScriptedKnockdownState::GettingUp)
	{
		return true;
	}
	if (!IsValid(Hero) || State != EScriptedKnockdownState::Lying)
	{
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s is not lying - nothing to get up from"), *GetNameSafe(GetOwner()));
		return false;
	}

	State = EScriptedKnockdownState::GettingUp;
	UAnimSequence* Anim = GetUpAnimation ? GetUpAnimation : Hero->GetUpAnimation;
	USkeletalMeshComponent* Mesh = Hero->GetMesh();
	// While a panic holds him down, the panic decides when he stands; the scripted part is simply over.
	if (Anim && IsValid(Mesh) && !Hero->bIsPanicking && !Hero->bIsAwaitingGetUp)
	{
		PlayPose(*Mesh, Anim, false);
		const UWorld* World = GetWorld();
		GetUpEndTime = (World ? World->GetTimeSeconds() : 0.0) + Anim->GetPlayLength();
		Notify(FString::Printf(TEXT("%s gets up (%s, %.1f s)"), *GetNameSafe(Hero), *Anim->GetName(), Anim->GetPlayLength()), FColor::Yellow);
	}
	else
	{
		FinishGettingUp(*Hero);
	}
	return true;
}

void UScriptedKnockdownComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (State == EScriptedKnockdownState::Standing)
	{
		return;
	}
	ABaseCharacter* Hero = GetHero();
	if (!IsValid(Hero))
	{
		return;
	}
	if (Hero->bIsDead)
	{
		// The death ragdoll owns the body now.
		State = EScriptedKnockdownState::Standing;
		LyingAnimation = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s died while down"), *GetNameSafe(Hero));
		return;
	}
	if (Hero->IsMounted())
	{
		// Mounted anyway (the mount key is not blocked while lying): the mount owns his movement, the fall is over.
		UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s mounted a monster while down - the fall is over"), *GetNameSafe(Hero));
		State = EScriptedKnockdownState::Standing;
		LyingAnimation = nullptr;
		if (USkeletalMeshComponent* Mesh = Hero->GetMesh())
		{
			Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
		Hero->SetAimSuppressed(!Hero->IsPlayerControlled());
		return;
	}

	const UWorld* World = GetWorld();
	if (State == EScriptedKnockdownState::GettingUp && World && World->GetTimeSeconds() >= GetUpEndTime)
	{
		FinishGettingUp(*Hero);
		return;
	}

	HoldDown(*Hero);
}

void UScriptedKnockdownComponent::HoldDown(ABaseCharacter& Hero)
{
	// No walking, jumping or crouching (crouch needs a walking/falling mode too). Put back every frame: a control
	// switch possesses the hero, and ACharacter::Restart() then sets walking again. In the air he lands first.
	if (UCharacterMovementComponent* MoveComp = Hero.GetCharacterMovement())
	{
		if (MoveComp->MovementMode != MOVE_None && !MoveComp->IsFalling())
		{
			MoveComp->DisableMovement();
		}
		// C while down still calls Crouch()/UnCrouch(): with no movement mode nothing happens, but the request is
		// kept and carried out the moment he is up (found in PIE 22.11.2026). Drop it: he keeps the stance he fell in.
		MoveComp->bWantsToCrouch = Hero.IsCrouched();
	}

	// Aiming turns the body to the camera (bUseControllerRotationYaw) and allows throwing: off while down. Looking
	// around with the camera is not affected.
	if (!Hero.IsAimSuppressed())
	{
		Hero.SetAimSuppressed(true);
	}

	// The lying pose, put back if something took the mesh over (a panic episode ends with the AnimBP restored).
	// While he panics the panic's own knockdown pose is shown.
	if (State == EScriptedKnockdownState::Lying && LyingAnimation && !Hero.bIsPanicking && !Hero.bIsAwaitingGetUp)
	{
		USkeletalMeshComponent* Mesh = Hero.GetMesh();
		if (IsValid(Mesh) && Mesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
		{
			PlayPose(*Mesh, LyingAnimation, true);
		}
	}
}

void UScriptedKnockdownComponent::FinishGettingUp(ABaseCharacter& Hero)
{
	State = EScriptedKnockdownState::Standing;
	LyingAnimation = nullptr;

	if (!Hero.bIsPanicking && !Hero.bIsAwaitingGetUp)
	{
		if (USkeletalMeshComponent* Mesh = Hero.GetMesh())
		{
			Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
	}
	// A hero hidden by his script (UHeroScriptRunner::SetHeroHidden()) gets walking back when he is shown again.
	UCharacterMovementComponent* MoveComp = Hero.GetCharacterMovement();
	if (MoveComp && MoveComp->MovementMode == MOVE_None && !Hero.IsHidden())
	{
		MoveComp->SetDefaultMovementMode();
	}
	// The same rule as APrologueDirector: aiming is suppressed exactly while the hero is under AI.
	Hero.SetAimSuppressed(!Hero.IsPlayerControlled());

	Notify(FString::Printf(TEXT("%s is on his feet"), *GetNameSafe(&Hero)), FColor::Green);
}

void UScriptedKnockdownComponent::PlayPose(USkeletalMeshComponent& Mesh, UAnimSequence* Anim, bool bAtEnd)
{
	Mesh.PlayAnimation(Anim, false);
	if (bAtEnd && Anim)
	{
		Mesh.SetPosition(Anim->GetPlayLength(), false);
	}
}

float UScriptedKnockdownComponent::GetFallDuration() const
{
	return LyingAnimation ? LyingAnimation->GetPlayLength() : 0.0f;
}

FString UScriptedKnockdownComponent::DescribeState() const
{
	switch (State)
	{
	case EScriptedKnockdownState::Lying:
		return TEXT("lying");
	case EScriptedKnockdownState::GettingUp:
	{
		const UWorld* World = GetWorld();
		const double Left = FMath::Max(0.0, GetUpEndTime - (World ? World->GetTimeSeconds() : 0.0));
		return FString::Printf(TEXT("getting up (%.1f s)"), Left);
	}
	default:
		return TEXT("standing");
	}
}

void UScriptedKnockdownComponent::Notify(const FString& Message, const FColor& Color) const
{
	UE_LOG(LogTemp, Warning, TEXT("Scripted knockdown: %s"), *Message);
	if (GEngine)
	{
		// Key 608: the director's debug keys are 600-609.
		GEngine->AddOnScreenDebugMessage(608, 4.0f, Color, TEXT("Knockdown: ") + Message);
	}
}
