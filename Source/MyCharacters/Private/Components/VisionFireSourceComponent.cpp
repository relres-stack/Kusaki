// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/VisionFireSourceComponent.h"
#include "VisionFireSource.h"
#include "PriestCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Math/RotationMatrix.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PointLightComponent.h"
#include "NiagaraComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Components/PostProcessComponent.h"
#include "Math/RandomStream.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Rendering/SlateRenderTransform.h"
#include "Math/TransformCalculus2D.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UVisionFireSourceComponent::UVisionFireSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	FireSourceClass = AVisionFireSource::StaticClass();
}

void UVisionFireSourceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(FireSourceClass) && GetWorld())
	{
		FireSourceActor = GetWorld()->SpawnActor<AVisionFireSource>(FireSourceClass);
	}

	BuildLookPostProcess();
	for (int32 StateIndex = 0; StateIndex < FrameStateCount; ++StateIndex)
	{
		BuildFrameTexture(StateIndex);
	}
	// The vein picture: a hand-made texture asset if it exists (white veins in the alpha channel, see
	// SourceArt/VisionVeins), otherwise the procedural network as a fallback.
	VeinsTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/T_VisionVeins.T_VisionVeins"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	bVeinsFromAsset = IsValid(VeinsTexture);
	if (bVeinsFromAsset)
	{
		// The swell reuses the same picture (drawn a hair larger), no second texture is needed.
		VeinsSwellTexture = VeinsTexture;
	}
	else
	{
		VeinsTexture = BuildVeinsTexture(1.0f);
		VeinsSwellTexture = (VeinsPulseSwell > 0.0f) ? BuildVeinsTexture(1.0f + VeinsPulseSwell) : nullptr;
	}
	BuildFingersTextures();

	if (IsValid(FireSourceActor) && IsValid(FireSourceActor->FireLight))
	{
		BaseFireLightIntensity = FireSourceActor->FireLight->Intensity;
		BaseFireLightRadius = FireSourceActor->FireLight->AttenuationRadius;
	}
}

void UVisionFireSourceComponent::ToggleVision()
{
	if (bIsVisionActive)
	{
		EndVisionEffect();
		return;
	}

	if (!bCanUseVision)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: ToggleVision() refused — still on cooldown"), *GetOwner()->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(421, 2.0f, FColor::Red, TEXT("Воспалённое зрение: на перезарядке"));
		}
		return;
	}

	// Moonshine (or anything else) is holding the eyes open — the ability needs them closed, so refuse
	// instead of running with open eyes.
	if (APriestCharacter* PriestCheck = Cast<APriestCharacter>(GetOwner()))
	{
		if (PriestCheck->AreEyesLockedOpen())
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(421, 2.0f, FColor::Red, TEXT("Воспалённое зрение: глаза не закрыть"));
			}
			return;
		}
	}

	bIsVisionActive = true;

	// Switch camera BEFORE UpdateFirePlacement() below, so the very first placement this activation already
	// reads the first-person camera's location/rotation, not the third-person one it's replacing — see
	// UpdateFirePlacement()'s own comment on why it now needs the ACTIVE camera specifically to be the
	// first-person one. SetEyesClosedDarknessSuppressed() is paired with it in the same block — this ability
	// is meant to be used with the eyes closed, and ABaseCharacter::Tick()'s own eyes-closed screen darkening
	// would otherwise crush this ability's post-process material (see that function's own comment).
	if (APriestCharacter* PriestOwner = Cast<APriestCharacter>(GetOwner()))
	{
		PriestOwner->SetVisionFirstPersonActive(true, bUse3DHand);
		PriestOwner->SetEyesClosedDarknessSuppressed(true);

		// The ability is only ever used with the eyes closed: close them now if they were open (already
		// closed -> nothing happens, activation works from closed eyes as before), then hold them shut for
		// the whole duration so the player cannot open them mid-vision. The lock is released in
		// EndVisionEffect(), the eyes themselves stay closed — the player opens them when they choose.
		if (!PriestOwner->IsEyesClosed())
		{
			PriestOwner->CloseEyes();
		}
		PriestOwner->SetEyesLockedClosed(true);
	}

	if (IsValid(FireSourceActor))
	{
		UpdateFirePlacement();
		FireSourceActor->Activate();

		// Tone the fire down (see FireLightIntensityScale / FireVisualScale).
		if (IsValid(FireSourceActor->FireLight) && BaseFireLightIntensity >= 0.0f)
		{
			FireSourceActor->FireLight->SetIntensity(BaseFireLightIntensity * FireLightIntensityScale);
		}
		if (IsValid(FireSourceActor->FireNiagaraComponent))
		{
			FireSourceActor->FireNiagaraComponent->SetRelativeScale3D(FVector(FireVisualScale));
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(VisionDurationTimerHandle, this, &UVisionFireSourceComponent::EndVisionEffect, VisionDuration, false);
	}
}

void UVisionFireSourceComponent::EndVisionEffect()
{
	if (!bIsVisionActive)
	{
		return;
	}

	bIsVisionActive = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VisionDurationTimerHandle);
	}

	if (APriestCharacter* PriestOwner = Cast<APriestCharacter>(GetOwner()))
	{
		PriestOwner->SetVisionFirstPersonActive(false, false);
		PriestOwner->SetEyesClosedDarknessSuppressed(false);
		PriestOwner->SetEyesLockedClosed(false);
	}

	if (IsValid(FireSourceActor))
	{
		FireSourceActor->Deactivate();
		// Back to a free-standing actor (it was glued to the camera while on the fingertip).
		FireSourceActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		FireSourceActor->SetActorRotation(FRotator::ZeroRotator);
		FingertipLeanCurrent = FVector2D::ZeroVector;
	}

	bCanUseVision = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(VisionCooldownTimerHandle, this, &UVisionFireSourceComponent::EndVisionCooldown, VisionCooldown, false);
	}
}

void UVisionFireSourceComponent::EndVisionCooldown()
{
	bCanUseVision = true;
}

void UVisionFireSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VisionDurationTimerHandle);
		World->GetTimerManager().ClearTimer(VisionCooldownTimerHandle);
	}
	if (bIsVisionActive)
	{
		if (APriestCharacter* PriestOwner = Cast<APriestCharacter>(GetOwner()))
		{
			PriestOwner->SetEyesLockedClosed(false);
		}
	}
	HideFrameOverlay();
	HideVeinsOverlay();
	HideFingersOverlay();
	Super::EndPlay(EndPlayReason);
}

void UVisionFireSourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Screen look fades in/out on its own schedule — it must keep running for a moment after the ability ends.
	UpdateLook(DeltaTime);

	if (!bIsVisionActive)
	{
		return;
	}

	// Eyes were forced open against the lock (panic, Moonshine) — the ability only makes sense with
	// closed eyes, so it ends itself (normal cooldown applies).
	if (const APriestCharacter* PriestOwner = Cast<APriestCharacter>(GetOwner()))
	{
		if (!PriestOwner->IsEyesClosed())
		{
			EndVisionEffect();
			return;
		}
	}

	UpdateFirePlacement();
}

