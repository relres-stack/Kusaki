// Fill out your copyright notice in the Description page of Project Settings.

#include "IntroSceneDirector.h"

#include "BaseCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

AIntroSceneDirector::AIntroSceneDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;   // ticks only while the scene runs
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIntroSceneDirector::BeginPlay()
{
	Super::BeginPlay();

	if (!bPlayOnBeginPlay)
	{
		UE_LOG(LogTemp, Warning, TEXT("IntroScene: disabled (bPlayOnBeginPlay = false)"));
		Phase = EIntroPhase::Done;
		return;
	}
	TryStartScene();
}

void AIntroSceneDirector::TryStartScene()
{
	// The player pawn may not be possessed yet on the very first frame: retry a few times.
	PC = UGameplayStatics::GetPlayerController(this, 0);
	Player = PC ? Cast<ABaseCharacter>(PC->GetPawn()) : nullptr;

	if (!IsValid(PC) || !IsValid(Player))
	{
		if (++StartAttempts > 40)
		{
			UE_LOG(LogTemp, Warning, TEXT("IntroScene: no player pawn found, scene skipped"));
			ShowDebug(501, 8.0f, FColor::Red, TEXT("IntroScene: no ABaseCharacter pawn on player 0 - scene skipped"));
			Phase = EIntroPhase::Done;
			return;
		}
		GetWorldTimerManager().SetTimer(StartRetryHandle, this, &AIntroSceneDirector::TryStartScene, 0.1f, false);
		return;
	}
	StartScene();
}

void AIntroSceneDirector::StartScene()
{
	if (Waypoints.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("IntroScene: needs at least 2 waypoints, scene skipped"));
		ShowDebug(501, 8.0f, FColor::Red, FString::Printf(TEXT("IntroScene: %d waypoints (need 2+) - scene skipped"), Waypoints.Num()));
		Phase = EIntroPhase::Done;
		return;
	}

	UCharacterMovementComponent* MoveComp = Player->GetCharacterMovement();
	SavedMaxWalkSpeed = MoveComp ? MoveComp->MaxWalkSpeed : 0.0f;

	// Lock the player out: the pawn's own input component is taken off the stack (no action fires),
	// move/look are ignored on the controller as well. Both are undone in FinishScene().
	Player->DisableInput(PC);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	// Aiming reads RMB straight from the key state (bypasses DisableInput), so it is switched off separately:
	// no crosshair dot, no aim zoom, no interaction prompt during the scene.
	Player->SetAimSuppressed(true);

	// Heading = direction of the last segment (the hero ends the walk facing the village).
	const int32 Last = Waypoints.Num() - 1;
	HeadingDir = (Waypoints[Last] - Waypoints[Last - 1]).GetSafeNormal2D();
	if (HeadingDir.IsNearlyZero())
	{
		HeadingDir = FVector::ForwardVector;
	}

	// Put the hero on the first waypoint, facing the second one. Waypoints are ground points, the actor
	// location is the capsule centre.
	const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Dir0 = (Waypoints[1] - Waypoints[0]).GetSafeNormal2D();
	Player->SetActorLocationAndRotation(Waypoints[0] + FVector(0, 0, HalfHeight + 5.0f), Dir0.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (MoveComp)
	{
		MoveComp->StopMovementImmediately();
	}

	BuildRoute();

	// Camera path: starts far in front of the final standing point and high above it, ends (they meet) right in
	// front of the hero's face. See UpdateApproachCamera().
	const FVector Final = Waypoints[Last];
	ApproachEnd = Final + HeadingDir * FrontCameraDistance + FVector(0, 0, FrontCameraHeight);
	const FVector ApproachDir = HeadingDir.RotateAngleAxis(-ApproachYawOffset, FVector::UpVector);
	ApproachStart = Final + ApproachDir * ApproachStartDistance + FVector(0, 0, ApproachStartHeight);
	OrbitPivot = Final + FVector(0, 0, HalfHeight);
	WalkProgress = 0.0f;
	CameraProgress = 0.0f;
	ClearanceLift = 0.0f;
	PlayerFOV = Player->GetFollowCamera() ? Player->GetFollowCamera()->FieldOfView : 90.0f;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SceneCamera = GetWorld()->SpawnActor<ACameraActor>(ApproachStart, FRotator::ZeroRotator, Params);
	if (!IsValid(SceneCamera))
	{
		UE_LOG(LogTemp, Warning, TEXT("IntroScene: could not spawn the camera, scene skipped"));
		ShowDebug(501, 8.0f, FColor::Red, TEXT("IntroScene: could not spawn the camera - scene skipped"));
		Player->EnableInput(PC);
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
		Player->SetAimSuppressed(false);
		Phase = EIntroPhase::Done;
		return;
	}
	if (UCameraComponent* Cam = SceneCamera->GetCameraComponent())
	{
		Cam->bConstrainAspectRatio = false;
	}
	UpdateApproachCamera(0.0f, true);
	UpdateCameraLook(0.0f, true);
	PC->SetViewTargetWithBlend(SceneCamera, 0.0f);

	UE_LOG(LogTemp, Warning, TEXT("IntroScene: started — %d waypoints, hero placed at %s, camera from %s to %s"),
		Waypoints.Num(), *Waypoints[0].ToString(), *ApproachStart.ToString(), *ApproachEnd.ToString());

	BeginWalking();
}

