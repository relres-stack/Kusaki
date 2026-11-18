// Fill out your copyright notice in the Description page of Project Settings.

#include "IntroSceneDirector.h"

#include "BaseCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
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

	// Camera path: starts far in front of the final standing point and high above it, ends in front of that point.
	// See UpdateApproachCamera(). Both camera moves are tied to walk distance: the orbit takes the last
	// OrbitDuration seconds of the walk, the fly-in ends ApproachOrbitOverlap seconds after the orbit has started.
	const FVector Final = Waypoints[Last];
	ApproachEnd = Final + HeadingDir * FrontCameraDistance + FVector(0, 0, FrontCameraHeight);
	const FVector ApproachDir = HeadingDir.RotateAngleAxis(-ApproachYawOffset, FVector::UpVector);
	ApproachStart = Final + ApproachDir * ApproachStartDistance + FVector(0, 0, ApproachStartHeight);
	const float Total = RouteDist.Last();
	OrbitStartRemaining = FMath::Clamp(WalkSpeed * OrbitDuration, 1.0f, Total * 0.8f);
	ApproachDistance = FMath::Clamp(Total - OrbitStartRemaining + WalkSpeed * ApproachOrbitOverlap, Total * 0.2f, Total);
	OrbitRaw = 0.0f;
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

	if (bFadeIn && PC->PlayerCameraManager)
	{
		// Black from the very first frame; the HUD (Slate/UMG) is not covered by the camera fade and stays visible.
		PC->PlayerCameraManager->SetManualCameraFade(1.0f, FLinearColor::Black, false);
		Phase = EIntroPhase::WarmingUp;
		PhaseTime = 0.0f;
		WarmupFrameTimes.Reset();
		SetActorTickEnabled(true);
		return;
	}
	BeginWalking();
}

void AIntroSceneDirector::TickWarmup(float DeltaSeconds)
{
	// Steady = the last WarmupStableFrames frames are within WarmupMaxFrameRatio of each other (not "fast"),
	// so a slow-but-even PC starts as quickly as a fast one. WarmupMaxTime is the hard limit.
	WarmupFrameTimes.Add(DeltaSeconds);
	const int32 Window = FMath::Max(WarmupStableFrames, 2);
	while (WarmupFrameTimes.Num() > Window)
	{
		WarmupFrameTimes.RemoveAt(0);
	}

	bool bSteady = false;
	float MinDt = 0.0f, MaxDt = 0.0f;
	if (WarmupFrameTimes.Num() == Window)
	{
		MinDt = FMath::Min(WarmupFrameTimes);
		MaxDt = FMath::Max(WarmupFrameTimes);
		bSteady = MinDt > KINDA_SMALL_NUMBER && MaxDt <= MinDt * WarmupMaxFrameRatio;
	}
	const bool bTimedOut = PhaseTime >= WarmupMaxTime;
	if (!bSteady && !bTimedOut)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("IntroScene: warm-up done after %.2fs (%s, last frames %.0f-%.0f ms), fading in over %.1fs"),
		PhaseTime, bSteady ? TEXT("frames steady") : TEXT("time limit reached"), MinDt * 1000.0f, MaxDt * 1000.0f, FadeInDuration);
	FadeElapsed = 0.0f;   // TickFadeIn() drives it from the next frame
	BeginWalking();       // he starts walking as the picture appears
}