void UVisionFireSourceComponent::UpdateFirePlacement()
{
	if (!IsValid(FireSourceActor))
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	// Same active-camera source as ABaseCharacter::TraceAimTarget()/TraceHunterFocusTarget() — the only
	// thing guaranteed to match the actual view, not GetControlRotation()/GetActorEyesViewPoint().
	const APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController());
	const APlayerCameraManager* CamManager = PC ? PC->PlayerCameraManager : nullptr;
	if (!IsValid(CamManager))
	{
		return;
	}

	// Full 3D camera rotation, on purpose — this went back and forth across several earlier rounds (see the
	// header's own history if this ever gets read out of order): a yaw-only, capsule-anchored, ground-
	// snapped design was built specifically to fix "aims into the floor/vanishes when looking down", but
	// the actual requirement turned out to be the opposite once it worked: the fire should stay visually
	// fixed on screen while sliding along the world wherever the camera is actually pointed — down at your
	// own feet lights that spot, far ahead lights a distant one — capped at FireDistance (see its own
	// comment) so it can't be aimed clear across the level. That's a "flashlight" behavior, which needs the
	// FULL view direction, pitch included, not a flattened one. Safe to do now specifically because the
	// origin is the ACTIVE camera (APriestCharacter::VisionFirstPersonCamera during this ability — head
	// socket, no spring-arm boom to orbit on) — the boom-orbit bobbing problem that originally justified
	// moving off the camera entirely was a third-person-only issue (see VisionFireSourceComponent.h's own
	// history).
	const FVector TraceStart = CamManager->GetCameraLocation();
	const FRotator CameraRotation = CamManager->GetCameraRotation();
	const FVector CameraForward = CameraRotation.Vector();

	// Direction of the placement ray: straight ahead, or — with bAttachFireToFingers — through the point of the
	// screen where the fingertips are, so the fire appears to sit on the fingers.
	FVector RayDirection = CameraForward;
	FVector FingertipWorld = FVector::ZeroVector;
	const APriestCharacter* HandOwner = Cast<APriestCharacter>(GetOwner());
	const bool bUseFingertip3D = bUse3DHand && bAttachFireToFingers && HandOwner && HandOwner->bVisionArmRaised
		&& HandOwner->GetVisionFingertipLocation(FingertipWorld) && FVector::DistSquared(FingertipWorld, TraceStart) > 25.0f;
	if (bUseFingertip3D)
	{
		// 3D hand: aim through the real fingertip of the raised arm.
		RayDirection = (FingertipWorld - TraceStart).GetSafeNormal();
	}
	else if (bAttachFireToFingers && !bUse3DHand)
	{
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		PC->GetViewportSize(ViewportWidth, ViewportHeight);
		if (ViewportWidth > 0 && ViewportHeight > 0)
		{
			const FVector2D TipFraction = ComputeFireScreenFraction(ViewportWidth, ViewportHeight);
			FVector RayOrigin = FVector::ZeroVector;
			FVector DeprojectedDirection = FVector::ForwardVector;
			if (PC->DeprojectScreenPositionToWorld(static_cast<float>(TipFraction.X * ViewportWidth), static_cast<float>(TipFraction.Y * ViewportHeight), RayOrigin, DeprojectedDirection))
			{
				RayDirection = DeprojectedDirection.GetSafeNormal();
			}
		}
	}

	// 3D hand, fire on the fingertip: no ray at all — the flame (and its light) is simply put at the fingertip.
	if (bUseFingertip3D && bFireOnFingertip)
	{
		// The flame is attached to the camera (the hand is too) and placed at the fingertip's camera-space position, so it
		// stays rigidly with the hand whatever the camera does. It leans against the movement and straightens when you stop.
		FVector FingertipLocal = FVector::ZeroVector;
		USceneComponent* CameraComponent = HandOwner ? Cast<USceneComponent>(HandOwner->VisionFirstPersonCamera) : nullptr;
		if (CameraComponent && HandOwner->GetVisionFingertipCameraLocal(FingertipLocal))
		{
			if (FireSourceActor->GetRootComponent() && FireSourceActor->GetRootComponent()->GetAttachParent() != CameraComponent)
			{
				FireSourceActor->AttachToComponent(CameraComponent, FAttachmentTransformRules::KeepRelativeTransform);
			}
			FVector HorizontalVelocity = GetOwner()->GetVelocity();
			HorizontalVelocity.Z = 0.0f;
			const FVector LocalVelocity = FRotator(0.0f, CameraRotation.Yaw, 0.0f).UnrotateVector(HorizontalVelocity);
			FVector2D LeanTarget(-LocalVelocity.X, -LocalVelocity.Y);
			LeanTarget /= FMath::Max(FingertipFlameLeanSpeedRef, 1.0f);
			if (LeanTarget.SizeSquared() > 1.0)
			{
				LeanTarget.Normalize();
			}
			const float LeanDeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			FingertipLeanCurrent = FMath::Vector2DInterpTo(FingertipLeanCurrent, LeanTarget, LeanDeltaTime, FingertipFlameLeanResponse);
			FQuat LeanRotation = FQuat::Identity;
			const FVector LeanDirection(FingertipLeanCurrent.X, FingertipLeanCurrent.Y, 0.0f);
			if (LeanDirection.SizeSquared() > 1e-6f)
			{
				const FVector LeanAxis = FVector::CrossProduct(FVector::UpVector, LeanDirection).GetSafeNormal();
				LeanRotation = FQuat(LeanAxis, FMath::DegreesToRadians(FingertipFlameMaxLean * FMath::Min(1.0f, static_cast<float>(LeanDirection.Size()))));
			}
			FireSourceActor->SetActorRelativeLocation(FingertipLocal + FingertipFireOffset);
			FireSourceActor->SetActorRelativeRotation(LeanRotation);
		}
		else
		{
			FireSourceActor->SetActorLocation(FingertipWorld + CameraRotation.RotateVector(FingertipFireOffset));
		}
		// Where the fingertip is on the screen: the flat flame (see UpdateLook()/ShowFingersOverlay()) is drawn there,
		// its base on the tip.
		{
			int32 FlameViewportWidth = 0;
			int32 FlameViewportHeight = 0;
			PC->GetViewportSize(FlameViewportWidth, FlameViewportHeight);
			FVector2D TipOnScreen = FVector2D::ZeroVector;
			if (FlameViewportWidth > 0 && FlameViewportHeight > 0
				&& PC->ProjectWorldLocationToScreen(FingertipWorld + CameraRotation.RotateVector(FingertipFireOffset), TipOnScreen, false))
			{
				FingertipScreenCenter = FVector2D(TipOnScreen.X / FlameViewportWidth,
					TipOnScreen.Y / FlameViewportHeight - 0.56 * FingertipFlameHalfExtent.Y);
			}
		}
		if (IsValid(FireSourceActor->FireNiagaraComponent))
		{
			FireSourceActor->FireNiagaraComponent->SetVisibility(bFingertipNiagaraFlame);
		}
		if (IsValid(FireSourceActor->SmokeNiagaraComponent))
		{
			FireSourceActor->SmokeNiagaraComponent->SetVisibility(bFingertipNiagaraFlame);
		}
		if (bFingertipNiagaraFlame && IsValid(FireSourceActor->FireNiagaraComponent))
		{
			// Size: the component scale (works for most fire systems) AND the NS_Fire user parameters (NS_Fire ignores the
			// component scale; for any other system the unknown parameters are simply not used).
			FireSourceActor->FireNiagaraComponent->SetRelativeScale3D(FVector(FingertipFireScale));
			FireSourceActor->FireNiagaraComponent->SetVariableFloat(FlameScaleParameter, FingertipFireScale);
			FireSourceActor->FireNiagaraComponent->SetVariableFloat(LifetimeScaleParameter, FingertipFlameLifetimeScale);
		}
		if (IsValid(FireSourceActor->FireLight) && BaseFireLightIntensity >= 0.0f)
		{
			FireSourceActor->FireLight->SetIntensity(BaseFireLightIntensity * FireLightIntensityScale * FingertipLightScale);
			if (BaseFireLightRadius > 0.0f)
			{
				FireSourceActor->FireLight->SetAttenuationRadius(BaseFireLightRadius * FMath::Max(FingertipLightScale, 0.05f));
			}
		}
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VisionFirePlacementTrace), false);
	QueryParams.AddIgnoredActor(OwnerCharacter);
	QueryParams.AddIgnoredActor(FireSourceActor);
	if (OwnerCharacter->GetMesh())
	{
		QueryParams.AddIgnoredComponent(OwnerCharacter->GetMesh());
	}
	if (OwnerCharacter->GetCapsuleComponent())
	{
		QueryParams.AddIgnoredComponent(OwnerCharacter->GetCapsuleComponent());
	}

	// Object-type query (not LineTraceSingleByChannel(ECC_Visibility), unlike TraceAimTarget()/
	// TraceHunterFocusTarget()) — every ABaseMonster capsule DELIBERATELY blocks ECC_Visibility (see
	// ABaseMonster's constructor comment: needed for the aim/throw reticle to hit monsters at all), so a
	// channel trace here would place the fire against a monster instead of ignoring it. World geometry's
	// ObjectType (WorldStatic/WorldDynamic) structurally excludes every Pawn regardless of that channel-
	// response override, with no changes to ABaseMonster needed.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	// Object-type traces (LineTraceSingleByObjectType) are a known Unreal gotcha: they report a "hit" for
	// ANY component of a queried object type whose response isn't explicitly Ignore — that includes pure
	// OVERLAP triggers, not just actual blocking geometry. Confirmed live via a temporary on-screen debug:
	// the fire was landing right on top of the player because the trace was hitting
	// AHolyLightZone::LightZoneTrigger (SetCollisionProfileName("OverlapAllDynamic"), Object Type
	// WorldDynamic, every response Overlap, radius 300) at distance ~0 — the zone was sitting right where
	// the character stood, and a pure overlap volume isn't something a fire should stop at or hug. This
	// project has several other Overlap-only trigger volumes in the same shape (BearTrap,
	// MoonshineScentComponent, etc.) that could just as easily sit in this trace's path, so the fix below
	// is general rather than special-casing AHolyLightZone specifically: after any object-type hit, check
	// whether the hit component ACTUALLY has a Block response to WorldStatic or WorldDynamic — if not, it's
	// an overlap-only trigger, so ignore that actor and re-trace past it (bounded iteration count as a
	// safety net against a pathological stack of overlapping trigger volumes).
	auto TraceIgnoringOverlapOnlyHits = [this, &ObjectParams, &QueryParams](const FVector& Start, const FVector& End, FHitResult& OutHit) -> bool
	{
		FCollisionQueryParams LocalParams = QueryParams;
		constexpr int32 MaxIgnoreIterations = 8;
		for (int32 Iteration = 0; Iteration < MaxIgnoreIterations; ++Iteration)
		{
			FHitResult Candidate;
			if (!GetWorld()->LineTraceSingleByObjectType(Candidate, Start, End, ObjectParams, LocalParams))
			{
				return false;
			}
			UPrimitiveComponent* HitComponent = Candidate.GetComponent();
			const bool bActuallyBlocks = HitComponent &&
				(HitComponent->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block ||
				 HitComponent->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
			if (!bActuallyBlocks)
			{
				LocalParams.AddIgnoredActor(Candidate.GetActor());
				continue;
			}
			OutHit = Candidate;
			return true;
		}
		return false; // gave up after MaxIgnoreIterations overlap-only actors — treat as "nothing in the way"
	};

	// One trace along the full view ray, out to FireDistance — this alone is what makes the fire land on
	// whatever the player is actually looking at (their own feet, a nearby wall, a distant floor) instead of
	// needing a separate "how far forward" pass and a separate "what's the floor height here" pass: with
	// pitch included, the trace direction itself already IS "look down" or "look far ahead".
	const FVector TraceEnd = TraceStart + RayDirection * FireDistance;

	FHitResult Hit;
	const bool bHit = TraceIgnoringOverlapOnlyHits(TraceStart, TraceEnd, Hit);
	// No lower distance clamp on the pull-back, by design — the fire can end up very close to the camera,
	// just never inside whatever it hit. If nothing is hit within FireDistance, the fire sits at the full
	// FireDistance out along the view ray — a flashlight beam that just reaches its max range and stops,
	// rather than vanishing.
	const FVector NewLocation = bHit ? (Hit.ImpactPoint - RayDirection * FireSafetyBuffer) : TraceEnd;

	FireSourceActor->SetActorLocation(NewLocation);

	// Keep the flame about the same size ON SCREEN wherever the ray lands (see bKeepFlameApparentSize).
	if (bKeepFlameApparentSize)
	{
		const float Distance = static_cast<float>(FVector::Dist(TraceStart, NewLocation));
		const float ApparentScale = FMath::Clamp(Distance / FMath::Max(FireReferenceDistance, 1.0f), 0.25f, 4.0f);
		if (IsValid(FireSourceActor->FireNiagaraComponent))
		{
			FireSourceActor->FireNiagaraComponent->SetRelativeScale3D(FVector(FireVisualScale * ApparentScale));
		}
		if (IsValid(FireSourceActor->SmokeNiagaraComponent))
		{
			FireSourceActor->SmokeNiagaraComponent->SetRelativeScale3D(FVector(ApparentScale));
		}
	}
}