void AIntroSceneDirector::BuildRoute()
{
	Route.Reset();
	RouteDist.Reset();
	Route.Add(Waypoints[0]);

	UNavigationSystemV1* NavSys = bUseNavMesh ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	int32 NavSegments = 0;
	for (int32 i = 1; i < Waypoints.Num(); ++i)
	{
		if (NavSys)
		{
			// Corner points of the NavMesh path between two waypoints: the hero goes around whatever stands between them.
			UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(this, Waypoints[i - 1], Waypoints[i], Player);
			if (Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 2)
			{
				for (int32 k = 1; k < Path->PathPoints.Num() - 1; ++k)
				{
					Route.Add(Path->PathPoints[k]);
				}
				++NavSegments;
			}
		}
		Route.Add(Waypoints[i]);   // the waypoint itself always closes the segment
	}

	RouteDist.SetNum(Route.Num());
	RouteDist[0] = 0.0f;
	for (int32 i = 1; i < Route.Num(); ++i)
	{
		RouteDist[i] = RouteDist[i - 1] + FVector::Dist2D(Route[i - 1], Route[i]);
	}

	UE_LOG(LogTemp, Warning, TEXT("IntroScene: route %d points, %d of %d segments through the NavMesh, %.0f cm long"),
		Route.Num(), NavSegments, Waypoints.Num() - 1, RouteDist.Last());
	if (bUseNavMesh && NavSegments == 0)
	{
		ShowDebug(501, 8.0f, FColor::Yellow, TEXT("IntroScene: no NavMesh path - walking straight between waypoints (build navigation?)"));
	}
}

float AIntroSceneDirector::ComputeWalkProgress() const
{
	const float Total = RouteDist.Num() > 0 ? RouteDist.Last() : 0.0f;
	if (Total <= KINDA_SMALL_NUMBER || !Route.IsValidIndex(RouteIndex))
	{
		return 1.0f;
	}
	const float Left = FVector::Dist2D(Player->GetActorLocation(), Route[RouteIndex]);
	return FMath::Clamp((RouteDist[RouteIndex] - Left) / Total, 0.0f, 1.0f);
}

float AIntroSceneDirector::RemainingWalkDistance() const
{
	return bHeroArrived ? 0.0f : (1.0f - WalkProgress) * (RouteDist.Num() > 0 ? RouteDist.Last() : 0.0f);
}

void AIntroSceneDirector::UpdateCameraLook(float DeltaSeconds, bool bSnap)
{
	if (!IsValid(SceneCamera) || !IsValid(Player))
	{
		return;
	}
	const FVector Target = Player->GetActorLocation() + FVector(0, 0, LookAtHeight);
	const FRotator Wanted = UKismetMathLibrary::FindLookAtRotation(SceneCamera->GetActorLocation(), Target);
	CameraLookRotation = bSnap ? Wanted : FMath::RInterpTo(CameraLookRotation, Wanted, DeltaSeconds, 6.0f);
	SceneCamera->SetActorRotation(CameraLookRotation);
}

void AIntroSceneDirector::UpdateApproachCamera(float DeltaSeconds, bool bSnap)
{
	if (IsValid(SceneCamera))
	{
		SceneCamera->SetActorLocation(ComputeApproachPos(DeltaSeconds, bSnap));
	}
}