void AIntroSceneDirector::TickFadeIn(float DeltaSeconds)
{
	APlayerCameraManager* CamManager = PC->PlayerCameraManager;
	if (!CamManager)
	{
		FadeElapsed = -1.0f;
		return;
	}

	// Driven by hand instead of StartCameraFade(): the engine blends the fade colour in LINEAR colour (before the
	// display gamma), so a linear fade amount makes the picture jump to ~1/3 brightness within the first tenth of
	// the fade. Here the visible brightness follows an eased curve: Visible = SmoothStep(t)^2.2 in linear terms,
	// which the display gamma turns back into an even rise. A long frame advances the fade by at most 50 ms.
	FadeElapsed += FMath::Min(DeltaSeconds, 0.05f);
	const float T = FMath::Clamp(FadeElapsed / FMath::Max(FadeInDuration, 0.01f), 0.0f, 1.0f);
	const float VisibleLinear = FMath::Pow(FMath::SmoothStep(0.0f, 1.0f, T), 2.2f);
	if (T >= 1.0f)
	{
		CamManager->StopCameraFade();   // overlay off entirely (the engine applies it only while fading is enabled)
		FadeElapsed = -1.0f;
		return;
	}
	CamManager->SetManualCameraFade(1.0f - VisibleLinear, FLinearColor::Black, false);
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
					// Corner points right next to a waypoint are NavMesh projection noise, not a real detour: skip them.
					const FVector& C = Path->PathPoints[k];
					if (FVector::Dist2D(C, Route.Last()) > 30.0f && FVector::Dist2D(C, Waypoints[i]) > 30.0f)
					{
						Route.Add(C);
						UE_LOG(LogTemp, Warning, TEXT("IntroScene: NavMesh corner between waypoints %d and %d at %s"), i - 1, i, *C.ToString());
					}
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

FVector AIntroSceneDirector::RoutePointAt(float Distance) const
{
	if (Distance <= 0.0f)
	{
		return Route[0];
	}
	for (int32 k = 0; k < Route.Num() - 1; ++k)
	{
		if (Distance <= RouteDist[k + 1])
		{
			const float SegLen = RouteDist[k + 1] - RouteDist[k];
			return FMath::Lerp(Route[k], Route[k + 1], SegLen > KINDA_SMALL_NUMBER ? (Distance - RouteDist[k]) / SegLen : 1.0f);
		}
	}
	return Route.Last();
}

void AIntroSceneDirector::UpdateRouteProjection()
{
	// Project the hero onto the route (2D), moving forward only: a segment is left once he is past its end.
	const FVector Hero = Player->GetActorLocation();
	float T = 0.0f;
	while (true)
	{
		const FVector A = Route[RouteSegment];
		const FVector AB(Route[RouteSegment + 1].X - A.X, Route[RouteSegment + 1].Y - A.Y, 0.0f);
		const float Len2 = AB.SizeSquared();
		T = Len2 > 1.0f ? FVector::DotProduct(FVector(Hero.X - A.X, Hero.Y - A.Y, 0.0f), AB) / Len2 : 1.0f;
		if (T < 1.0f || RouteSegment >= Route.Num() - 2)
		{
			break;
		}
		++RouteSegment;
	}
	const float SegLen = RouteDist[RouteSegment + 1] - RouteDist[RouteSegment];
	RouteAlong = FMath::Max(RouteAlong, RouteDist[RouteSegment] + FMath::Clamp(T, 0.0f, 1.0f) * SegLen);
}

float AIntroSceneDirector::RemainingWalkDistance() const
{
	return bHeroArrived ? 0.0f : FMath::Max((RouteDist.Num() > 0 ? RouteDist.Last() : 0.0f) - RouteAlong, 0.0f);
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
	RouteSegment = 0;
	RouteAlong = 0.0f;
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

	if (FadeElapsed >= 0.0f)
	{
		TickFadeIn(DeltaSeconds);
	}

	// The walk runs on its own: the orbit may already have started while the hero takes his last steps.
	const bool bWalkPhase = (Phase == EIntroPhase::Walking || Phase == EIntroPhase::Orbiting);
	if (bWalkPhase && !bHeroArrived)
	{
		WalkTime += DeltaSeconds;
		TickWalking(DeltaSeconds);
	}

	// Long frames show up as camera/hero jerks: log them, so a stutter can be told apart from a code problem.
	if (bWalkPhase && DeltaSeconds > 0.06f)
	{
		UE_LOG(LogTemp, Warning, TEXT("IntroScene: slow frame %.0f ms (%s, t=%.2fs)"),
			DeltaSeconds * 1000.0f, *UEnum::GetValueAsString(Phase), PhaseTime);
	}

	switch (Phase)
	{
	case EIntroPhase::WarmingUp:
		UpdateCameraLook(DeltaSeconds, true);
		TickWarmup(DeltaSeconds);
		break;
	case EIntroPhase::Walking:
		UpdateApproachCamera(DeltaSeconds, false);
		UpdateCameraLook(DeltaSeconds, false);
		if (bHeroArrived || RemainingWalkDistance() <= OrbitStartRemaining)
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
		if (!bHeroArrived && RouteDist.Num() > 0)
		{
			const float Speed = Player->GetVelocity().Size2D();
			Extra = FString::Printf(TEXT("  segment %d/%d, %.0f cm to go, speed %.0f, walk %.0f%% camera %.0f%%, timeout in %.0fs"),
				RouteSegment + 1, Route.Num() - 1, RouteDist.Last() - RouteAlong,
				Speed, WalkProgress * 100.0f, CameraProgress * 100.0f, FMath::Max(WalkTimeout - WalkTime, 0.0f));
			if (WalkTime > 0.5f && Speed < 10.0f)
			{
				Color = FColor::Yellow;   // should be walking but is not moving: stuck on something
			}
		}
		if (Phase == EIntroPhase::Orbiting)
		{
			Extra += FString::Printf(TEXT("  orbit %.0f%%%s"), OrbitRaw * 100.0f,
				bHeroArrived ? TEXT(" (hero stopped)") : TEXT(""));
		}
		if (Phase == EIntroPhase::WarmingUp)
		{
			Extra = FString::Printf(TEXT("  black screen, waiting for %d steady frames (max %.1fs)"), WarmupStableFrames, WarmupMaxTime);
		}
		if (DeltaSeconds > 0.06f)
		{
			Color = FColor::Orange;   // slow frame right now
		}
		ShowDebug(500, 0.0f, Color, FString::Printf(TEXT("IntroScene: %s  t=%.1fs  frame %.0f ms%s"),
			*UEnum::GetValueAsString(Phase), PhaseTime, DeltaSeconds * 1000.0f, *Extra));
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
		ShowDebug(501, 6.0f, FColor::Yellow, FString::Printf(TEXT("IntroScene: walk TIMED OUT on segment %d/%d - hero teleported to the last point"),
			RouteSegment + 1, Route.Num() - 1));
		OnHeroArrived();
		return;
	}

	const FVector HeroLoc = Player->GetActorLocation();
	if (FVector::Dist2D(HeroLoc, Route.Last()) <= 40.0f)
	{
		OnHeroArrived();
		return;
	}

	UpdateRouteProjection();
	WalkProgress = FMath::Max(WalkProgress, ApproachDistance > KINDA_SMALL_NUMBER ? FMath::Min(RouteAlong / ApproachDistance, 1.0f) : 1.0f);

	// Aim at a point RouteLookAhead further along the route ("carrot"): smooth through NavMesh corners, never back.
	// Near the end the carrot sits on the last point, so he walks straight onto the start spot.
	FVector ToTarget = RoutePointAt(RouteAlong + RouteLookAhead) - HeroLoc;
	ToTarget.Z = 0.0f;
	if (ToTarget.SizeSquared() < 1.0f)
	{
		ToTarget = Route.Last() - HeroLoc;
		ToTarget.Z = 0.0f;
	}

	// Input EVERY frame (a frame without it lets the anim graph drop into its stop state).
	// bForce = true: move input is ignored on the controller, but this is the scene driving the pawn, not the player.
	Player->AddMovementInput(ToTarget.GetSafeNormal(), 1.0f, true);
}

void AIntroSceneDirector::OnHeroArrived()
{
	bHeroArrived = true;
	WalkProgress = 1.0f;   // the camera finishes its approach (glides the last bit)
	UE_LOG(LogTemp, Warning, TEXT("IntroScene: hero arrived after %.1fs (orbit at %.0f%%)"), WalkTime, OrbitRaw * 100.0f);
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
	// the camera is now to where the player's camera will be.
	const FVector Pivot = Player->GetActorLocation();
	const float FromAngle = IntroYawOf(SceneCamera->GetActorLocation() - Pivot);
	const float ToAngle = IntroYawOf(ComputeOrbitEndPos() - Pivot);
	bOrbitFullTurn = FMath::Fmod(FromAngle - ToAngle + 720.0f, 360.0f) < 30.0f;   // camera already behind: still make a visible turn

	UE_LOG(LogTemp, Warning, TEXT("IntroScene: orbit starts, %.0f cm of the walk left (%s)"),
		RemainingWalkDistance(), bHeroArrived ? TEXT("hero already stopped") : TEXT("ends when he stops"));
}

void AIntroSceneDirector::TickOrbit(float DeltaSeconds)
{
	// Orbit progress follows the walk: 0 when OrbitStartRemaining of the route is left, 1 when he stops.
	// Smoothed so it never jumps; after he has stopped (normally only a hair is left; after a timeout teleport
	// the whole sweep) it finishes at a steady pace, at most ~2 s for the full turn.
	if (!bHeroArrived)
	{
		const float Target = FMath::Clamp(1.0f - RemainingWalkDistance() / FMath::Max(OrbitStartRemaining, 1.0f), 0.0f, 1.0f);
		OrbitRaw = FMath::FInterpTo(OrbitRaw, Target, DeltaSeconds, 10.0f);
	}
	else
	{
		OrbitRaw = FMath::FInterpConstantTo(OrbitRaw, 1.0f, DeltaSeconds, 0.5f);
	}
	const float Alpha = FMath::SmoothStep(0.0f, 1.0f, OrbitRaw);

	// Until he has stopped he may still turn a little: keep the target behind his back.
	GameplayRotation.Yaw = Player->GetActorRotation().Yaw;
	PC->SetControlRotation(GameplayRotation);

	// Polar coordinates around the hero himself (he is still walking). The fly-in keeps going underneath and is
	// turned around him by the orbit: the camera spirals in instead of stopping and then starting to turn, and
	// ends exactly at the player's camera position relative to him.
	const FVector Pivot = Player->GetActorLocation();
	const FVector Off = ComputeApproachPos(DeltaSeconds, false) - Pivot;
	const FVector EndOff = ComputeOrbitEndPos() - Pivot;

	const float FromAngle = IntroYawOf(Off);
	float Sweep = FMath::Fmod(FromAngle - IntroYawOf(EndOff) + 720.0f, 360.0f);
	if (bOrbitFullTurn)
	{
		Sweep += 360.0f;
	}
	const float Rad = FMath::DegreesToRadians(FromAngle - Sweep * Alpha);
	const float Radius = FMath::Lerp(FVector(Off.X, Off.Y, 0.0f).Size(), FVector(EndOff.X, EndOff.Y, 0.0f).Size(), Alpha);
	const float Z = FMath::Lerp(Off.Z, EndOff.Z, Alpha);
	const FVector Pos = Pivot + FVector(Radius * FMath::Cos(Rad), Radius * FMath::Sin(Rad), Z);

	// Look at the hero's face, and over the last part of the sweep turn towards the direction the player's
	// camera will look in, so the hand-over blend has almost nothing left to correct.
	const FVector FaceTarget = Pivot + FVector(0, 0, LookAtHeight);
	const FVector GameTarget = Pos + GameplayRotation.Vector() * 1000.0f;
	const float TurnAlpha = FMath::SmoothStep(0.0f, 1.0f, (Alpha - 0.6f) / 0.4f);
	const FVector LookTarget = FMath::Lerp(FaceTarget, GameTarget, TurnAlpha);
	CameraLookRotation = FMath::RInterpTo(CameraLookRotation, UKismetMathLibrary::FindLookAtRotation(Pos, LookTarget), DeltaSeconds, 10.0f);

	SceneCamera->SetActorLocationAndRotation(Pos, CameraLookRotation);

	// Hand over the moment he is standing and the camera is behind him (normally the same moment).
	if (bHeroArrived && OrbitRaw >= 0.995f)
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
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StopCameraFade();   // safety: never leave the screen dark (the fade-in is long over by now)
	}
	FadeElapsed = -1.0f;

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