FVector2D UVisionFireSourceComponent::ComputeFingersSway() const
{
	const UWorld* World = GetWorld();
	const float Time = World ? World->GetTimeSeconds() * FingersSwaySpeed : 0.0f;
	const float OffsetX = FingersSwayAmount * (FMath::Sin(Time * 0.9f) + 0.5f * FMath::Sin(Time * 2.1f + 1.7f));
	const float OffsetY = FingersSwayAmount * 0.7f * (FMath::Sin(Time * 1.3f + 0.6f) + 0.4f * FMath::Sin(Time * 2.9f));
	return FVector2D(OffsetX, OffsetY);
}

FVector2D UVisionFireSourceComponent::ComputeFireScreenFraction(int32 ViewportWidth, int32 ViewportHeight) const
{
	// Reproduces how the fingers picture is laid out on screen: it fills its box (FingersScreenMin..Max) while keeping
	// its own proportions (ScaleToFit, centred), so the fingertips' position follows the picture, not just the box.
	const UTexture2D* FingersImage = IsValid(FingersTexture) ? FingersTexture.Get() : FingersPlaceholderTexture.Get();
	const float ImageWidth = FingersImage ? static_cast<float>(FingersImage->GetSizeX()) : 512.0f;
	const float ImageHeight = FingersImage ? static_cast<float>(FingersImage->GetSizeY()) : 400.0f;

	const FVector2D Sway = ComputeFingersSway();
	const float BoxX = static_cast<float>(FingersScreenMin.X + Sway.X) * ViewportWidth;
	const float BoxY = static_cast<float>(FingersScreenMin.Y + Sway.Y) * ViewportHeight;
	const float BoxWidth = static_cast<float>(FingersScreenMax.X - FingersScreenMin.X) * ViewportWidth;
	const float BoxHeight = static_cast<float>(FingersScreenMax.Y - FingersScreenMin.Y) * ViewportHeight;

	const float Scale = FMath::Min(BoxWidth / ImageWidth, BoxHeight / ImageHeight);
	const float DrawWidth = ImageWidth * Scale;
	const float DrawHeight = ImageHeight * Scale;
	const float DrawX = BoxX + (BoxWidth - DrawWidth) * 0.5f;
	const float DrawY = BoxY + (BoxHeight - DrawHeight) * 0.5f;

	const float TipX = DrawX + static_cast<float>(FingersPinchPoint.X) * DrawWidth;
	const float TipY = DrawY + static_cast<float>(FingersPinchPoint.Y) * DrawHeight;
	return FVector2D(TipX / ViewportWidth + FireScreenOffset.X, TipY / ViewportHeight + FireScreenOffset.Y);
}

// ---------------------------------------------------------------------------------------------------------
// Screen look ("Воспалённое зрение")
// ---------------------------------------------------------------------------------------------------------

void UVisionFireSourceComponent::BuildLookPostProcess()
{
	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return;
	}

	// Stays alive for the whole play session; only its BlendWeight and bEnabled change (see UpdateLook()).
	// Priority 50: above the eyes-closed look (default 0) and the level's own volumes.
	LookPostProcess = NewObject<UPostProcessComponent>(OwnerActor, TEXT("InflamedVisionPostProcess"));
	LookPostProcess->SetupAttachment(OwnerActor->GetRootComponent());
	LookPostProcess->RegisterComponent();
	LookPostProcess->bUnbound = true;
	LookPostProcess->Priority = 50.0f;
	LookPostProcess->BlendWeight = 0.0f;
	LookPostProcess->bEnabled = false;

	// The vision material: own assignment, or the asset /Game/M_InflamedVision if it exists.
	if (!IsValid(VisionMaterial))
	{
		VisionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_InflamedVision.M_InflamedVision"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}
	if (IsValid(VisionMaterial))
	{
		VisionMID = UMaterialInstanceDynamic::Create(VisionMaterial, this);
		if (IsValid(VisionMID))
		{
			LookPostProcess->Settings.AddBlendable(VisionMID, 1.0f);
		}
	}

	ApplyLookSettings();
}