FVector AIntroSceneDirector::ComputeApproachPos(float DeltaSeconds, bool bSnap)
{
	// Smooth follow of the walk progress: almost no lag while he walks evenly, and a glide (not a jump) if the
	// hero was teleported by the timeout.
	CameraProgress = bSnap ? WalkProgress : FMath::FInterpTo(CameraProgress, WalkProgress, DeltaSeconds, 4.0f);

	// Horizontal: ease in/out, the camera leaves and arrives softly. Height: squared, so it stays high over the
	// hills for longer and swoops down to face height at the end.
	const float XYAlpha = FMath::SmoothStep(0.0f, 1.0f, CameraProgress);
	const float ZAlpha = XYAlpha * XYAlpha;
	FVector Pos(FMath::Lerp(ApproachStart.X, ApproachEnd.X, XYAlpha),
		FMath::Lerp(ApproachStart.Y, ApproachEnd.Y, XYAlpha),
		FMath::Lerp(ApproachStart.Z, ApproachEnd.Z, ZAlpha));

	// Never through a hill or a tree: trace down from high above, keep MinGroundClearance over the first thing hit.
	float NeededLift = 0.0f;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IntroCameraClearance), false, this);
	Query.AddIgnoredActor(Player);
	Query.AddIgnoredActor(SceneCamera);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Pos + FVector(0, 0, 20000.0f), Pos - FVector(0, 0, 20000.0f), ECC_Visibility, Query))
	{
		NeededLift = FMath::Max(Hit.ImpactPoint.Z + MinGroundClearance - Pos.Z, 0.0f);
	}
	// Rise quickly (do not clip into a crest), come back down slowly (no bobbing over each tree).
	ClearanceLift = bSnap ? NeededLift : FMath::FInterpTo(ClearanceLift, NeededLift, DeltaSeconds, NeededLift > ClearanceLift ? 8.0f : 1.5f);
	Pos.Z += ClearanceLift;

	if (UCameraComponent* Cam = IsValid(SceneCamera) ? SceneCamera->GetCameraComponent() : nullptr)
	{
		Cam->SetFieldOfView(FMath::Lerp(ApproachStartFOV, PlayerFOV, XYAlpha));
	}
	return Pos;
}

void AIntroSceneDirector::BeginWalking()
{
	Phase = EIntroPhase::Walking;
	PhaseTime = 0.0f;
	WalkTime = 0.0f;
	bHeroArrived = false;
	RouteIndex = 1;
	SetActorTickEnabled(true);
}

void AIntroSceneDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsValid(Player) || !IsValid(PC))
	{
		SetActorTickEnabled(false);
		return;
	}
	PhaseTime += DeltaSeconds;

	// The walk runs on its own: the orbit may already have started while the hero takes his last steps.
	if (!bHeroArrived)
	{
		WalkTime += DeltaSeconds;
		TickWalking(DeltaSeconds);
	}

	switch (Phase)
	{
	case EIntroPhase::Walking:
		UpdateApproachCamera(DeltaSeconds, false);
		UpdateCameraLook(DeltaSeconds, false);
		if (bHeroArrived)
		{
			if (OrbitLeadTime <= 0.0f && HoldDuration > 0.0f)
			{
				BeginHolding();
			}
			else
			{
				BeginOrbit();
			}
		}
		else if (OrbitLeadTime > 0.0f && RemainingWalkDistance() <= WalkSpeed * OrbitLeadTime)
		{
			BeginOrbit();
		}
		break;
	case EIntroPhase::Holding:
		UpdateApproachCamera(DeltaSeconds, false);
		UpdateCameraLook(DeltaSeconds, false);
		if (PhaseTime >= HoldDuration)
		{
			BeginOrbit();
		}
		break;
	case EIntroPhase::Orbiting:
		TickOrbit(DeltaSeconds);
		break;
	default:
		break;
	}

	// Live state line (key 500 overwrites itself every frame; disappears once the scene stops ticking).
	if (bShowDebug && Phase != EIntroPhase::Done && IsValid(Player))
	{
		FString Extra;
		FColor Color = FColor::Cyan;
		if (!bHeroArrived && Route.IsValidIndex(RouteIndex))
		{
			const float Speed = Player->GetVelocity().Size2D();
			Extra = FString::Printf(TEXT("  point %d/%d, %.0f cm to go, speed %.0f, walk %.0f%% camera %.0f%%, timeout in %.0fs"),
				RouteIndex, Route.Num() - 1, FVector::Dist2D(Player->GetActorLocation(), Route[RouteIndex]),
				Speed, WalkProgress * 100.0f, CameraProgress * 100.0f, FMath::Max(WalkTimeout - WalkTime, 0.0f));
			if (WalkTime > 0.5f && Speed < 10.0f)
			{
				Color = FColor::Yellow;   // should be walking but is not moving: stuck on something
			}
		}
		if (Phase == EIntroPhase::Orbiting)
		{
			Extra += FString::Printf(TEXT("  orbit %.0f%%%s"), FMath::Clamp(PhaseTime / FMath::Max(OrbitDuration, 0.1f), 0.0f, 1.0f) * 100.0f,
				bHeroArrived ? TEXT("") : TEXT(" (hero still walking)"));
		}
		ShowDebug(500, 0.0f, Color, FString::Printf(TEXT("IntroScene: %s  t=%.1fs%s"),
			*UEnum::GetValueAsString(Phase), PhaseTime, *Extra));
	}
}