void UVisionFireSourceComponent::UpdateCameraParams()
{
	if (!IsValid(VisionMID))
	{
		return;
	}

	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	APlayerController* PC = OwnerCharacter ? Cast<APlayerController>(OwnerCharacter->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	const FRotationMatrix CameraAxes(PC->PlayerCameraManager->GetCameraRotation());
	const FVector Forward = CameraAxes.GetScaledAxis(EAxis::X);
	const FVector Right = CameraAxes.GetScaledAxis(EAxis::Y);

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PC->GetViewportSize(ViewportX, ViewportY);
	const float Aspect = (ViewportY > 0) ? static_cast<float>(ViewportX) / static_cast<float>(ViewportY) : 1.7778f;
	// The camera's FOV is the horizontal one.
	const float TanHalfFov = FMath::Tan(FMath::DegreesToRadians(PC->PlayerCameraManager->GetFOVAngle() * 0.5f));

	// The floor under the player (the level's floor height the fog lies on).
	float FloorZ = CameraLocation.Z - 160.0f;
	if (const UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
	{
		FloorZ = OwnerCharacter->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	}

	VisionMID->SetVectorParameterValue(TEXT("C1"), FLinearColor(CameraLocation.X, CameraLocation.Y, CameraLocation.Z, TanHalfFov));
	VisionMID->SetVectorParameterValue(TEXT("C2"), FLinearColor(Forward.X, Forward.Y, Forward.Z, Aspect));
	VisionMID->SetVectorParameterValue(TEXT("C3"), FLinearColor(Right.X, Right.Y, Right.Z, FloorZ));
}

void UVisionFireSourceComponent::ApplyLookSettings()
{
	if (!IsValid(LookPostProcess))
	{
		return;
	}

	FPostProcessSettings& S = LookPostProcess->Settings;

	// Colour: almost monochrome, multiplied by a red-orange gain, with the shadows lifted to dark brown-red.
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(VisionSaturation, VisionSaturation, VisionSaturation, 1.0f);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(VisionColorGain.R, VisionColorGain.G, VisionColorGain.B, 1.0f);
	S.bOverride_ColorOffset = true;
	S.ColorOffset = FVector4(VisionShadowLift.R, VisionShadowLift.G, VisionShadowLift.B, 0.0f);

	// Darker overall, but lights keep glowing (bloom).
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = VisionExposureBias;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = VisionBloomIntensity;

	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = VisionVignetteIntensity;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = VisionGrainIntensity;

	// Blur that grows with distance: a real depth-of-field (sharp at the focal distance) plus a distance blur.
	// Near things stay fairly sharp, far things dissolve.
	S.bOverride_DepthOfFieldDepthBlurAmount = true;
	S.DepthOfFieldDepthBlurAmount = VisionDepthBlurDistanceKm;
	S.bOverride_DepthOfFieldDepthBlurRadius = true;
	S.DepthOfFieldDepthBlurRadius = VisionBlurRadius;
	S.bOverride_DepthOfFieldFstop = true;
	S.DepthOfFieldFstop = VisionBlurFstop;
	S.bOverride_DepthOfFieldFocalDistance = true;
	S.DepthOfFieldFocalDistance = VisionFocalDistance;
}

void UVisionFireSourceComponent::BuildFrameTexture(int32 Seed)
{
	// A procedural "smoke" vignette: transparent in the middle, dark red-brown haze with a ragged edge
	// towards the screen borders, nearly black in the corners. Generated once, so no texture asset is needed
	// (and it is not tied to any editor setup). Stretched over the whole screen by ShowFrameOverlay().
	// Several copies are built with different Seeds (different smoke shapes); the screen cross-fades between
	// them in UpdateLook(), which is what makes the edges slowly move.
	constexpr int32 Width = 512;
	constexpr int32 Height = 288;

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return;
	}
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;

	auto Hash = [](int32 X, int32 Y) -> float
	{
		uint32 N = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u;
		N = (N ^ (N >> 13)) * 1274126177u;
		N ^= (N >> 16);
		return static_cast<float>(N & 0xFFFFFFu) / 16777215.0f;
	};
	auto ValueNoise = [&Hash](float X, float Y) -> float
	{
		const int32 IX = FMath::FloorToInt(X);
		const int32 IY = FMath::FloorToInt(Y);
		float FX = X - IX;
		float FY = Y - IY;
		FX = FX * FX * (3.0f - 2.0f * FX);
		FY = FY * FY * (3.0f - 2.0f * FY);
		const float Top = FMath::Lerp(Hash(IX, IY), Hash(IX + 1, IY), FX);
		const float Bottom = FMath::Lerp(Hash(IX, IY + 1), Hash(IX + 1, IY + 1), FX);
		return FMath::Lerp(Top, Bottom, FY);
	};
	auto Fbm = [&ValueNoise](float X, float Y) -> float
	{
		float Sum = 0.0f;
		float Amplitude = 0.5f;
		float Frequency = 1.0f;
		for (int32 Octave = 0; Octave < 4; ++Octave)
		{
			Sum += Amplitude * ValueNoise(X * Frequency, Y * Frequency);
			Frequency *= 2.0f;
			Amplitude *= 0.5f;
		}
		return Sum; // roughly 0..0.94, centred around 0.47
	};

	const float InnerRadius = FrameInnerRadius;
	const float OuterRadius = FMath::Max(FrameOuterRadius, InnerRadius + 0.01f);

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			// -1..1 from the screen centre. The texture is stretched to the screen, so this is an ellipse
			// that follows the screen's own proportions.
			const float U = (static_cast<float>(X) + 0.5f) / Width * 2.0f - 1.0f;
			const float V = (static_cast<float>(Y) + 0.5f) / Height * 2.0f - 1.0f;
			const float Radius = FMath::Sqrt(U * U + V * V);

			// Two scales of noise: big slow clouds plus finer wisps — makes the edge look like smoke.
			const float Clouds = Fbm(U * 2.0f + 7.3f + Seed * 31.7f, V * 2.0f + 1.9f - Seed * 17.3f);
			const float Wisps = Fbm(U * 6.0f + 3.1f - Seed * 23.9f, V * 6.0f + 9.4f + Seed * 41.1f);
			const float Wobble = (Clouds * 0.7f + Wisps * 0.3f - 0.47f) * FrameNoiseAmount * 2.0f;

			const float T = FMath::Clamp((Radius + Wobble - InnerRadius) / (OuterRadius - InnerRadius), 0.0f, 1.0f);
			const float Alpha = FMath::SmoothStep(0.0f, 1.0f, T);

			// Red-ish at the inner edge of the haze, nearly black deeper in.
			const FLinearColor Color = FMath::Lerp(FrameInnerColor, FrameOuterColor, FMath::Clamp(T * 1.4f, 0.0f, 1.0f));
			FColor Pixel = Color.ToFColor(true);
			Pixel.A = static_cast<uint8>(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
			Pixels[Y * Width + X] = Pixel;
		}
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	FrameTextures.Add(Texture);
}

UTexture2D* UVisionFireSourceComponent::BuildVeinsTexture(float ThicknessMultiplier)
{
	// Procedural network of thin blood vessels: trunks start at the screen edges (mostly near the corners),
	// wander inwards with a smooth random bend, split into thinner and shorter branches and fade out at their
	// tips. Painted into a coverage buffer with soft round dabs, blurred a little (vessels in the reference are
	// slightly soft), then written as a dark-red texture whose alpha is the coverage.
	constexpr int32 Width = 1024;
	constexpr int32 Height = 576;

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;

	TArray<float> Coverage;
	Coverage.Init(0.0f, Width * Height);
	FRandomStream Rand(VeinsSeed);

	auto Dab = [&](float CenterX, float CenterY, float Radius, float Strength)
	{
		const int32 Reach = FMath::CeilToInt(Radius + 1.0f);
		const int32 MinX = FMath::Max(0, FMath::FloorToInt(CenterX) - Reach);
		const int32 MaxX = FMath::Min(Width - 1, FMath::FloorToInt(CenterX) + Reach);
		const int32 MinY = FMath::Max(0, FMath::FloorToInt(CenterY) - Reach);
		const int32 MaxY = FMath::Min(Height - 1, FMath::FloorToInt(CenterY) + Reach);
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const float DX = (static_cast<float>(X) + 0.5f) - CenterX;
				const float DY = (static_cast<float>(Y) + 0.5f) - CenterY;
				const float Distance = FMath::Sqrt(DX * DX + DY * DY);
				const float Value = FMath::Clamp(Radius + 0.5f - Distance, 0.0f, 1.0f) * Strength;
				float& Cell = Coverage[Y * Width + X];
				Cell = FMath::Max(Cell, Value);
			}
		}
	};

	struct FVeinBranch
	{
		float X;
		float Y;
		float Angle;
		float Thickness;
		float TotalLength;
		float Remaining;
		float Curve;
		float BaseAngle;
		int32 Depth;
	};
	TArray<FVeinBranch> Pending;

	const float HalfWidth = Width * 0.5f;
	const float HalfHeight = Height * 0.5f;
	const float TrunkLength = VeinsReach * HalfWidth;
	const float TrunkThickness = 1.8f * VeinsThickness * ThicknessMultiplier;
	constexpr int32 MaxDepth = 4;

	// ---- Where the trunks start: always in a screen corner, shared evenly between the four corners, so the
	// network does not clump in one place and leave the rest bare. Each trunk then heads towards the screen centre. ----
	for (int32 RootIndex = 0; RootIndex < VeinsRootCount; ++RootIndex)
	{
		const int32 Corner = RootIndex % 4;
		const float CornerX = (Corner & 1) ? static_cast<float>(Width) : 0.0f;
		const float CornerY = (Corner & 2) ? static_cast<float>(Height) : 0.0f;
		// Right in the corner (a small slide only, so the trunks of one corner fan out instead of starting far
		// along the screen edge, where they would run sideways along it).
		const float Slide = FMath::Square(Rand.FRand()) * 0.06f;
		float StartX = CornerX;
		float StartY = CornerY;
		if (Rand.FRand() < 0.5f)
		{
			StartX = (Corner & 1) ? CornerX - Slide * Width : CornerX + Slide * Width;
		}
		else
		{
			StartY = (Corner & 2) ? CornerY - Slide * Height : CornerY + Slide * Height;
		}

		const float TowardsCentre = FMath::Atan2(HalfHeight - StartY, HalfWidth - StartX);
		FVeinBranch Trunk;
		Trunk.X = StartX;
		Trunk.Y = StartY;
		Trunk.Angle = TowardsCentre + Rand.FRandRange(-0.3f, 0.3f);
		Trunk.Thickness = TrunkThickness * Rand.FRandRange(0.8f, 1.2f);
		// Long enough to reach the bright part of the haze from the corner (short trunks would only ever
		// exist in the black rim, where a dark line cannot be seen).
		Trunk.TotalLength = TrunkLength * Rand.FRandRange(0.9f, 1.3f);
		Trunk.Remaining = Trunk.TotalLength;
		Trunk.Curve = 0.0f;
		Trunk.BaseAngle = Trunk.Angle;
		Trunk.Depth = 0;
		Pending.Add(Trunk);
	}

	// ---- Grow every branch step by step; splits are queued as new branches ----
	constexpr float StepLength = 1.5f;
	int32 SafetyCounter = 0;
	while (Pending.Num() > 0 && SafetyCounter < 600)
	{
		++SafetyCounter;
		FVeinBranch Branch = Pending.Pop();

		while (Branch.Remaining > 0.0f && Branch.Thickness > 0.22f * TrunkThickness)
		{
			// Smooth wandering: the bend itself changes slowly, so the vessel curves instead of jittering.
			// The bend changes slowly, and a weak pull towards the branch's own main direction keeps it from
			// curling into loops: it meanders gently instead of spiralling.
			Branch.Curve = Branch.Curve * 0.95f + Rand.FRandRange(-0.012f, 0.012f) * VeinsWiggle;
			Branch.Angle += Branch.Curve + Rand.FRandRange(-0.015f, 0.015f) * VeinsWiggle;
			Branch.Angle += (Branch.BaseAngle - Branch.Angle) * 0.02f;
			Branch.X += FMath::Cos(Branch.Angle) * StepLength;
			Branch.Y += FMath::Sin(Branch.Angle) * StepLength;
			Branch.Remaining -= StepLength;
			Branch.Thickness *= 0.997f;

			if (Branch.X < -30.0f || Branch.X > Width + 30.0f || Branch.Y < -30.0f || Branch.Y > Height + 30.0f)
			{
				break;
			}

			// Fades out over the last quarter of its length, and thin branches are fainter than trunks.
			const float TipFade = FMath::SmoothStep(0.0f, 0.25f, Branch.Remaining / FMath::Max(Branch.TotalLength, 1.0f));
			const float Strength = FMath::Clamp(0.35f + 0.65f * Branch.Thickness / TrunkThickness, 0.0f, 1.0f) * TipFade;
			Dab(Branch.X, Branch.Y, Branch.Thickness * 0.5f, Strength);

			// Like a real branch: no splitting right at the root and none at the very tip, so a few clear
			// side twigs come off the middle of the trunk instead of a bush.
			const float AlongBranch = 1.0f - Branch.Remaining / FMath::Max(Branch.TotalLength, 1.0f);
			if (Branch.Depth < MaxDepth && Branch.Thickness > 0.35f * TrunkThickness && Pending.Num() < 400
				&& AlongBranch > 0.15f && AlongBranch < 0.75f
				&& Rand.FRand() < 0.045f * VeinsBranching)
			{
				FVeinBranch Child;
				Child.X = Branch.X;
				Child.Y = Branch.Y;
				Child.Angle = Branch.Angle + ((Rand.FRand() < 0.5f) ? -1.0f : 1.0f) * Rand.FRandRange(0.25f, 0.55f);
				Child.Thickness = Branch.Thickness * 0.65f;
				Child.TotalLength = Branch.Remaining * Rand.FRandRange(0.3f, 0.6f) + 8.0f;
				Child.Remaining = Child.TotalLength;
				Child.Curve = 0.0f;
				Child.BaseAngle = Child.Angle;
				Child.Depth = Branch.Depth + 1;
				Pending.Add(Child);
				Branch.Thickness *= 0.9f;
			}
		}
	}

	// ---- One soft 3x3 blur so the lines are not razor sharp ----
	TArray<float> Blurred;
	Blurred.SetNumZeroed(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float Sum = 0.0f;
			float Weight = 0.0f;
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
				{
					const int32 SampleX = FMath::Clamp(X + OffsetX, 0, Width - 1);
					const int32 SampleY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
					const float W = (OffsetX == 0 && OffsetY == 0) ? 2.0f : 1.0f;
					Sum += Coverage[SampleY * Width + SampleX] * W;
					Weight += W;
				}
			}
			Blurred[Y * Width + X] = Sum / Weight;
		}
	}

	// The picture itself is pure WHITE with the vein shape in the alpha channel; the actual colour is applied as
	// a tint when drawing (see ShowVeinsOverlay()). White is the same value in every colour space, so there is no
	// gamma guesswork about how Slate treats the texture's bytes (a dark colour baked into the texture came out
	// beige on screen), and the colour can be tuned live. White in every pixel also means the bilinear filter
	// never bleeds black into the lines.
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	const float FadeInStart = VeinsInnerRadius;
	const float FadeInEnd = VeinsInnerRadius + 0.1f;
	const float FadeOutEnd = FMath::Max(VeinsOuterRadius, FadeInEnd + 0.05f);
	const float FadeOutStart = FadeOutEnd - 0.15f;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			// Same elliptical "distance from the screen centre" as the frame texture uses.
			const float U = (static_cast<float>(X) + 0.5f) / Width * 2.0f - 1.0f;
			const float V = (static_cast<float>(Y) + 0.5f) / Height * 2.0f - 1.0f;
			const float Radius = FMath::Sqrt(U * U + V * V);
			const float RingMask = FMath::SmoothStep(FadeInStart, FadeInEnd, Radius)
				* (1.0f - FMath::SmoothStep(FadeOutStart, FadeOutEnd, Radius));

			const int32 Index = Y * Width + X;
			FColor Pixel(255, 255, 255, 255);
			Pixel.A = static_cast<uint8>(FMath::Clamp(Blurred[Index] * 2.2f * RingMask, 0.0f, 1.0f) * 255.0f + 0.5f);
			Pixels[Index] = Pixel;
		}
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

void UVisionFireSourceComponent::ShowVeinsOverlay()
{
	if (!bShowVeins || VeinsWidget.IsValid() || !IsValid(VeinsTexture))
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return;
	}

	VeinsBrush.SetResourceObject(VeinsTexture);
	VeinsBrush.ImageSize = FVector2D(VeinsTexture->GetSizeX(), VeinsTexture->GetSizeY());
	VeinsBrush.DrawAs = ESlateBrushDrawType::Image;

	TWeakObjectPtr<UVisionFireSourceComponent> WeakThis(this);

	// Tremble: the network does not travel anywhere, it only quivers — a tiny turn and a tiny zoom, each
	// made of a few sine waves of different speed, so the shaking never looks like a regular pattern.
	// Shared by both layers (the normal veins and the thicker "swollen" copy) so they always stay on top of each other.
	// ExtraZoom is the swell layer's own growth: the same texture a hair bigger gives the veins a thicker twin.
	auto MakeTrembleTransform = [WeakThis](float ExtraZoom)
	{
		return [WeakThis, ExtraZoom]() -> TOptional<FSlateRenderTransform>
	{
		UWorld* LayerWorld = WeakThis.IsValid() ? WeakThis->GetWorld() : nullptr;
		if (!LayerWorld)
		{
			return TOptional<FSlateRenderTransform>();
		}
		const float Time = LayerWorld->GetTimeSeconds() * WeakThis->VeinsTrembleSpeed;
		const float Amount = WeakThis->VeinsTrembleAmount;
		const float AngleDegrees = Amount
			* (0.6f * FMath::Sin(Time * 7.3f + 0.8f) + 0.3f * FMath::Sin(Time * 11.7f + 1.9f) + 0.1f * FMath::Sin(Time * 17.9f + 0.4f));
		const float Zoom = 1.1f + ExtraZoom + 0.01f * Amount
			* (0.6f * FMath::Sin(Time * 9.1f + 2.2f) + 0.4f * FMath::Sin(Time * 13.3f + 0.3f));
		return TOptional<FSlateRenderTransform>(FSlateRenderTransform(
			Concatenate(FScale2D(Zoom), FQuat2D(FMath::DegreesToRadians(AngleDegrees)))));
		};
	};

	TSharedRef<SOverlay> Overlay = SNew(SOverlay);
	Overlay->AddSlot()
	[
		SNew(SImage)
		.Image(&VeinsBrush)
		.ColorAndOpacity_Lambda([WeakThis]() -> FSlateColor
		{
			if (!WeakThis.IsValid())
			{
				return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
			}
			const FLinearColor& Tint = WeakThis->VeinsColor;
			return FSlateColor(FLinearColor(Tint.R, Tint.G, Tint.B, WeakThis->VeinsCurrentAlpha));
		})
		.RenderTransformPivot(FVector2D(0.5, 0.5))
		.RenderTransform_Lambda(MakeTrembleTransform(0.0f))
	];

	if (IsValid(VeinsSwellTexture) && (!bVeinsFromAsset || VeinsPulseSwell > 0.0f))
	{
		VeinsSwellBrush.SetResourceObject(VeinsSwellTexture);
		VeinsSwellBrush.ImageSize = FVector2D(VeinsSwellTexture->GetSizeX(), VeinsSwellTexture->GetSizeY());
		VeinsSwellBrush.DrawAs = ESlateBrushDrawType::Image;
		Overlay->AddSlot()
		[
			SNew(SImage)
			.Image(&VeinsSwellBrush)
			.ColorAndOpacity_Lambda([WeakThis]() -> FSlateColor
			{
				if (!WeakThis.IsValid())
				{
					return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
				}
				const FLinearColor& Tint = WeakThis->VeinsColor;
				return FSlateColor(FLinearColor(Tint.R, Tint.G, Tint.B, WeakThis->VeinsSwellAlpha));
			})
			.RenderTransformPivot(FVector2D(0.5, 0.5))
			.RenderTransform_Lambda(MakeTrembleTransform(bVeinsFromAsset ? 0.04f * VeinsPulseSwell : 0.0f))
		];
	}

	VeinsWidget = Overlay;
	VeinsWidget->SetVisibility(EVisibility::HitTestInvisible);

	// Z-order 21 = on top of the frame (20), 19 = just under it (the dark haze then covers the veins).
	ViewportClient->AddViewportWidgetContent(VeinsWidget.ToSharedRef(), bVeinsAboveFrame ? 21 : 19);
}