void AIntroSceneDirector::ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const
{
	if (bShowDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Time, Color, Message);
	}
}

void AIntroSceneDirector::TickWalking(float DeltaSeconds)
{
	UCharacterMovementComponent* MoveComp = Player->GetCharacterMovement();
	if (MoveComp)
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;   // other code may touch it: keep it at the scene's pace
	}

	if (WalkTime > WalkTimeout)
	{
		const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		Player->SetActorLocation(Waypoints.Last() + FVector(0, 0, HalfHeight + 5.0f), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Warning, TEXT("IntroScene: walk timed out, hero placed on the last waypoint"));
		ShowDebug(501, 6.0f, FColor::Yellow, FString::Printf(TEXT("IntroScene: walk TIMED OUT at point %d/%d - hero teleported to the last point"),
			RouteIndex, Route.Num() - 1));
		OnHeroArrived();
		return;
	}

	FVector ToTarget = Route[RouteIndex] - Player->GetActorLocation();
	ToTarget.Z = 0.0f;
	const bool bIsLast = (RouteIndex == Route.Num() - 1);
	// Corner points from the NavMesh sit right next to obstacles: pass them close, do not cut the corner.
	const float ArriveRadius = bIsLast ? 40.0f : 50.0f;

	if (ToTarget.Size() <= ArriveRadius)
	{
		if (bIsLast)
		{
			OnHeroArrived();
			return;
		}
		++RouteIndex;
		return;
	}

	WalkProgress = FMath::Max(WalkProgress, ComputeWalkProgress());

	// bForce = true: move input is ignored on the controller, but this is the scene driving the pawn, not the player.
	Player->AddMovementInput(ToTarget.GetSafeNormal(), 1.0f, true);
}

void AIntroSceneDirector::OnHeroArrived()
{
	bHeroArrived = true;
	WalkProgress = 1.0f;   // the camera finishes its approach (glides the last bit)
	UE_LOG(LogTemp, Warning, TEXT("IntroScene: hero arrived after %.1fs"), WalkTime);
}

void AIntroSceneDirector::BeginHolding()
{
	Phase = EIntroPhase::Holding;
	PhaseTime = 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("IntroScene: holding for %.1fs"), HoldDuration);
}

FVector AIntroSceneDirector::ComputeOrbitEndPos() const
{
	// Where the player's own camera will be: boom origin pulled back along the control rotation.
	if (USpringArmComponent* Boom = Player->GetCameraBoomComponent())
	{
		return Boom->GetComponentLocation() + GameplayRotation.RotateVector(FVector(-Boom->TargetArmLength, 0.0f, 0.0f) + Boom->SocketOffset);
	}
	return Player->GetActorLocation() + GameplayRotation.RotateVector(FVector(-400.0f, 0.0f, 0.0f));
}

static float IntroYawOf(const FVector& V)
{
	return FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X));
}

void AIntroSceneDirector::BeginOrbit()
{
	Phase = EIntroPhase::Orbiting;
	PhaseTime = 0.0f;

	// Point the controller (and with it the spring arm) the way the hero faces, so the gameplay camera is
	// already where the orbit will end. The hero himself does not turn (bUseControllerRotationYaw is off).
	GameplayRotation = FRotator(GameplayCameraPitch, Player->GetActorRotation().Yaw, 0.0f);
	PC->SetControlRotation(GameplayRotation);

	// Sweep around the hero's RIGHT side (decreasing angle): front -> right -> behind. Decided once, from where
	// the fly-in ends to where the player's camera will be.
	const float FromAngle = IntroYawOf(ApproachEnd - OrbitPivot);
	const float ToAngle = IntroYawOf(ComputeOrbitEndPos() - OrbitPivot);
	bOrbitFullTurn = FMath::Fmod(FromAngle - ToAngle + 720.0f, 360.0f) < 30.0f;   // camera already behind: still make a visible turn

	UE_LOG(LogTemp, Warning, TEXT("IntroScene: orbit starts (%s), %.1fs"),
		bHeroArrived ? TEXT("hero has arrived") : TEXT("hero still walking"), OrbitDuration);
}