void UVisionFireSourceComponent::HideVeinsOverlay()
{
	if (!VeinsWidget.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr)
	{
		ViewportClient->RemoveViewportWidgetContent(VeinsWidget.ToSharedRef());
	}
	VeinsWidget.Reset();
}

void UVisionFireSourceComponent::ShowFrameOverlay()
{
	if (FrameWidget.IsValid() || FrameTextures.Num() < FrameStateCount)
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return;
	}

	// One full-screen image per smoke shape, stacked; UpdateLook() sets each layer's opacity so that the
	// screen cross-fades from one shape to the next.
	TWeakObjectPtr<UVisionFireSourceComponent> WeakThis(this);
	TSharedRef<SOverlay> Overlay = SNew(SOverlay);
	for (int32 LayerIndex = 0; LayerIndex < FrameStateCount; ++LayerIndex)
	{
		UTexture2D* LayerTexture = FrameTextures[LayerIndex];
		if (!IsValid(LayerTexture))
		{
			continue;
		}
		FrameBrushes[LayerIndex].SetResourceObject(LayerTexture);
		FrameBrushes[LayerIndex].ImageSize = FVector2D(LayerTexture->GetSizeX(), LayerTexture->GetSizeY());
		FrameBrushes[LayerIndex].DrawAs = ESlateBrushDrawType::Image;

		Overlay->AddSlot()
		[
			SNew(SImage)
			.Image(&FrameBrushes[LayerIndex])
			.ColorAndOpacity_Lambda([WeakThis, LayerIndex]() -> FSlateColor
			{
				const float Alpha = WeakThis.IsValid() ? WeakThis->FrameLayerAlpha[LayerIndex] : 0.0f;
				return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
			})
			// Each layer slowly turns and zooms in its own rhythm (neighbouring layers turn in opposite
			// directions), so the smoke visibly rolls instead of just fading. Drawn larger than the screen
			// (FrameOverscan) so the turning never reveals the screen corners.
			.RenderTransformPivot(FVector2D(0.5, 0.5))
			.RenderTransform_Lambda([WeakThis, LayerIndex]() -> TOptional<FSlateRenderTransform>
			{
				UWorld* LayerWorld = WeakThis.IsValid() ? WeakThis->GetWorld() : nullptr;
				if (!LayerWorld)
				{
					return TOptional<FSlateRenderTransform>();
				}
				const float Time = LayerWorld->GetTimeSeconds() * WeakThis->FrameSwirlSpeed;
				const float Phase = static_cast<float>(LayerIndex) * 2.1f;
				const float Direction = (LayerIndex % 2 == 0) ? 1.0f : -1.0f;
				const float AngleDegrees = WeakThis->FrameSwirlAmount * Direction
					* (FMath::Sin(Time * 0.37f + Phase) + 0.4f * FMath::Sin(Time * 0.91f + Phase * 1.7f));
				const float Zoom = WeakThis->FrameOverscan + 0.05f * FMath::Sin(Time * 0.43f + Phase * 1.3f);
				return TOptional<FSlateRenderTransform>(FSlateRenderTransform(
					Concatenate(FScale2D(Zoom), FQuat2D(FMath::DegreesToRadians(AngleDegrees)))));
			})
		];
	}

	FrameWidget = Overlay;
	FrameWidget->SetVisibility(EVisibility::HitTestInvisible);

	// Z-order 20: above the HUD, so the haze covers everything the player sees.
	ViewportClient->AddViewportWidgetContent(FrameWidget.ToSharedRef(), 20);
}

void UVisionFireSourceComponent::HideFrameOverlay()
{
	if (!FrameWidget.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr)
	{
		ViewportClient->RemoveViewportWidgetContent(FrameWidget.ToSharedRef());
	}
	FrameWidget.Reset();
}

void UVisionFireSourceComponent::UpdateLook(float DeltaTime)
{
	const float TargetWeight = bIsVisionActive ? 1.0f : 0.0f;
	LookWeight = FMath::FInterpTo(LookWeight, TargetWeight, DeltaTime, VisionLookBlendSpeed);
	if (FMath::IsNearlyEqual(LookWeight, TargetWeight, 0.01f))
	{
		LookWeight = TargetWeight;
	}

	if (IsValid(LookPostProcess))
	{
		// Disabled entirely at weight 0, so nothing of this look can linger while the ability is off.
		LookPostProcess->bEnabled = LookWeight > 0.0f;
		LookPostProcess->BlendWeight = LookWeight;
		if (LookWeight > 0.0f)
		{
			// Re-applied every frame while visible, so values tuned in the Blueprint/PIE show up live.
			ApplyLookSettings();
			UpdateCameraParams();
		}
	}

	if (LookWeight > 0.0f)
	{
		const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float Breath = 1.0f + FrameBreathing * FMath::Sin(Time * 0.9f);
		FrameCurrentAlpha = FMath::Clamp(LookWeight * FrameOpacity * Breath, 0.0f, 1.0f);

		// Slowly cross-fade between the pre-built smoke shapes: the current one fades out while the next one
		// fades in (square-root weights keep the haze from thinning out in the middle of a transition).
		const float StatePosition = FMath::Fmod(Time * FrameMorphSpeed, static_cast<float>(FrameStateCount));
		const int32 CurrentState = FMath::Clamp(FMath::FloorToInt(StatePosition), 0, FrameStateCount - 1);
		const float Fade = FMath::SmoothStep(0.0f, 1.0f, StatePosition - static_cast<float>(CurrentState));
		for (int32 LayerIndex = 0; LayerIndex < FrameStateCount; ++LayerIndex)
		{
			FrameLayerAlpha[LayerIndex] = 0.0f;
		}
		FrameLayerAlpha[CurrentState] = FrameCurrentAlpha * FMath::Sqrt(1.0f - Fade);
		FrameLayerAlpha[(CurrentState + 1) % FrameStateCount] = FrameCurrentAlpha * FMath::Sqrt(Fade);
		ShowFrameOverlay();

		// Veins under the frame: slow pulse of brightness (two sine waves of different speed, so it never
		// looks like a metronome).
		const float PulsePhase = Time * VeinsPulseSpeed * 6.2831853f;
		const float PulseWave = 0.65f * FMath::Sin(PulsePhase) + 0.35f * FMath::Sin(PulsePhase * 0.37f + 1.1f);
		const float Pulse = 1.0f - VeinsPulseDepth * 0.5f * (1.0f - PulseWave);
		VeinsCurrentAlpha = FMath::Clamp(LookWeight * VeinsOpacity * Pulse, 0.0f, 1.0f);
		// The thicker copy fades in at the peak of the pulse and out at its low point: the veins swell with the beat.
		const float Swell = (VeinsPulseDepth > 0.001f) ? FMath::Clamp((Pulse - (1.0f - VeinsPulseDepth)) / VeinsPulseDepth, 0.0f, 1.0f) : 0.0f;
		VeinsSwellAlpha = FMath::Clamp(LookWeight * VeinsOpacity * Swell, 0.0f, 1.0f);
		ShowVeinsOverlay();

		// Fingers + flame. The flame flickers: a few sine waves of different speed, so it never looks periodic.
		FingersCurrentAlpha = FMath::Clamp(LookWeight * FingersOpacity, 0.0f, 1.0f);
		const float Wave = 0.5f * FMath::Sin(Time * 17.0f) + 0.3f * FMath::Sin(Time * 29.0f + 1.3f) + 0.2f * FMath::Sin(Time * 7.1f + 0.4f);
		FingersBrightness = 1.0f - FingersFlicker * 0.5f * (1.0f - Wave);
		FlameCurrentAlpha = FMath::Clamp(LookWeight * FlameGlowOpacity * (1.0f - FlameFlicker * 0.35f + FlameFlicker * 0.3f * Wave), 0.0f, 1.0f);
		const bool bFingertipGlow = bUse3DHand && bFireOnFingertip && bFingertipFlameGlow;
		if ((bShowFingers && !bUse3DHand) || bFingertipGlow)
		{
			ShowFingersOverlay();
		}
		else
		{
			HideFingersOverlay();
		}
	}
	else
	{
		FrameCurrentAlpha = 0.0f;
		VeinsCurrentAlpha = 0.0f;
		VeinsSwellAlpha = 0.0f;
		for (int32 LayerIndex = 0; LayerIndex < FrameStateCount; ++LayerIndex)
		{
			FrameLayerAlpha[LayerIndex] = 0.0f;
		}
		FingersCurrentAlpha = 0.0f;
		FlameCurrentAlpha = 0.0f;
		HideFrameOverlay();
		HideVeinsOverlay();
		HideFingersOverlay();
	}
}

// ---------------------------------------------------------------------------------------------------------
// Fingers with the flame (2D overlay)
// ---------------------------------------------------------------------------------------------------------

namespace
{
	// Wraps a finished pixel array into a transient texture (same recipe as UVisionFireSourceComponent::BuildFrameTexture()).
	UTexture2D* MakeTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	// Distance from point P to the segment A-B, minus a radius that goes from RadiusA (at A) to RadiusB (at B).
	// Negative = inside the finger.
	float FingerDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B, float RadiusA, float RadiusB)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FVector2D::DotProduct(AB, AB), 0.0f, 1.0f);
		const FVector2D Closest = A + AB * T;
		return static_cast<float>(FVector2D::Distance(P, Closest)) - FMath::Lerp(RadiusA, RadiusB, T);
	}
}