void AIntroSceneDirector::TickOrbit(float DeltaSeconds)
{
	const float Raw = FMath::Clamp(PhaseTime / FMath::Max(OrbitDuration, 0.1f), 0.0f, 1.0f);
	const float Alpha = FMath::SmoothStep(0.0f, 1.0f, Raw);

	// Until he has stopped he may still turn a little: keep the target behind his back.
	GameplayRotation.Yaw = Player->GetActorRotation().Yaw;
	PC->SetControlRotation(GameplayRotation);

	// The fly-in keeps going underneath (it is still closing in if the hero has not arrived yet) and is turned
	// around the pivot by the orbit: the camera spirals in instead of stopping and then starting to turn.
	const FVector Off = ComputeApproachPos(DeltaSeconds, false) - OrbitPivot;
	const FVector EndOff = ComputeOrbitEndPos() - OrbitPivot;

	const float FromAngle = IntroYawOf(Off);
	float Sweep = FMath::Fmod(FromAngle - IntroYawOf(EndOff) + 720.0f, 360.0f);
	if (bOrbitFullTurn)
	{
		Sweep += 360.0f;
	}
	const float Rad = FMath::DegreesToRadians(FromAngle - Sweep * Alpha);
	const float Radius = FMath::Lerp(FVector(Off.X, Off.Y, 0.0f).Size(), FVector(EndOff.X, EndOff.Y, 0.0f).Size(), Alpha);
	const float Z = FMath::Lerp(Off.Z, EndOff.Z, Alpha);
	const FVector Pos = OrbitPivot + FVector(Radius * FMath::Cos(Rad), Radius * FMath::Sin(Rad), Z);

	// Look at the hero's face, and over the last part of the sweep turn towards the direction the player's
	// camera will look in, so the hand-over blend has almost nothing left to correct.
	const FVector FaceTarget = Player->GetActorLocation() + FVector(0, 0, LookAtHeight);
	const FVector GameTarget = Pos + GameplayRotation.Vector() * 1000.0f;
	const float TurnAlpha = FMath::SmoothStep(0.0f, 1.0f, (Alpha - 0.6f) / 0.4f);
	const FVector LookTarget = FMath::Lerp(FaceTarget, GameTarget, TurnAlpha);
	CameraLookRotation = FMath::RInterpTo(CameraLookRotation, UKismetMathLibrary::FindLookAtRotation(Pos, LookTarget), DeltaSeconds, 10.0f);

	SceneCamera->SetActorLocationAndRotation(Pos, CameraLookRotation);

	// Hand over only once the hero is really standing (normally long before the orbit ends).
	if (Raw >= 1.0f && bHeroArrived)
	{
		BeginHandover();
	}
}

void AIntroSceneDirector::BeginHandover()
{
	Phase = EIntroPhase::Handover;
	PhaseTime = 0.0f;

	// The view blends from the scene camera to the player's own camera; the player may move right away.
	PC->SetViewTargetWithBlend(Player, HandoverBlendTime, VTBlend_Cubic, 0.0f, false);
	FinishScene();
}

void AIntroSceneDirector::FinishScene()
{
	if (UCharacterMovementComponent* MoveComp = Player->GetCharacterMovement())
	{
		if (SavedMaxWalkSpeed > 0.0f)
		{
			MoveComp->MaxWalkSpeed = SavedMaxWalkSpeed;
		}
	}
	Player->EnableInput(PC);
	PC->SetIgnoreMoveInput(false);
	PC->SetIgnoreLookInput(false);
	Player->SetAimSuppressed(false);

	Phase = EIntroPhase::Done;
	SetActorTickEnabled(false);
	GetWorldTimerManager().SetTimer(CameraCleanupHandle, this, &AIntroSceneDirector::DestroySceneCamera, HandoverBlendTime + 0.3f, false);

	UE_LOG(LogTemp, Warning, TEXT("IntroScene: finished, the player has control"));
	ShowDebug(500, 3.0f, FColor::Green, TEXT("IntroScene: finished - control returned to the player"));
	OnIntroFinished.Broadcast();
}

void AIntroSceneDirector::DestroySceneCamera()
{
	if (IsValid(SceneCamera))
	{
		SceneCamera->Destroy();
		SceneCamera = nullptr;
	}
}