void UVisionFireSourceComponent::BuildFingersTextures()
{
	// Your own fingers picture: if nothing is assigned in the Blueprint, look for the imported asset
	// /Game/T_VisionFingers (quietly — it is fine if it does not exist yet, the placeholder is used then).
	if (!IsValid(FingersTexture))
	{
		FingersTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/T_VisionFingers.T_VisionFingers"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	// ---- Placeholder fingers: two dark fingers rising from the bottom, tips close together, lit by a flame
	// that hovers between and above the tips (that is where the flame picture is drawn). Pixel space 512x400.
	{
		constexpr int32 Width = 512;
		constexpr int32 Height = 400;
		const FVector2D FlamePos(256.0, 70.0);

		auto SceneDistance = [](const FVector2D& P) -> float
		{
			const float DistA = FingerDistance(P, FVector2D(160.0, 470.0), FVector2D(200.0, 110.0), 60.0f, 36.0f);
			const float DistB = FingerDistance(P, FVector2D(352.0, 470.0), FVector2D(312.0, 90.0), 62.0f, 38.0f);
			return FMath::Min(DistA, DistB);
		};

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FVector2D P(X + 0.5, Y + 0.5);
				const float D = SceneDistance(P);

				// Soft edge (about 9 px) — the fingers are out of focus on the reference.
				const float Coverage = FMath::Clamp(0.5f - D / 9.0f, 0.0f, 1.0f);

				// Outward normal from the distance field (finite differences).
				FVector2D Normal(SceneDistance(P + FVector2D(2.0, 0.0)) - SceneDistance(P - FVector2D(2.0, 0.0)),
					SceneDistance(P + FVector2D(0.0, 2.0)) - SceneDistance(P - FVector2D(0.0, 2.0)));
				Normal = Normal.GetSafeNormal();
				const FVector2D ToFlame = (FlamePos - P).GetSafeNormal();
				const float Facing = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Normal, ToFlame)) * 0.5f + 0.5f, 0.0f, 1.0f);

				const float Rim = FMath::Exp(FMath::Min(D, 0.0f) / 11.0f);          // 1 at the edge, fading inwards
				const float Proximity = 1.0f / (1.0f + FMath::Square(static_cast<float>(FVector2D::Distance(P, FlamePos)) / 150.0f));
				const float RimLight = Rim * FMath::Square(Facing) * (0.25f + 1.1f * Proximity);
				const float Translucency = Proximity * 0.07f;                         // flesh glowing from inside, near the flame

				const FLinearColor Base(0.012f, 0.004f, 0.002f);
				const FLinearColor RimColor(1.0f, 0.22f, 0.05f);
				const FLinearColor Color = Base + RimColor * (RimLight + Translucency);

				FColor Pixel = Color.ToFColor(true);
				Pixel.A = static_cast<uint8>(Coverage * 255.0f + 0.5f);
				Pixels[Y * Width + X] = Pixel;
			}
		}
		FingersPlaceholderTexture = MakeTextureFromPixels(Pixels, Width, Height);
	}

	// ---- Flame: a soft orange halo with a teardrop-shaped bright core, tip pointing up. 256x256.
	{
		constexpr int32 Size = 256;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float Dx = X + 0.5f - 128.0f;
				const float Dy = Y + 0.5f - 150.0f;

				// Halo
				const float HaloRadius = FMath::Sqrt(Dx * Dx + Dy * Dy);
				// The halo fades to exactly zero before the picture's border, so no square edge is ever visible.
				const float HaloFade = FMath::Clamp(1.0f - HaloRadius / 124.0f, 0.0f, 1.0f);
				const float HaloAlpha = FMath::Clamp((0.55f * FMath::Exp(-FMath::Square(HaloRadius / 56.0f)) + 0.2f * FMath::Exp(-FMath::Square(HaloRadius / 100.0f))) * HaloFade, 0.0f, 1.0f);
				const FLinearColor HaloColor(1.0f, 0.28f, 0.05f);

				// Flame body: t = 0 at the tip (y = 45), 1 at the base (y = 200)
				const float T = (Y + 0.5f - 45.0f) / 155.0f;
				float FlameAlpha = 0.0f;
				float CoreMix = 0.0f;
				if (T > 0.0f && T < 1.0f)
				{
					const float HalfWidth = 95.0f * FMath::Pow(T, 0.75f) * FMath::Pow(1.0f - T, 0.35f);
					if (HalfWidth > 0.5f)
					{
						const float Inside = 1.0f - FMath::Abs(Dx) / HalfWidth;
						if (Inside > 0.0f)
						{
							FlameAlpha = FMath::Clamp(Inside * 2.2f, 0.0f, 1.0f);
							CoreMix = FMath::Pow(Inside, 1.6f);
						}
					}
				}
				const FLinearColor FlameEdge(1.0f, 0.38f, 0.06f);
				const FLinearColor FlameCore(1.0f, 0.9f, 0.55f);
				const FLinearColor FlameColor = FMath::Lerp(FlameEdge, FlameCore, CoreMix);

				// Flame over halo.
				const float OutAlpha = FlameAlpha + HaloAlpha * (1.0f - FlameAlpha);
				FLinearColor OutColor = FLinearColor::Black;
				if (OutAlpha > 0.0001f)
				{
					OutColor = (FlameColor * FlameAlpha + HaloColor * HaloAlpha * (1.0f - FlameAlpha)) / OutAlpha;
				}
				FColor Pixel = OutColor.ToFColor(true);
				Pixel.A = static_cast<uint8>(FMath::Clamp(OutAlpha, 0.0f, 1.0f) * 255.0f + 0.5f);
				Pixels[Y * Size + X] = Pixel;
			}
		}
		FlameGlowTexture = MakeTextureFromPixels(Pixels, Size, Size);
	}
}

void UVisionFireSourceComponent::ShowFingersOverlay()
{
	if (FingersWidget.IsValid())
	{
		return;
	}

	UTexture2D* FingersImage = IsValid(FingersTexture) ? FingersTexture.Get() : FingersPlaceholderTexture.Get();
	UWorld* World = GetWorld();
	UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	if (!ViewportClient || !FingersImage)
	{
		return;
	}

	FingersBrush.SetResourceObject(FingersImage);
	FingersBrush.ImageSize = FVector2D(FingersImage->GetSizeX(), FingersImage->GetSizeY());
	FingersBrush.DrawAs = ESlateBrushDrawType::Image;

	TWeakObjectPtr<UVisionFireSourceComponent> WeakThis(this);

	// The boxes are anchored with SCREEN FRACTIONS (min/max corner), so the layout is the same at any
	// resolution; the SScaleBox keeps the picture's own proportions inside its box. Boxes are re-read from the
	// properties every frame through the Anchors lambda, so dragging the values in PIE moves things live.
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Anchors(TAttribute<FAnchors>::CreateLambda([WeakThis]() -> FAnchors
		{
			if (!WeakThis.IsValid())
			{
				return FAnchors(0.0f, 0.0f, 0.0f, 0.0f);
			}
			// Slow drift of the whole hand (see ComputeFingersSway()).
			const FVector2D Sway = WeakThis->ComputeFingersSway();
			const float OffsetX = static_cast<float>(Sway.X);
			const float OffsetY = static_cast<float>(Sway.Y);
			return FAnchors(WeakThis->FingersScreenMin.X + OffsetX, WeakThis->FingersScreenMin.Y + OffsetY, WeakThis->FingersScreenMax.X + OffsetX, WeakThis->FingersScreenMax.Y + OffsetY);
		}))
		.Offset(FMargin(0.0f))
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SImage)
				.Image(&FingersBrush)
				.ColorAndOpacity_Lambda([WeakThis]() -> FSlateColor
				{
					// In 3D-hand mode the flat hand picture is not drawn (only the flame below is).
					const float Alpha = (WeakThis.IsValid() && !WeakThis->bUse3DHand) ? WeakThis->FingersCurrentAlpha : 0.0f;
					const float Brightness = WeakThis.IsValid() ? WeakThis->FingersBrightness : 1.0f;
					return FSlateColor(FLinearColor(Brightness, Brightness, Brightness, Alpha));
				})
			]
		];

	if ((bUse3DHand ? bFingertipFlameGlow : bDrawFlameGlow) && IsValid(FlameGlowTexture))
	{
		FlameBrush.SetResourceObject(FlameGlowTexture);
		FlameBrush.ImageSize = FVector2D(FlameGlowTexture->GetSizeX(), FlameGlowTexture->GetSizeY());
		FlameBrush.DrawAs = ESlateBrushDrawType::Image;

		Canvas->AddSlot()
		.Anchors(TAttribute<FAnchors>::CreateLambda([WeakThis]() -> FAnchors
		{
			if (!WeakThis.IsValid())
			{
				return FAnchors(0.0f, 0.0f, 0.0f, 0.0f);
			}
			const bool b3D = WeakThis->bUse3DHand;
			const FVector2D Center = b3D ? WeakThis->FingertipScreenCenter : WeakThis->FlameScreenCenter;
			const FVector2D Half = b3D ? WeakThis->FingertipFlameHalfExtent : WeakThis->FlameScreenHalfExtent;
			return FAnchors(Center.X - Half.X, Center.Y - Half.Y, Center.X + Half.X, Center.Y + Half.Y);
		}))
		.Offset(FMargin(0.0f))
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SImage)
				.Image(&FlameBrush)
				.ColorAndOpacity_Lambda([WeakThis]() -> FSlateColor
				{
					const float Alpha = WeakThis.IsValid() ? WeakThis->FlameCurrentAlpha : 0.0f;
					return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
				})
			]
		];
	}

	FingersWidget = Canvas;
	FingersWidget->SetVisibility(EVisibility::HitTestInvisible);

	// Z-order 19: under the smoky frame (20), above the HUD.
	ViewportClient->AddViewportWidgetContent(FingersWidget.ToSharedRef(), 19);
}

void UVisionFireSourceComponent::HideFingersOverlay()
{
	if (!FingersWidget.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr)
	{
		ViewportClient->RemoveViewportWidgetContent(FingersWidget.ToSharedRef());
	}
	FingersWidget.Reset();
}
