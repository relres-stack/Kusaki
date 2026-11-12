// Fill out your copyright notice in the Description page of Project Settings.

#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "BearTrap.h"
#include "Components/HeartbeatAudioComponent.h"
#include "Components/HidingComponent.h"
#include "Components/PostProcessComponent.h"
#include "CrosshairWidget.h"
#include "InventoryComponent.h"
#include "ProbeComponent.h"
#include "PlayerHUDWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "HolyItem.h"
#include "CollisionQueryParams.h"
#include "TimerManager.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "InputCoreTypes.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ConstructorHelpers.h"

ABaseCharacter::ABaseCharacter()
	: bIsEyesClosed(false)
{
	PrimaryActorTick.bCanEverTick = true;

	// Every hero gets a quick-slot inventory regardless of subclass — see UInventoryComponent's own
	// header comment.
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));

	// Universal "Прощупывание" blind-navigation ability — see UProbeComponent's own header comment. Same
	// "every hero gets it regardless of subclass" precedent as InventoryComponent above.
	ProbeComponent = CreateDefaultSubobject<UProbeComponent>(TEXT("ProbeComponent"));

	// Universal "Укрытия" ability — see UHidingComponent's own header comment. Same "every hero gets it
	// regardless of subclass" precedent as InventoryComponent/ProbeComponent above.
	HidingComponent = CreateDefaultSubobject<UHidingComponent>(TEXT("HidingComponent"));

	// Universal heartbeat/breathing — see UHeartbeatAudioComponent's own header comment. Same "every
	// hero gets it regardless of subclass" precedent as the components above.
	HeartbeatAudioComponent = CreateDefaultSubobject<UHeartbeatAudioComponent>(TEXT("HeartbeatAudioComponent"));

	// Mesh alignment: the standard Mannequin skeleton's root sits at the feet and faces +X, so it needs
	// pulling down to the capsule's base (HalfHeight is 88 by default) and turning -90 degrees on Yaw to
	// face the capsule's own forward (+X) direction. Done here, once, so every Blueprint subclass's mesh
	// lines up inside its capsule automatically regardless of which skeletal mesh asset it assigns.
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	// Standard third-person rotation setup: the capsule/mesh turns to face movement direction, NOT the
	// camera — Tick()'s bIsAiming block above flips bUseControllerRotationYaw/bOrientRotationToMovement
	// only while actively aiming.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
		MoveComp->JumpZVelocity = 700.0f;
		MoveComp->AirControl = 0.35f;

		// Default movement speeds: 300 walking, 600 sprinting (see StartSprinting()/StopSprinting()), 150
		// crouched. Set here (not just left at the engine's own 600/300 defaults) so every Blueprint hero
		// starts with the same baseline unless a subclass overrides it in Class Defaults.
		MoveComp->MaxWalkSpeed = DefaultWalkSpeed;
		MoveComp->MaxWalkSpeedCrouched = 150.0f;

		// Required for Crouch()/UnCrouch() (see ToggleCrouch(), bound to 'C') to do anything at all —
		// ACharacter::CanCrouch() checks this and silently no-ops the whole call if it's false, which is
		// UCharacterMovementComponent's own default. Moved here from APriestCharacter's own constructor
		// (which used to set this just for its own auto-crouch-while-holding-a-Candle behavior) now that
		// every hero can crouch via 'C', not just the Priest.
		MoveComp->NavAgentProps.bCanCrouch = true;
	}

	// Camera boom: pulls in toward the capsule on collision (bDoCollisionTest, on by default) so the
	// camera never renders from inside a wall/the mesh capsule, and rotates with the controller so
	// looking around doesn't require a Blueprint-side "use pawn control rotation" node.
	CameraBoomComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoomComponent"));
	CameraBoomComponent->SetupAttachment(RootComponent);
	CameraBoomComponent->TargetArmLength = 400.0f;
	CameraBoomComponent->bUsePawnControlRotation = true;

	// The actual view camera — attached to the boom's end socket, not directly to the capsule/mesh, so
	// it inherits the boom's collision-tested position rather than the pawn's raw rotation.
	FollowCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCameraComponent"));
	FollowCameraComponent->SetupAttachment(CameraBoomComponent, USpringArmComponent::SocketName);
	FollowCameraComponent->bUsePawnControlRotation = false;

	// Default Enhanced Input assets — loaded via ConstructorHelpers so every Blueprint subclass
	// (BP_Redneck/BP_Priest/BP_Rider/BP_Sibling) inherits a working control scheme with zero editor setup;
	// any of these can still be swapped per-subclass in that Blueprint's Class Defaults if a hero ever
	// needs different bindings. A failed find (asset moved/renamed) just leaves the property unset —
	// ApplyDefaultInputMappingContext()/SetupPlayerInputComponent() both null-check before using it.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultContextFinder(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (DefaultContextFinder.Succeeded())
	{
		DefaultMappingContext = DefaultContextFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookContextFinder(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	if (MouseLookContextFinder.Succeeded())
	{
		MouseLookMappingContext = MouseLookContextFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	if (MoveActionFinder.Succeeded())
	{
		MoveAction = MoveActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> LookActionFinder(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	if (LookActionFinder.Succeeded())
	{
		LookAction = LookActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookActionFinder(TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (MouseLookActionFinder.Succeeded())
	{
		MouseLookAction = MouseLookActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> JumpActionFinder(TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	if (JumpActionFinder.Succeeded())
	{
		JumpAction = JumpActionFinder.Object;
	}

	// See KnockdownFallAnimation's own header comment — reuses one of the Mannequin's existing "Death" anim
	// sequences purely for its fallen POSE during the Panic State's knockdown, unrelated to bIsDead/
	// HandleDeath(). GetUpAnimation is deliberately left unset here — no dedicated get-up clip exists
	// anywhere in this project's Content yet, so RecoverFromKnockdown() falls back to an instant snap
	// back to standing until a designer assigns one.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> KnockdownAnimFinder(TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"));
	if (KnockdownAnimFinder.Succeeded())
	{
		KnockdownFallAnimation = KnockdownAnimFinder.Object;
	}
}

void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	GetWorldTimerManager().SetTimer(InteractScanTimerHandle, this, &ABaseCharacter::ScanForInteractable, 0.1f, true);

	// Usually a no-op here — see InitializeLocalPlayerUI()'s own comment on why PossessedBy() (below) is
	// the call site that actually matters for the GameMode's initial default pawn. Still called here too,
	// in case this pawn was somehow already possessed before BeginPlay() ran.
	InitializeLocalPlayerUI();

	// CameraBoomComponent/FollowCameraComponent are native subobjects created in the constructor (always
	// valid) — their CURRENT TargetArmLength/SocketOffset/FOV, whatever the constructor (or a Blueprint
	// subclass's Class Defaults override) set them to, become the "return to default" values Tick()
	// interpolates back to when not aiming.
	if (IsValid(CameraBoomComponent))
	{
		DefaultCameraArmLength = CameraBoomComponent->TargetArmLength;
		DefaultCameraSocketOffset = CameraBoomComponent->SocketOffset;
	}

	if (IsValid(FollowCameraComponent))
	{
		DefaultCameraFOV = FollowCameraComponent->FieldOfView;
	}

	// Same "capture whatever the constructor/Blueprint Class Defaults actually set, don't hardcode it"
	// reasoning as DefaultCameraArmLength above — DefaultWalkSpeed becomes whatever
	// GetCharacterMovement()'s own MaxWalkSpeed already is (300, set in the constructor, unless a
	// Blueprint subclass overrides it), so StopSprinting() always has an accurate "un-sprint" speed to
	// restore.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		DefaultWalkSpeed = MoveComp->MaxWalkSpeed;
	}

	// Eyes-closed look: everything dissolves into darkness and blur, only bright light sources stay as soft
	// glowing blobs. Done with exposure (not a flat colour tint — a tint would kill the lights too) plus a
	// strong whole-screen blur. The component stays enabled all the time and Tick() only changes its values:
	// exposure adapts over time, and a component that is switched off loses its own fast adaptation speed.
	//
	// History (so nobody re-adds it): an earlier version fought a bright scene with manual exposure
	// (AEM_Manual, Bias -8) and per-frame hacks; that was caused by the "developer scene lighting" left on in
	// the editor, not by the code. The current values are plain Post Process settings, tunable in the Blueprint
	// (category "Eyes Closed"). If the effect is too weak, check the scene lighting first.
	EyesClosedPostProcessComp = NewObject<UPostProcessComponent>(this, TEXT("EyesClosedPostProcess"));
	EyesClosedPostProcessComp->SetupAttachment(GetRootComponent());
	EyesClosedPostProcessComp->RegisterComponent();
	EyesClosedPostProcessComp->bUnbound = true;
	EyesClosedPostProcessComp->bEnabled = true;

	// Fast adaptation both ways, so closing/opening eyes (and squinting) changes the picture almost instantly.
	EyesClosedPostProcessComp->Settings.bOverride_AutoExposureSpeedUp = true;
	EyesClosedPostProcessComp->Settings.AutoExposureSpeedUp = EyesClosedAutoExposureSpeed;
	EyesClosedPostProcessComp->Settings.bOverride_AutoExposureSpeedDown = true;
	EyesClosedPostProcessComp->Settings.AutoExposureSpeedDown = EyesClosedAutoExposureSpeed;

	// Values below are the "eyes open" starting point (no effect); Tick() switches them while blind.
	EyesClosedPostProcessComp->Settings.bOverride_AutoExposureBias = true;
	EyesClosedPostProcessComp->Settings.AutoExposureBias = 0.0f;
	EyesClosedPostProcessComp->Settings.bOverride_VignetteIntensity = true;
	EyesClosedPostProcessComp->Settings.VignetteIntensity = 0.0f;
}

void ABaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// The call site that actually matters — see InitializeLocalPlayerUI()'s own comment.
	InitializeLocalPlayerUI();
}

void ABaseCharacter::InitializeLocalPlayerUI()
{
	if (bLocalPlayerUIInitialized)
	{
		return;
	}

	// Only for the locally-controlled player (a remote/AI-possessed instance of this class, if this
	// project ever has one, has no local viewport to draw a HUD into, and CreateWidget()/AddToViewport()
	// would be meaningless there anyway).
	APlayerController* OwningPC = Cast<APlayerController>(GetController());
	if (!IsValid(OwningPC) || !OwningPC->IsLocalController())
	{
		return;
	}

	bLocalPlayerUIInitialized = true;

	// See ApplyDefaultInputMappingContext()'s own comment — this is what makes WASD/mouse-look/Jump work
	// immediately on possession without any Blueprint "Add Input Mapping Context" node.
	ApplyDefaultInputMappingContext();

	// Screen-space crosshair widget. Defaults to the fully native UCrosshairWidget class itself when
	// CrosshairWidgetClass is left unset, so this works with zero required Blueprint/editor setup.
	TSubclassOf<UCrosshairWidget> WidgetClass = CrosshairWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = UCrosshairWidget::StaticClass();
	}
	CrosshairWidgetInstance = CreateWidget<UCrosshairWidget>(OwningPC, WidgetClass);
	if (IsValid(CrosshairWidgetInstance))
	{
		// Z-order 1: explicitly above the HUD widget below (Z-order 0), so the crosshair/interaction
		// prompt always draws on top of the quick-slot bar/health bar rather than depending on
		// insertion-order tie-breaking between two same-Z widgets.
		CrosshairWidgetInstance->AddToViewport(1);
		CrosshairWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Force InventoryComponent's quick-slots to be populated NOW, in case InitializeLocalPlayerUI() fired
	// via PossessedBy() (the call site that matters for the GameMode's initial pawn) BEFORE
	// InventoryComponent::BeginPlay() — the only other place these get populated — has had a chance to
	// run. Without this, the HUD widget's one-time "pull current state on construct" below would read
	// QuickSlots still at their default-constructed Empty state (e.g. Redneck's Moonshine slot looking
	// empty on start) instead of the real light-source/consumable data, and never self-correct unless
	// something ELSE happened to change afterward and re-broadcast.
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->RefreshLightSourceSlot();
		InventoryComponent->RefreshConsumableSlot();
	}

	// Horror HUD — health bar, quick-slot bar, mechanic/status readouts. Same locally-controlled-only/
	// class-defaults-to-native-C++ reasoning as the crosshair above. UPlayerHUDWidget::NativeConstruct()
	// (run synchronously inside CreateWidget()) is what binds to OnHealthChanged/InventoryComponent's
	// delegates and pulls their current values immediately, so Health/Quick-Slots render on the very
	// first frame rather than waiting for the next change to broadcast.
	TSubclassOf<UPlayerHUDWidget> HUDClass = PlayerHUDWidgetClass;
	if (!HUDClass)
	{
		HUDClass = UPlayerHUDWidget::StaticClass();
	}
	PlayerHUDWidgetInstance = CreateWidget<UPlayerHUDWidget>(OwningPC, HUDClass);
	if (IsValid(PlayerHUDWidgetInstance))
	{
		// Explicitly set BEFORE AddToViewport() — AddToViewport() is what triggers this widget's first
		// NativeConstruct() (via RebuildWidget()/TakeWidget()), and that must see a valid OwnerCharacter
		// already in place rather than trying (and, for this call path, failing) to resolve it itself —
		// see SetOwnerCharacter()'s own comment for why.
		PlayerHUDWidgetInstance->SetOwnerCharacter(this);
		PlayerHUDWidgetInstance->AddToViewport(0);
	}
}

void ABaseCharacter::ApplyDefaultInputMappingContext()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!IsValid(LocalPlayer))
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	// Both contexts layered together at the same priority — see MouseLookMappingContext's own header
	// comment on why mouse-look needs a second context on top of DefaultMappingContext. ClearAllMappings()
	// first so re-possession (e.g. this pawn is re-possessed after being released) doesn't stack duplicate
	// entries.
	Subsystem->ClearAllMappings();
	if (DefaultMappingContext)
	{
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}
	if (MouseLookMappingContext)
	{
		Subsystem->AddMappingContext(MouseLookMappingContext, 0);
	}
}

void ABaseCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D MovementVector = Value.Get<FVector2D>();

	// See this function's own header comment for the full reasoning on both detours below.
	if (bIsPanicking)
	{
		// Per-direction edge detection (not a single combined "any input" flag) — see
		// bWasForwardHeldLastFrame's own comment for why: a fast W->A->S->D switch never actually passes
		// MovementVector back through exactly zero between presses, so a single flag would miss all but the
		// first press. bNewPress is an OR, not a sum — at most one PanicMeter jump per call even if several
		// directions newly become held on the same frame.
		const bool bForwardHeld = MovementVector.Y > KINDA_SMALL_NUMBER;
		const bool bBackHeld = MovementVector.Y < -KINDA_SMALL_NUMBER;
		const bool bRightHeld = MovementVector.X > KINDA_SMALL_NUMBER;
		const bool bLeftHeld = MovementVector.X < -KINDA_SMALL_NUMBER;
		const bool bNewPress = (bForwardHeld && !bWasForwardHeldLastFrame)
			|| (bBackHeld && !bWasBackHeldLastFrame)
			|| (bRightHeld && !bWasRightHeldLastFrame)
			|| (bLeftHeld && !bWasLeftHeldLastFrame);

		if (bNewPress)
		{
			PanicMeter = FMath::Min(PanicMeterMax, PanicMeter + PanicMovementPenalty);

			UE_LOG(LogTemp, Warning, TEXT("%s: struggled against being pinned down — Panic %.0f/%.0f"),
				*GetName(), PanicMeter, PanicMeterMax);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(252, BreathRhythmInterval, FColor::Orange,
					FString::Printf(TEXT("Panic: %.0f/%.0f (struggling — stay still!)"), PanicMeter, PanicMeterMax));
			}

			// Same PanicMeterMax check AdvanceToNextBeat() runs after every breath beat — duplicated here
			// since a movement-triggered spike bypasses that beat cycle entirely.
			if (PanicMeter >= PanicMeterMax)
			{
				ForceEyesOpenFromPanic();
			}
		}

		bWasForwardHeldLastFrame = bForwardHeld;
		bWasBackHeldLastFrame = bBackHeld;
		bWasRightHeldLastFrame = bRightHeld;
		bWasLeftHeldLastFrame = bLeftHeld;
		return;
	}
	bWasForwardHeldLastFrame = false;
	bWasBackHeldLastFrame = false;
	bWasRightHeldLastFrame = false;
	bWasLeftHeldLastFrame = false;

	if (bIsAwaitingGetUp)
	{
		const bool bForwardInput = MovementVector.Y > 0.0f;
		if (bForwardInput && !bWasGetUpInputActiveLastFrame)
		{
			bIsAwaitingGetUp = false;
			RecoverFromKnockdown();
		}
		bWasGetUpInputActiveLastFrame = bForwardInput;
		return;
	}
	bWasGetUpInputActiveLastFrame = false;

	// Control-rotation-relative forward/right, each issued as its own AddMovementInput() call with a pure
	// direction vector — the exact convention AddMovementInput()'s own override already assumes when
	// deciding whether a call is the forward or right axis (see its comment), so movement keeps working
	// correctly while IsMounted() redirects it to the mounted monster.
	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void ABaseCharacter::Look(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// HandleDeath() parks the camera boom at a fixed world transform and detaches it for spectating the
	// ragdoll — none of the per-frame aim/mount camera-boom interpolation below should run afterward, or
	// it would fight (and override) that fixed framing every tick.
	if (bIsDead)
	{
		return;
	}

	// GUARANTEED FALLBACK: poll the raw RightMouseButton key state directly every frame instead of
	// relying solely on the "Aim" action-mapping event bindings in SetupPlayerInputComponent(). The
	// Output Log confirmed StartAiming()/StopAiming() were never firing at all (no "AIM RETICLE
	// DRAWING" line ever appeared while RMB was held) — something in the named-action/Enhanced Input
	// path wasn't reaching this class. IsInputKeyDown() bypasses that entire system and reads the
	// physical key directly, so it can't fail the same way. This is now authoritative: it overwrites
	// bIsAiming every frame to match the real key state, so StartAiming()/StopAiming() (still bound
	// below, and still callable directly) can no longer desync from what RMB is actually doing.
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		bIsAiming = PC->IsInputKeyDown(EKeys::RightMouseButton);

		// Panic overrides the raw RMB read — same "authoritative every tick" enforcement as the rest of
		// this block, just gated on bIsPanicking instead of on having a valid PC at all. Without this, a
		// panicking (knocked-down, mid-breathing-minigame) character could still zoom the camera by
		// holding RMB, and on Redneck specifically ARedneckCharacter::TryHijackAttackPress() gates Rage
		// Dash on nothing but !bIsAiming — reading bIsAiming here as forced-false is enough to block both,
		// since neither place has its own separate panic check.
		if (bIsPanicking)
		{
			bIsAiming = false;
		}
	}
	else
	{
		// No longer controlled by a player on this pawn (e.g. ejected via F8 into the spectator/debug
		// camera) — force aiming off instead of freezing bIsAiming at its last value. Left frozen at
		// true, UpdateAimCrosshair() would keep running with no valid controller for TraceAimTarget() to
		// get a camera from, hitting its "no CamManager" fallback (OutTargetPoint = GetActorLocation())
		// — drawing a reticle glued to this actor's own root, which looks exactly like a bug from an
		// outside/spectator view even though it's really just this pawn no longer being the one aiming.
		bIsAiming = false;
	}

	// MANDATORY per-project convention going forward: always surface state/toggle changes on screen
	// with a fixed message key, not just in the log, so they're visible live in PIE without having to
	// open the Output Log.
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(99, 0.0f, bIsAiming ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("bIsAiming: %s"), bIsAiming ? TEXT("TRUE") : TEXT("FALSE")));
	}

	// "Eye Peek Risk" post-process — see EyePeekVignetteIntensity's own comment. Enforced every tick from
	// the same authoritative bIsAiming above (not a separate StartAiming()/StopAiming() path), so it can
	// never desync from the real RMB state the same way the rest of this Tick()'s aim-driven state
	// doesn't. Instant on/off (no interpolation) — a real blink doesn't fade in.
	if (IsValid(FollowCameraComponent))
	{
		if (bIsAiming)
		{
			FollowCameraComponent->PostProcessBlendWeight = 1.0f;
			FollowCameraComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
			FollowCameraComponent->PostProcessSettings.VignetteIntensity = EyePeekVignetteIntensity;
			FollowCameraComponent->PostProcessSettings.bOverride_DepthOfFieldFstop = true;
			FollowCameraComponent->PostProcessSettings.DepthOfFieldFstop = EyePeekDepthOfFieldFstop;
			FollowCameraComponent->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
			FollowCameraComponent->PostProcessSettings.DepthOfFieldFocalDistance = EyePeekDepthOfFieldFocalDistance;
		}
		else
		{
			// "Мгновенно отключать пост-процесс" — dropping the blend weight to 0 is enough on its own
			// (FollowCameraComponent contributes nothing to the final blend), so the override flags above
			// are deliberately left set rather than cleared — they're inert with BlendWeight at 0 and
			// get overwritten with fresh values the next time bIsAiming goes true anyway.
			FollowCameraComponent->PostProcessBlendWeight = 0.0f;
		}
	}

	// Eyes-closed look on/off. Squint (RMB, bIsAiming) lets the player see through; an ability that draws its
	// own screen effect (Ink Vision / Inflamed Vision) suppresses the darkness so it isn't crushed.
	if (IsValid(EyesClosedPostProcessComp))
	{
		const bool bShouldBeBlind = bIsEyesClosed && !bIsAiming && !bIsDarknessSuppressedByAbility;
		FPostProcessSettings& PP = EyesClosedPostProcessComp->Settings;

		PP.AutoExposureBias = bShouldBeBlind ? EyesClosedExposureBias : 0.0f;
		PP.VignetteIntensity = bShouldBeBlind ? EyesClosedVignetteIntensity : 0.0f;

		// Blur everything: depth blur (constant blur radius for everything beyond a very short distance) plus a
		// wide-open aperture focused right at the camera. Overrides are only on while blind, so the normal
		// scene/squint depth of field is untouched the rest of the time.
		PP.bOverride_DepthOfFieldDepthBlurAmount = bShouldBeBlind;
		PP.DepthOfFieldDepthBlurAmount = 0.001f; // km — blur reaches 50% at 1 metre
		PP.bOverride_DepthOfFieldDepthBlurRadius = bShouldBeBlind;
		PP.DepthOfFieldDepthBlurRadius = EyesClosedBlurRadius;
		PP.bOverride_DepthOfFieldFstop = bShouldBeBlind;
		PP.DepthOfFieldFstop = EyesClosedBlurFstop;
		PP.bOverride_DepthOfFieldFocalDistance = bShouldBeBlind;
		PP.DepthOfFieldFocalDistance = 10.0f;
	}

	if (bIsAiming != bWasPeekingLastTick)
	{
		OnEyePeekStateChanged(bIsAiming);
		bWasPeekingLastTick = bIsAiming;
	}

	// Enforced every tick from the authoritative bIsAiming above, not just from StartAiming()/
	// StopAiming() (which set the same two flags for any direct caller, but aren't guaranteed to be
	// what actually flips bIsAiming — see this Tick()'s own IsInputKeyDown() comment on why the named-
	// action path calling them can't be trusted alone). While aiming, controller yaw drives the
	// character's facing directly (Witcher-3-style "body snaps to face the reticle") instead of
	// CharacterMovement's normal orient-to-movement, so the body is always aligned with where the aim
	// trace/throw is actually going; released back to normal free-facing movement otherwise.
	//
	// IsMounted() overrides bIsAiming here specifically: UMountRiderComponent's mount attachment computed
	// and set a fixed relative rotation so the rider sits facing along the monster's own facing. If
	// bUseControllerRotationYaw were left true while mounted+aiming, APlayerController::UpdateRotation()
	// would apply the camera's yaw directly to the rider's actor rotation every frame — since the rider is
	// ATTACHED, that becomes a relative-rotation override fighting the fixed one from mount time, which is
	// exactly what read as "the player mesh rotates inside the monster while looking around/aiming." The
	// rider must NEVER rotate independently while on the monster's shoulders, aiming or not — looking
	// around while mounted should only move the camera (already independent of actor rotation via the
	// SpringArm/FollowCamera), never the rider's own attached transform.
	const bool bCurrentlyMounted = IsMounted();
	ABaseMonster* CurrentMountedMonster = GetMountedMonster();
	bUseControllerRotationYaw = bIsAiming && !bCurrentlyMounted;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = !bIsAiming;
	}

	// Mirror of the block above, but turning the mounted monster's actual actor rotation instead of the
	// rider's own — the rider's world rotation is fully determined by ITS fixed relative rotation (set
	// once at mount time) composed with whatever the monster's world rotation currently is, so rotating
	// the monster here automatically carries the attached rider's facing along with it for free. Only
	// while aiming: bOrientRotationToMovement is temporarily disabled on the monster too (same reason as
	// the rider's own MoveComp above — without this, strafing while aiming would have CharacterMovement
	// fighting this direct rotation override every tick) and restored to normal movement-facing rotation
	// the instant aiming stops, so the mount still turns to face travel direction during ordinary
	// (non-aiming) riding.
	if (bCurrentlyMounted && IsValid(CurrentMountedMonster))
	{
		if (UCharacterMovementComponent* MonsterMoveComp = CurrentMountedMonster->GetCharacterMovement())
		{
			MonsterMoveComp->bOrientRotationToMovement = !bIsAiming;
		}

		if (bIsAiming)
		{
			if (const APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				CurrentMountedMonster->SetActorRotation(FRotator(0.0f, PC->GetControlRotation().Yaw, 0.0f));
			}
		}
	}

	// Runs every tick regardless of bIsAiming (not just while aiming) — it has to, so releasing RMB
	// actually interpolates the boom back to DefaultCameraArmLength/SocketOffset instead of snapping or
	// freezing wherever it was. FInterpTo/VInterpTo are self-terminating (converge and stay put once
	// close enough), so there's no need to gate this on "is it still moving".
	if (IsValid(CameraBoomComponent))
	{
		// IsMounted() takes priority over bIsAiming for arm length specifically (a wide rodeo view matters
		// more than an ADS zoom while riding) — reusing this same FInterpTo, rather than the mount/dismount
		// path setting TargetArmLength directly, is deliberate: this block runs every tick regardless of
		// mount state, so a direct set on mount/dismount would just get immediately pulled back toward
		// AimCameraArmLength/DefaultCameraArmLength by this same code the very next frame.
		const float TargetArmLength = bCurrentlyMounted ? MountCameraArmLength : (bIsAiming ? AimCameraArmLength : DefaultCameraArmLength);
		FVector TargetSocketOffset = bIsAiming ? AimCameraSocketOffset : DefaultCameraSocketOffset;

		// Crouch camera dip — see CrouchCameraZOffset's own comment on why this rides the same
		// FInterpTo/VInterpTo blend as the aim offset above instead of its own separate interpolant.
		// Added on top of whichever base offset was just picked (aiming or not), so crouching while
		// aiming still lowers the view by the same amount as crouching while not.
		if (IsCrouched())
		{
			TargetSocketOffset.Z += CrouchCameraZOffset;
		}

		CameraBoomComponent->TargetArmLength = FMath::FInterpTo(CameraBoomComponent->TargetArmLength, TargetArmLength, DeltaTime, CameraAimInterpSpeed);
		CameraBoomComponent->SocketOffset = FMath::VInterpTo(CameraBoomComponent->SocketOffset, TargetSocketOffset, DeltaTime, CameraAimInterpSpeed);
	}

	// ADS-style FOV zoom, same always-runs/self-terminating reasoning as the arm-length/socket-offset
	// block above — has to run every tick (not just while aiming) so releasing RMB smoothly zooms back
	// out instead of snapping.
	if (IsValid(FollowCameraComponent))
	{
		const float TargetFOV = bIsAiming ? AimCameraFOV : DefaultCameraFOV;
		FollowCameraComponent->SetFieldOfView(FMath::FInterpTo(FollowCameraComponent->FieldOfView, TargetFOV, DeltaTime, CameraAimInterpSpeed));
	}

	if (bIsAiming)
	{
		UpdateAimCrosshair();
	}

	// Sprint/crouch interaction + AI noise generation. Checked every tick (not just at the moment Left
	// Alt was toggled) so a crouch that starts mid-sprint (e.g. APriestCharacter::OnHeldItemsChanged()
	// auto-crouching while holding a lit Candle) still cuts the sprint immediately rather than waiting for
	// the next ToggleSprint() press. StopSprinting() here restores MaxWalkSpeed to the un-crouched 300
	// baseline, but the character's actual movement speed while crouched still comes out to 150 either
	// way, since GetCharacterMovement()->GetMaxSpeed() reads MaxWalkSpeedCrouched instead of MaxWalkSpeed
	// for as long as IsCrouched() is true.
	if (bIsSprinting && IsCrouched())
	{
		StopSprinting();
	}

	if (bIsSprinting)
	{
		// "During movement" — a stationary sprint-hold (e.g. blocked by a wall) shouldn't keep pulsing
		// noise into the world. Squared 2D velocity avoids a sqrt for a plain "is it moving" threshold
		// check.
		const bool bIsActuallyMoving = GetVelocity().SizeSquared2D() > FMath::Square(10.0f);
		if (bIsActuallyMoving)
		{
			const float CurrentTime = GetWorld()->GetTimeSeconds();
			if (CurrentTime >= LastSprintNoiseTime + SprintNoiseInterval)
			{
				LastSprintNoiseTime = CurrentTime;
				MakeNoise(SprintNoiseLoudness, this, GetActorLocation(), SprintNoiseRadius);
				UE_LOG(LogTemp, Verbose, TEXT("%s: sprint noise pulse (Loudness=%.1f Radius=%.0f)"),
					*GetName(), SprintNoiseLoudness, SprintNoiseRadius);
			}
		}
	}

	if (GEngine)
	{
		const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
		GEngine->AddOnScreenDebugMessage(260, 0.0f, bIsSprinting ? FColor::Cyan : FColor::White,
			FString::Printf(TEXT("bIsSprinting: %s  MaxWalkSpeed: %.0f"),
				bIsSprinting ? TEXT("TRUE") : TEXT("FALSE"), MoveComp ? MoveComp->MaxWalkSpeed : -1.0f));
		GEngine->AddOnScreenDebugMessage(261, 0.0f, IsCrouched() ? FColor::Orange : FColor::White,
			FString::Printf(TEXT("IsCrouched: %s"), IsCrouched() ? TEXT("TRUE") : TEXT("FALSE")));
	}

	// Interaction prompt (see BuildInteractionPromptText()) is independent of bIsAiming — it should show
	// whenever HighlightedItem is set, whether or not the player is also aiming. Combined with bIsAiming
	// below to decide the WHOLE widget's visibility: it needs to stay visible for the prompt alone even
	// while not aiming (dot hidden via bShowDot instead), and must be explicitly Collapsed the instant
	// BOTH conditions are false, or it'd stay visible at its last drawn state forever — a persistent
	// widget, not a single-frame transient draw call.
	if (IsValid(CrosshairWidgetInstance))
	{
		const FString PromptString = BuildInteractionPromptText();
		CrosshairWidgetInstance->bShowDot = bIsAiming;
		CrosshairWidgetInstance->PromptText = FText::FromString(PromptString);

		const bool bShouldBeVisible = bIsAiming || !PromptString.IsEmpty();
		CrosshairWidgetInstance->SetVisibility(bShouldBeVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Input-chain diagnostic for the "F does nothing at all, not even an on-screen message" report: if
	// SOMETHING upstream (a leftover UI widget's InputModeUIOnly, bBlockInput on either the controller or
	// this pawn, etc.) is silently swallowing key events before they ever reach the bindings below, no
	// amount of adding more bindings fixes that — this function wouldn't even be the problem, the input
	// would just never get this far. Logged loudly (not silently corrected only) so the actual prior state
	// is visible in the Output Log even once forced back to sane values below.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::SetupPlayerInputComponent: PlayerController=%s InputMode-relevant state: bShowMouseCursor=%s bBlockInput(PC)=%s bBlockInput(Pawn)=%s"),
			*GetName(), *PC->GetName(),
			PC->bShowMouseCursor ? TEXT("true") : TEXT("false"),
			PC->bBlockInput ? TEXT("true") : TEXT("false"),
			bBlockInput ? TEXT("true") : TEXT("false"));

		// Forced to plain Game-only input every time this pawn's input is (re)bound — if a UI widget
		// upstream left the controller in InputModeUIOnly (which routes ALL keyboard input to the widget
		// and none of it ever reaches PlayerInputComponent bindings at all), this is what actually
		// restores gameplay key presses rather than just reporting the problem.
		PC->SetInputMode(FInputModeGameOnly());
		PC->bBlockInput = false;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("%s::SetupPlayerInputComponent: GetController() is not a valid APlayerController — this pawn cannot receive player input at all"), *GetName());
	}
	bBlockInput = false;

	// Enhanced Input: Move/Look/MouseLook/Jump — bound here (not per Blueprint subclass) so WASD movement,
	// mouse look, and jump all work the instant any BP_* hero is possessed. DefaultInputComponentClass is
	// already set to EnhancedInputComponent project-wide (see DefaultEngine.ini), so this Cast always
	// succeeds; the guard is just defensive.
	if (UEnhancedInputComponent* EnhancedInputComp = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComp->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABaseCharacter::Move);
		}
		if (LookAction)
		{
			EnhancedInputComp->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABaseCharacter::Look);
		}
		if (MouseLookAction)
		{
			EnhancedInputComp->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ABaseCharacter::Look);
		}
		if (JumpAction)
		{
			// Started/Completed (not Triggered/Ongoing) so this fires once on press/release, matching
			// OnJumpOrDismountPressed()/OnJumpOrDismountReleased()'s existing press/release semantics
			// used by the legacy SpaceBar BindKey fallback below.
			EnhancedInputComp->BindAction(JumpAction, ETriggerEvent::Started, this, &ABaseCharacter::OnJumpOrDismountPressed);
			EnhancedInputComp->BindAction(JumpAction, ETriggerEvent::Completed, this, &ABaseCharacter::OnJumpOrDismountReleased);
		}
	}

	// "CloseEyes" action mapping is bound to LeftShift in DefaultInput.ini — toggled on IE_Pressed only
	// (no IE_Released binding) so one press closes eyes and the next press opens them again, instead of
	// requiring Shift to be held the whole time.
	PlayerInputComponent->BindAction("CloseEyes", IE_Pressed, this, &ABaseCharacter::ToggleEyesClosed);

	// "Interact" action mapping is bound to E in DefaultInput.ini.
	PlayerInputComponent->BindAction("Interact", IE_Pressed, this, &ABaseCharacter::Interact);

	// Bear Trap mash-to-escape — no named Input Action mapping exists for this (a new one would need
	// hand-editing DefaultInput.ini, which the rest of this project avoids relying on), so this is a
	// direct BindKey() only, same "guaranteed, zero-setup" pattern as the quick-slot number keys below.
	// Deliberately reuses E (also "Interact" above) rather than a fresh key — MashTrapEscape() itself
	// no-ops unless bIsTrapped, and Interact() now skips itself entirely while trapped (see its own
	// comment), so the two can never fire meaningfully at the same time.
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ABaseCharacter::MashTrapEscape);

	// "Aim" action mapping is bound to RightMouseButton in DefaultInput.ini. Left in place for anyone
	// calling StartAiming()/StopAiming() directly, but no longer load-bearing for bIsAiming itself —
	// Tick() now polls IsInputKeyDown(RightMouseButton) directly every frame as the authoritative
	// source, since this action-mapping path was confirmed (via Output Log) to never fire at all.
	PlayerInputComponent->BindAction("Aim", IE_Pressed, this, &ABaseCharacter::StartAiming);
	PlayerInputComponent->BindAction("Aim", IE_Released, this, &ABaseCharacter::StopAiming);

	// "Throw" action mapping is bound to LeftMouseButton in DefaultInput.ini.
	PlayerInputComponent->BindAction("Throw", IE_Pressed, this, &ABaseCharacter::ThrowItem);

	// Guaranteed fallback for Throw, same reasoning as the Tick()-polled Aim fallback above: BindKey()
	// binds directly to the physical key, bypassing the DefaultInput.ini ActionMappings table/Enhanced
	// Input entirely, so it can't be silently dropped the same way "Throw" above apparently was. Both
	// paths DO reliably fire for the same LeftMouseButton press (confirmed by the dual-candle-throw bug
	// this caused), so — unlike the Aim fallback, which only ever needs bIsAiming to already be false to
	// no-op safely — ThrowItem() itself now has an explicit same-frame duplicate-call guard
	// (LastThrowActionTime) specifically so this intentional double-binding stays safe.
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ABaseCharacter::ThrowItem);

	// Debug/verification tool — see SpawnAimTargetMarker()'s header comment.
	PlayerInputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &ABaseCharacter::SpawnAimTargetMarker);

	// "MeleeSwing" (Q) is no longer bound to anything — SwingHeldCenser() is now called from ThrowItem()
	// itself (Aim+LMB) when the throwing hand holds a Censer, instead of a separate Q keybind. Q does
	// nothing for a Rider now.

	// "Mount" (F) is NOT bound here — mounting is isolated entirely in ARiderCharacter/
	// UMountRiderComponent (see that class), which binds it itself from its own
	// SetupPlayerInputComponent() override. Only ARiderCharacter can ever execute a mount.

	// Sprint — no named Input Action mapping exists for this (same "direct BindKey(), no project-settings
	// editing required" reasoning as Bear Trap's E binding). Toggle (not press-and-hold): one Left Alt
	// press flips bIsSprinting via ToggleSprint(); the next press flips it back — same "press once to
	// toggle" pattern as Left Shift's ToggleEyesClosed(), rather than requiring the key held down for the
	// whole sprint like the earlier StartSprinting()/StopSprinting() Pressed/Released binding did.
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &ABaseCharacter::ToggleSprint);

	// Crouch — no named Input Action mapping exists for this (same reasoning as Sprint's own Left Alt
	// binding above), toggled (not press-and-hold) on 'C'. Free for this since the earlier Walk/Run
	// toggle that used to live on 'C' was replaced by the Sprint system above.
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &ABaseCharacter::ToggleCrouch);

	// "Прощупывание" — общая механика для всех героев, отдельного Input Action не заводим, тот же
	// "direct BindKey(), no project-settings dependency" приём, что и у ToggleSprint()/ToggleCrouch() выше.
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &ABaseCharacter::OnProbePressed);

	// "Укрытия" — общая механика для всех героев. Третий независимый слушатель E (см.
	// OnHidingTogglePressed()'s own comment) — Interact()/MashTrapEscape()'s собственные BindKey(E, ...)
	// above are untouched.
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ABaseCharacter::OnHidingTogglePressed);

	// Quick-slot selection — Number keys 1-4 select directly, mouse wheel steps forward/backward. Every
	// hero has an InventoryComponent (created in the constructor), so this is bound here rather than
	// per-subclass.
	PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ABaseCharacter::OnSelectQuickSlot1);
	PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ABaseCharacter::OnSelectQuickSlot2);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ABaseCharacter::OnSelectQuickSlot3);
	PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ABaseCharacter::OnSelectQuickSlot4);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ABaseCharacter::OnScrollSelectNext);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ABaseCharacter::OnScrollSelectPrevious);

	// Space: dismounts while mounted, jumps normally otherwise — see OnJumpOrDismountPressed(). Also
	// bound to OnBreathPressed() for the Nose-Breathing panic mini-game — both fire on the same press,
	// same "both bound, each self-gates" pattern as Interact()/MashTrapEscape() sharing E; OnJumpOr
	// DismountPressed() itself now no-ops while bIsPanicking so the two can never fire meaningfully at
	// the same time.
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ABaseCharacter::OnJumpOrDismountPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ABaseCharacter::OnJumpOrDismountReleased);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ABaseCharacter::OnBreathPressed);
}

void ABaseCharacter::CloseEyes()
{
	if (bEyesLockedOpen)
	{
		return;
	}

	bIsEyesClosed = true;

	OnEyesClosedChanged(true);
}

void ABaseCharacter::ToggleEyesClosed()
{
	if (bIsEyesClosed)
	{
		OpenEyes();
	}
	else
	{
		CloseEyes();
	}
}

void ABaseCharacter::OpenEyes()
{
	if (bEyesLockedClosed)
	{
		return;
	}

	bIsEyesClosed = false;

	// Instant kill: opening eyes mid-Scan or mid-Panic (both set BlindEncounterMonster the moment
	// physical contact happens in the dark — see ABaseMonster::OnCapsuleHit()) means the monster sees you
	// see it — game over. Checked here, at the exact moment eyes open, rather than polled, so it fires
	// immediately regardless of which Scenario (A or B) is currently active.
	if (BlindEncounterMonster.IsValid())
	{
		ABaseMonster* AttackingMonster = BlindEncounterMonster.Get();
		BlindEncounterMonster.Reset();
		AttackingMonster->ResolveBlindEncounterKill(this);
	}

	OnEyesClosedChanged(false);
}

void ABaseCharacter::ForceEyesOpen()
{
	if (!bIsEyesClosed)
	{
		return;
	}

	bIsEyesClosed = false;
	OnEyesClosedChanged(false);
}

void ABaseCharacter::ScanForInteractable()
{
	// GetActorEyesViewPoint (not the camera manager) so TraceStart is the pawn's eye point, which we
	// then push forward — a trace starting exactly at/behind the camera can clip the player's own
	// capsule/mesh (visible from a third-person or over-the-shoulder camera) before it ever leaves.
	FVector EyesLocation;
	FRotator EyesRotation;
	GetActorEyesViewPoint(EyesLocation, EyesRotation);

	const FVector ForwardVector = EyesRotation.Vector();
	const FVector TraceStart = EyesLocation + (ForwardVector * 60.0f);
	const FVector TraceEnd = TraceStart + (ForwardVector * InteractTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (GetMesh())
	{
		QueryParams.AddIgnoredComponent(GetMesh());
	}
	if (IsValid(EquippedRightHand))
	{
		QueryParams.AddIgnoredActor(EquippedRightHand);
	}
	if (IsValid(EquippedLeftHand))
	{
		QueryParams.AddIgnoredActor(EquippedLeftHand);
	}

	// Swept sphere instead of a pixel-thin line trace, so the player doesn't have to aim dead-center
	// on an item to have it register as interactable.
	GetWorld()->SweepSingleByChannel(LastScanHit, TraceStart, TraceEnd, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(InteractSweepRadius), QueryParams);

	// GetActor() itself goes through FActorInstanceHandle resolution internally and can hit an
	// engine-side edge case that hard-crashes on a bad cast before we ever get a return value to
	// check — no amount of IsValid() on our end after calling it can catch that. GetComponent() ->
	// GetOwner() is a plain cached-pointer lookup that doesn't go through that resolution path.
	UPrimitiveComponent* HitComponent = LastScanHit.GetComponent();
	if (IsValid(HitComponent))
	{
		AActor* HitOwner = HitComponent->GetOwner();
		if (IsValid(HitOwner))
		{
			AHolyItem* HitItem = Cast<AHolyItem>(HitOwner);
			if (HitItem)
			{
				if (HighlightedItem != HitItem)
				{
					if (HighlightedItem)
					{
						HighlightedItem->DisableHighlight();
					}
					HighlightedItem = HitItem;
					HighlightedItem->EnableHighlight();
					OnHighlightedItemChanged.Broadcast(HighlightedItem);
				}
				return;
			}
		}
	}

	// Ray hit nothing, or hit something that isn't a HolyItem: clear any existing highlight.
	if (HighlightedItem)
	{
		HighlightedItem->DisableHighlight();
		HighlightedItem = nullptr;
		OnHighlightedItemChanged.Broadcast(nullptr);
	}
}

FString ABaseCharacter::BuildInteractionPromptText() const
{
	// Bear Trap QTE takes total priority over the ordinary pickup/drop prompt below — escaping is the
	// only thing E does while trapped (see Interact()'s own comment), so the prompt should say so
	// instead of whatever item happens to be highlighted underneath the character at the time. Reuses
	// UCrosshairWidget's existing PromptText/MakeText draw call rather than a new widget — proven to
	// render correctly regardless of the unresolved box-rendering issue documented on
	// UPlayerHUDWidget::PaintQuickSlot().
	if (bIsTrapped)
	{
		return FString::Printf(TEXT("MASH [E] TO ESCAPE! (%d/%d)"), TrapEscapePresses, TrapEscapePressesRequired);
	}

	// Hunter's Focus step-count readout — below the (mutually-exclusive-in-practice) trap prompt but
	// above the ordinary pickup prompt, since it's an active, deliberately-triggered ability readout, not
	// passive world-highlighting like HighlightedItem below. Empty on every hero but ARedneckCharacter —
	// see GetHunterFocusPromptText()'s own comment.
	const FString FocusPrompt = GetHunterFocusPromptText();
	if (!FocusPrompt.IsEmpty())
	{
		return FocusPrompt;
	}

	if (!IsValid(HighlightedItem))
	{
		return FString();
	}

	// Only AHolyItem pickup exists today — see this function's own header comment for where a future
	// generic interactable (a door/gate/etc.) would add its own prompt text.
	const TCHAR* ItemLabel = (HighlightedItem->ItemType == EHolyItemType::Candle) ? TEXT("Candle") : TEXT("Censer");
	return FString::Printf(TEXT("[E] Pick Up %s"), ItemLabel);
}

void ABaseCharacter::OnSelectQuickSlot1()
{
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->SelectSlot(0);
	}
}

void ABaseCharacter::OnSelectQuickSlot2()
{
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->SelectSlot(1);
	}
}

void ABaseCharacter::OnSelectQuickSlot3()
{
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->SelectSlot(2);
	}
}

void ABaseCharacter::OnSelectQuickSlot4()
{
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->SelectSlot(3);
	}
}

void ABaseCharacter::OnScrollSelectNext()
{
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->SelectNextSlot();
	}
}

void ABaseCharacter::OnScrollSelectPrevious()
{
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->SelectPreviousSlot();
	}
}

void ABaseCharacter::OnProbePressed()
{
	if (IsValid(ProbeComponent))
	{
		ProbeComponent->Probe();
	}
}

void ABaseCharacter::OnHidingTogglePressed()
{
	if (IsValid(HidingComponent))
	{
		HidingComponent->TryToggle();
	}
}

bool ABaseCharacter::IsHiddenInSpot() const
{
	return IsValid(HidingComponent) && HidingComponent->IsHidden();
}

void ABaseCharacter::Interact()
{
	// E is shared with MashTrapEscape() (see SetupPlayerInputComponent()) — while trapped, mashing E
	// should only ever advance the escape QTE, not also pick up/drop whatever's highlighted/held.
	if (bIsTrapped)
	{
		return;
	}

	// Item pickup/drop ONLY — mounting is a separate action entirely, isolated in ARiderCharacter/
	// UMountRiderComponent (bound to F there), so it can never conflict with item interaction on this
	// key.
	if (IsValid(HighlightedItem))
	{
		PickUp(HighlightedItem);
		return;
	}

	// Nothing highlighted: if a hand is occupied, drop it at the last interact-scan hit point
	// instead of running a fresh trace.
	if (IsValid(EquippedRightHand))
	{
		EquippedRightHand->DropItem(LastScanHit.Location, GetActorRotation());
		EquippedRightHand = nullptr;
		// Immediate HUD refresh — see PickUp()'s matching call for why this can't just wait for the next
		// auto-refresh tick.
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
		OnHeldItemsChanged();
		return;
	}
	if (IsValid(EquippedLeftHand))
	{
		EquippedLeftHand->DropItem(LastScanHit.Location, GetActorRotation());
		EquippedLeftHand = nullptr;
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
		OnHeldItemsChanged();
	}
}

void ABaseCharacter::OnJumpOrDismountPressed()
{
	// Space is shared with OnBreathPressed() (see SetupPlayerInputComponent()) — while panicking, Space
	// should only ever feed the Nose-Breathing mini-game, not also try to jump/dismount while lying on
	// the ground.
	if (bIsPanicking)
	{
		return;
	}

	if (IsMounted())
	{
		Dismount();
		return;
	}

	Jump();
}

void ABaseCharacter::OnJumpOrDismountReleased()
{
	// Harmless if not currently jumping (including while mounted, where Jump() was never called) —
	// ACharacter::StopJumping() just clears the jump-hold state.
	StopJumping();
}

void ABaseCharacter::PickUp(AHolyItem* Item)
{
	if (!IsValid(Item))
	{
		return;
	}

	// Prefer the right hand; fall back to the left if the right is already full. No-op if both are full.
	if (!IsValid(EquippedRightHand))
	{
		Item->PickUp(GetMesh(), RightHandSocket);
		EquippedRightHand = Item;
	}
	else if (!IsValid(EquippedLeftHand))
	{
		// Left hand is always the secondary candle: the hold animation still poses it forward like the
		// right hand (no distinct "held behind the back" animation exists), so its actual protection
		// cone is inverted to face backward instead — see AHolyItem::bInvertProtectionForward.
		Item->PickUp(GetMesh(), LeftHandSocket, /*bInvertForward=*/true);
		EquippedLeftHand = Item;
	}
	else
	{
		return;
	}

	// The item just left the world into a hand, so it shouldn't stay outlined as "pick-up-able" —
	// clear it immediately rather than waiting up to 0.1s for the next scan tick to notice.
	if (HighlightedItem == Item)
	{
		Item->DisableHighlight();
		HighlightedItem = nullptr;
		OnHighlightedItemChanged.Broadcast(nullptr);
	}

	// Immediate HUD refresh — same reasoning as ThrowItem()'s own matching call: without this, the
	// quick-slot bar wouldn't show the newly-equipped light source until AutoRefreshTimerHandle's next
	// tick (up to AutoRefreshInterval seconds later).
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->RefreshLightSourceSlot();
	}
	OnHeldItemsChanged();
}

void ABaseCharacter::StartAiming()
{
	bIsAiming = true;

	// Set here too for any direct BlueprintCallable caller — but Tick() re-applies the same two flags
	// from bIsAiming every frame regardless, since that (not this function) is the actually-authoritative
	// source of bIsAiming (see Tick()'s IsInputKeyDown() comment). Kept here so this function alone
	// still does the right thing if called directly instead of through the RMB poll.
	bUseControllerRotationYaw = true;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = false;
	}
}

void ABaseCharacter::StopAiming()
{
	bIsAiming = false;

	// See StartAiming()'s comment — same "set here for direct callers, but Tick() is what actually
	// enforces it every frame" reasoning.
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
	}
}

bool ABaseCharacter::TraceAimTarget(FVector& OutTargetPoint, FHitResult& OutHit) const
{
	// Center-screen crosshair: the active camera, not GetControlRotation()/GetActorEyesViewPoint(),
	// is the only thing guaranteed to match what's actually on screen in third person.
	//
	// This character's OWN controller, not GetWorld()->GetFirstPlayerController() — in any multi-
	// controller scenario those can differ; using this actor's actual controller is the only way to
	// be certain the camera read here is the one actually rendering this character's view. Tick()'s
	// IsInputKeyDown() poll uses the same source for the same reason.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const APlayerCameraManager* CamManager = PC ? PC->PlayerCameraManager : nullptr;
	if (!IsValid(CamManager))
	{
		OutTargetPoint = GetActorLocation();
		OutHit = FHitResult();
		return false;
	}

	const FVector CameraLoc = CamManager->GetCameraLocation();
	const FVector CameraForward = CamManager->GetCameraRotation().Vector();

	// Trace starts exactly at the camera (not offset forward along its own ray), so TraceStart/TraceEnd
	// sit on precisely the ray the player sees on screen — the previous forward offset skipped the
	// first stretch of that same ray, which is the suspected cause of the reticle pinning near the
	// character's feet in normal (non-pitched-down) TPS camera views. The player's own capsule/mesh
	// right next to CameraLoc is handled by the ignore list below instead of by skipping ahead of it.
	const FVector TraceStart = CameraLoc;
	const FVector TraceEnd = CameraLoc + (CameraForward * AimTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (GetMesh())
	{
		QueryParams.AddIgnoredComponent(GetMesh());
	}
	if (GetCapsuleComponent())
	{
		QueryParams.AddIgnoredComponent(GetCapsuleComponent());
	}
	if (IsValid(EquippedRightHand))
	{
		QueryParams.AddIgnoredActor(EquippedRightHand);
	}
	if (IsValid(EquippedLeftHand))
	{
		QueryParams.AddIgnoredActor(EquippedLeftHand);
	}

	// While mounted, the camera sits right above/next to the ridden monster's own body — a
	// steep upward aim (e.g. at a ceiling AHookPoint) can otherwise clip the monster itself
	// before reaching the real target, misclassifying the crosshair (green, as if aimed at a
	// mountable monster) and making ARiderCharacter::OnMountOrHookPressed()'s Cast<AHookPoint>
	// fail even when genuinely aimed at a hook. Only IsMounted()/GetMountedMonster() are used
	// here (both already virtual on ABaseCharacter, satisfied by ARiderCharacter) — no new
	// dependency on ARiderCharacter/UMountRiderComponent from this shared function.
	if (IsMounted())
	{
		if (ABaseMonster* CurrentMountedMonster = GetMountedMonster())
		{
			if (CurrentMountedMonster->GetMesh())
			{
				QueryParams.AddIgnoredComponent(CurrentMountedMonster->GetMesh());
			}
			if (CurrentMountedMonster->GetCapsuleComponent())
			{
				QueryParams.AddIgnoredComponent(CurrentMountedMonster->GetCapsuleComponent());
			}
		}
	}

	// ECC_Visibility, matching the same channel ScanForInteractable() already uses for gameplay
	// raycasts elsewhere in this class — the earlier detour through ECC_Camera and a hard distance
	// guard was chasing a symptom that turned out to be a stale/corrupted build (see project memory),
	// not a real collision-channel problem; the plain ignore-list above is sufficient now.
	const bool bHitSomething = GetWorld()->LineTraceSingleByChannel(OutHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	OutTargetPoint = bHitSomething ? OutHit.ImpactPoint : TraceEnd;
	return bHitSomething;
}

bool ABaseCharacter::TraceHunterFocusTarget(float MaxDistance, FVector& OutTargetPoint, FHitResult& OutHit) const
{
	// Same camera-viewpoint/ignore-list/channel reasoning as TraceAimTarget() above — only MaxDistance
	// differs, so it's duplicated rather than shared, to keep the aim/throw reticle's own AimTraceDistance
	// completely independent of Hunter's Focus's range.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const APlayerCameraManager* CamManager = PC ? PC->PlayerCameraManager : nullptr;
	if (!IsValid(CamManager))
	{
		OutTargetPoint = GetActorLocation();
		OutHit = FHitResult();
		return false;
	}

	const FVector CameraLoc = CamManager->GetCameraLocation();
	const FVector CameraForward = CamManager->GetCameraRotation().Vector();

	const FVector TraceStart = CameraLoc;
	const FVector TraceEnd = CameraLoc + (CameraForward * MaxDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (GetMesh())
	{
		QueryParams.AddIgnoredComponent(GetMesh());
	}
	if (GetCapsuleComponent())
	{
		QueryParams.AddIgnoredComponent(GetCapsuleComponent());
	}
	if (IsValid(EquippedRightHand))
	{
		QueryParams.AddIgnoredActor(EquippedRightHand);
	}
	if (IsValid(EquippedLeftHand))
	{
		QueryParams.AddIgnoredActor(EquippedLeftHand);
	}

	const bool bHitSomething = GetWorld()->LineTraceSingleByChannel(OutHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	OutTargetPoint = bHitSomething ? OutHit.ImpactPoint : TraceEnd;
	return bHitSomething;
}

void ABaseCharacter::ThrowItem()
{
	// See TryHijackAttackPress()'s own comment — a subclass gets first refusal on this same LMB press
	// (e.g. ARedneckCharacter's RMB+LMB Bulls Charge combo) before any of this base throw logic runs.
	if (TryHijackAttackPress())
	{
		return;
	}

	// Restricted to aiming: a throw is a deliberate, aimed action (see UpdateAimCrosshair()'s crosshair
	// dot, which is the player's only feedback on where a throw would actually go), not something LMB
	// should fire off on its own.
	if (!bIsAiming)
	{
		return;
	}

	// Swallow a same-frame duplicate call — see LastThrowActionTime's own header comment on why
	// SetupPlayerInputComponent() binding both the "Throw" action mapping AND a direct BindKey fallback
	// to the same LeftMouseButton press means this function runs twice per click. Without this guard, the
	// first call below throws the right-hand candle and the second (same click) immediately throws the
	// left-hand one too instead of leaving it lit.
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (FMath::IsNearlyEqual(CurrentTime, LastThrowActionTime, KINDA_SMALL_NUMBER))
	{
		return;
	}

	// Right hand (primary/active hand) preferred, matching PickUp()'s own hand preference order — so a
	// dual-wielding Priest throws the right-hand candle first, leaving the left-hand one lit until a
	// follow-up press throws it too.
	AHolyItem* ItemToThrow = IsValid(EquippedRightHand) ? EquippedRightHand : EquippedLeftHand;
	if (!IsValid(ItemToThrow))
	{
		return;
	}

	// Censer no longer throws — Aim+LMB now swings it in place (PerformMeleeSwing(), mounted-only, costs
	// fuel) instead of launching it as a projectile. Candle is untouched, falls through to the ordinary
	// throw logic below exactly as before.
	if (ItemToThrow->ItemType == EHolyItemType::Censer)
	{
		SwingHeldCenser();
		return;
	}

	// Only consumed once an actual throw is going to happen (not on an empty-handed no-op above), so an
	// empty click can't block a genuine throw attempt on the very next frame.
	LastThrowActionTime = CurrentTime;

	FVector TargetPoint;
	FHitResult AimHit;
	TraceAimTarget(TargetPoint, AimHit);

	// Spawn/launch from whichever hand is actually throwing, not the camera — the camera is only the
	// AIM origin (what TraceAimTarget() traces from), the hand socket is the physical throw origin.
	const FName HandSocket = (ItemToThrow == EquippedRightHand) ? RightHandSocket : LeftHandSocket;
	FVector ThrowLocation = IsValid(GetMesh()) ? GetMesh()->GetSocketLocation(HandSocket) : GetActorLocation();

	// Mounted-throw fix: the hand socket sits directly on/inside the mounted monster's own capsule while
	// mounted, so a thrown item's ItemMesh (QueryAndPhysics, blocking ECC_Pawn) registers a Frame-0
	// OnComponentHit against the mount before its launch velocity ever carries it away — this is what
	// stops the self-hit from being possible at all, rather than relying only on AHolyItem::Throw()'s
	// own IgnoreActorWhenMoving guard (kept as belt-and-braces there, not a replacement for this).
	// Pushed along the CONTROL rotation — the same aim direction TargetPoint was traced from — rather
	// than the hand socket's own forward, so the push is always away from camera-forward regardless of
	// the hand socket's current animated orientation. ClearanceDistance is computed from the mounted
	// monster's actual capsule radius (same "real dimensions over a flat guess" reasoning as
	// UMountRiderComponent's MountRiderHeightBuffer) so it scales correctly for a bigger/smaller ridden
	// monster, falling back to MountedThrowFallbackOffset only if the capsule is somehow invalid.
	if (IsMounted())
	{
		ABaseMonster* CurrentMountedMonster = GetMountedMonster();

		float ClearanceDistance = MountedThrowFallbackOffset;
		if (IsValid(CurrentMountedMonster) && IsValid(CurrentMountedMonster->GetCapsuleComponent()))
		{
			ClearanceDistance = CurrentMountedMonster->GetCapsuleComponent()->GetScaledCapsuleRadius() + MountedThrowClearanceBuffer;
		}

		const FVector AimForward = GetControlRotation().Vector();
		ThrowLocation += AimForward * ClearanceDistance;

		UE_LOG(LogTemp, Warning, TEXT("ThrowItem() mounted: pushed launch origin %.0f units clear of %s"),
			ClearanceDistance, IsValid(CurrentMountedMonster) ? *CurrentMountedMonster->GetName() : TEXT("None"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(222, 2.5f, FColor::Yellow,
				FString::Printf(TEXT("ThrowItem() mounted: launch origin pushed %.0f units clear"), ClearanceDistance));
		}
	}

	// Synchronized trajectory: launch TOWARD TargetPoint (the same point the reticle is drawn at)
	// rather than just along the camera's raw forward vector — otherwise a hand-offset throw origin
	// would fly parallel to, but not actually through, the crosshair's target point. This is the
	// standard third-person-shooter "barrel doesn't match the camera, but the shot still goes where
	// the crosshair points" convergence.
	const FVector ThrowDirection = (TargetPoint - ThrowLocation).GetSafeNormal();

	// AHolyItem::Throw() relocates via SetActorLocation(..., bSweep=false, ...) — a direct teleport,
	// not a swept collision query — so there is no FCollisionQueryParams here needing `this` added to
	// its ignore list; TraceAimTarget() above is what handles ignoring the thrower for the aim trace.
	ItemToThrow->Throw(ThrowLocation, ThrowDirection * ThrowSpeed);

	// Cleared immediately (not deferred to any callback) — see ThrowItem()'s header comment on why
	// light-protection needs this to happen right now rather than on impact/landing.
	if (ItemToThrow == EquippedRightHand)
	{
		EquippedRightHand = nullptr;
	}
	else if (ItemToThrow == EquippedLeftHand)
	{
		EquippedLeftHand = nullptr;
	}

	if (HighlightedItem == ItemToThrow)
	{
		HighlightedItem = nullptr;
	}

	// Immediate HUD refresh — RefreshLightSourceSlot() would otherwise not re-check EquippedRightHand/
	// EquippedLeftHand until AutoRefreshTimerHandle's next tick (up to AutoRefreshInterval seconds later),
	// leaving the quick-slot bar showing the just-thrown candle as still equipped for a beat. Reflects
	// whichever hand (if any) is still lit — the left-hand candle if only the right was just thrown, or
	// Empty if that was the last one — exactly like every other piece of state this class clears
	// immediately rather than deferring to a poll tick (see PickUp()'s own HighlightedItem handling).
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->RefreshLightSourceSlot();
	}
	OnHeldItemsChanged();
}

void ABaseCharacter::SwingHeldCenser()
{
	// Right hand preferred, matching PickUp()/ThrowItem()'s own hand preference order.
	AHolyItem* CenserItem = nullptr;
	if (IsValid(EquippedRightHand) && EquippedRightHand->ItemType == EHolyItemType::Censer)
	{
		CenserItem = EquippedRightHand;
	}
	else if (IsValid(EquippedLeftHand) && EquippedLeftHand->ItemType == EHolyItemType::Censer)
	{
		CenserItem = EquippedLeftHand;
	}

	// PerformMeleeSwing() itself already no-ops for a Candle, but checking ItemType here first avoids
	// even attempting a swing when neither hand holds a Censer at all.
	if (IsValid(CenserItem))
	{
		CenserItem->PerformMeleeSwing();
	}
}

void ABaseCharacter::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	// While IsMounted(), every movement-input call is redirected to GetMountedMonster() instead of moving
	// this character's own (collision-disabled, attached) capsule.
	ABaseMonster* CurrentMountedMonster = GetMountedMonster();
	if (IsMounted() && IsValid(CurrentMountedMonster))
	{
		const APlayerController* PC = Cast<APlayerController>(GetController());
		if (!IsValid(PC))
		{
			// No controller to read control rotation from (shouldn't happen for the locally-controlled
			// rider, but stay safe) — fall back to passing the direction through as-is rather than
			// dropping the input entirely.
			CurrentMountedMonster->AddMovementInput(WorldDirection, ScaleValue, bForce);
			return;
		}

		// Re-expressed relative to the CONTROLLER's own camera/control rotation rather than trusting the
		// incoming WorldDirection as-is — while mounted this character's own actor rotation now tracks the
		// MONSTER's facing (see UMountRiderComponent's relative-rotation fix), not the camera, so an
		// actor-forward-relative direction would otherwise steer relative to the mount instead of the
		// camera. Move() (this class's Enhanced Input handler, see its own comment) issues each axis as
		// its own separate AddMovementInput() call with a pure (unit-length) direction vector and the sign
		// carried entirely in ScaleValue — so WorldDirection here is reliably EITHER a forward call or a
		// right call, never a diagonal combination, and comparing |dot| against each control-rotation axis
		// correctly identifies which one this is.
		const FRotator YawRotation(0.0f, PC->GetControlRotation().Yaw, 0.0f);
		const FVector ControlForward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector ControlRight = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		const FVector NormalizedInput = WorldDirection.GetSafeNormal();
		const bool bIsForwardAxis = FMath::Abs(FVector::DotProduct(NormalizedInput, ControlForward))
			>= FMath::Abs(FVector::DotProduct(NormalizedInput, ControlRight));

		CurrentMountedMonster->AddMovementInput(bIsForwardAxis ? ControlForward : ControlRight, ScaleValue, bForce);
		return;
	}

	Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
}

float ABaseCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// See SetImmuneToMonsterAttacks()'s own comment — every damage source in this project that currently
	// reaches a player's TakeDamage() originates from a monster (ChaseAttackDamage, Naskok, PanicStrike,
	// Eye Peek hit, etc.); no unrelated damage source (e.g. fall damage) exists yet, so this is a flat
	// no-op rather than trying to filter by DamageCauser/EventInstigator.
	if (bImmuneToMonsterAttacks)
	{
		return 0.0f;
	}

	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	Health = FMath::Clamp(Health - ActualDamage, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(Health, MaxHealth);

	// No-op on ABaseCharacter itself; ARiderCharacter overrides OnKnockedOffMount() to actually dismount
	// and launch the rider — see that hook's own header comment.
	if (IsMounted())
	{
		OnKnockedOffMount(DamageCauser);
	}

	if (Health <= 0.0f)
	{
		HandleDeath();
	}

	return ActualDamage;
}

void ABaseCharacter::HandleDeath()
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;

	// Input: DisableInput() needs the OWNING PlayerController specifically (it no-ops silently on a null/
	// wrong controller), not just any controller — stops this character from processing further movement/
	// action input entirely.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}

	// Movement: stops CharacterMovementComponent from fighting the ragdoll's own physics simulation every
	// tick — left running, it would keep trying to walk/apply gravity through the capsule toward wherever
	// it last had input, fighting the mesh's now-independent physics simulation.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
	}

	// Capsule: NoCollision (not just ignoring one channel) so it stops blocking monsters/other actors as
	// an invisible obstacle at its last standing location once the mesh has physically fallen away from it.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Ragdoll: the engine's built-in "Ragdoll" collision profile (Block WorldStatic/Dynamic/PhysicsBody,
	// Overlap Pawn — the standard convention for a simulating character mesh) plus full-body physics
	// simulation. SetAllBodiesSimulatePhysics() (not just SetSimulatePhysics()) explicitly, since
	// SetSimulatePhysics() alone has historically been unreliable at reaching every body on a skeletal
	// mesh with a Physics Asset rather than just the root bone — belt-and-braces so the WHOLE body
	// ragdolls, not just the capsule-root bone. Deliberately does NOT hide/destroy the mesh — the whole
	// point of this function is that the ragdoll stays visible where it falls.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();
	}

	// Camera: left attached to the mesh/capsule, the boom would collapse straight down into the ragdoll
	// as it falls (and bDoCollisionTest would then yank it in further once the now-Ragdoll-profiled mesh
	// becomes a solid obstacle right next to it). Capture the boom's last live forward direction BEFORE
	// detaching, park it at a fixed world transform behind-and-above the death spot looking at it, and
	// turn off collision probing so it holds that framing instead of snapping/zooming against the floor
	// or the fallen body.
	if (USpringArmComponent* Boom = GetCameraBoomComponent())
	{
		const FVector DeathLocation = GetActorLocation();
		const FVector LastForward = Boom->GetForwardVector();

		FVector DeathCamLocation = DeathLocation - (LastForward * DeathCameraBackDistance);
		DeathCamLocation.Z += DeathCameraHeightOffset;

		Boom->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Boom->bDoCollisionTest = false;
		Boom->SetWorldLocation(DeathCamLocation);
		Boom->SetWorldRotation((DeathLocation - DeathCamLocation).Rotation());
		Boom->SetComponentTickEnabled(false);
	}

	if (UCameraComponent* Camera = GetFollowCamera())
	{
		Camera->SetComponentTickEnabled(false);
	}
}

void ABaseCharacter::CaughtInTrap(ABearTrap* Trap)
{
	if (bIsTrapped || !IsValid(Trap))
	{
		return;
	}

	bIsTrapped = true;
	TrapEscapePresses = 0;
	CurrentTrap = Trap;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		PreTrapMaxWalkSpeed = MoveComp->MaxWalkSpeed;
		MoveComp->MaxWalkSpeed = FMath::Min(MoveComp->MaxWalkSpeed, TrappedMaxWalkSpeed);
	}
}

void ABaseCharacter::MashTrapEscape()
{
	if (!bIsTrapped)
	{
		return;
	}

	// Swept, not a raw teleport — a wall or other blocking geometry right at the drag step's endpoint
	// should still stop the character normally instead of clipping through it.
	AddActorWorldOffset(GetActorForwardVector() * TrapDragStepDistance, /*bSweep=*/true);

	++TrapEscapePresses;
	if (TrapEscapePresses >= TrapEscapePressesRequired)
	{
		EscapeTrap();
	}
}

void ABaseCharacter::EscapeTrap()
{
	bIsTrapped = false;
	TrapEscapePresses = 0;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = PreTrapMaxWalkSpeed;
	}

	if (CurrentTrap.IsValid())
	{
		CurrentTrap->OnPlayerEscaped();
	}
	CurrentTrap.Reset();
}

void ABaseCharacter::ToggleSprint()
{
	if (bIsSprinting)
	{
		StopSprinting();
	}
	else
	{
		StartSprinting();
	}
}

void ABaseCharacter::ToggleCrouch()
{
	// IsCrouched() only actually flips once UCharacterMovementComponent processes bWantsToCrouch on the
	// next movement update (Crouch()/UnCrouch() just request it) — so this branches on the CURRENT value
	// to decide which to call, but logs the requested transition rather than re-reading IsCrouched()
	// immediately afterward, which would still report the stale pre-toggle value.
	if (IsCrouched())
	{
		UnCrouch();
		UE_LOG(LogTemp, Log, TEXT("%s::ToggleCrouch: UnCrouch() requested"), *GetName());
	}
	else
	{
		Crouch();
		UE_LOG(LogTemp, Log, TEXT("%s::ToggleCrouch: Crouch() requested"), *GetName());
	}
}

void ABaseCharacter::StartSprinting()
{
	// Blocked outright while crouched — no un-crouching, no queuing for later, just a no-op press.
	if (bIsSprinting || IsCrouched())
	{
		UE_LOG(LogTemp, Log, TEXT("%s::StartSprinting: blocked (bIsSprinting=%s IsCrouched=%s)"),
			*GetName(), bIsSprinting ? TEXT("true") : TEXT("false"), IsCrouched() ? TEXT("true") : TEXT("false"));
		return;
	}

	bIsSprinting = true;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = SprintSpeed;
	}

	UE_LOG(LogTemp, Log, TEXT("%s::StartSprinting: MaxWalkSpeed -> %.0f"), *GetName(), SprintSpeed);
}

void ABaseCharacter::StopSprinting()
{
	if (!bIsSprinting)
	{
		return;
	}

	bIsSprinting = false;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = DefaultWalkSpeed;
	}

	UE_LOG(LogTemp, Log, TEXT("%s::StopSprinting: MaxWalkSpeed -> %.0f"), *GetName(), DefaultWalkSpeed);
}

void ABaseCharacter::EnterPanicState(ABaseMonster* AttackingMonster)
{
	if (bIsPanicking || bIsDead || bIsTrapped || bImmuneToMonsterAttacks || !IsValid(AttackingMonster))
	{
		return;
	}

	bIsPanicking = true;
	bIsAwaitingGetUp = false; // in case a fresh hit lands before a previous episode's get-up was confirmed
	BlindEncounterMonster = AttackingMonster;
	PanicEpisodeId++;
	PanicEpisodeOriginator = AttackingMonster;
	AttackingMonster->ArmSelfPilingOnCooldown();
	LastPanicEpisodeStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	PanicMeter = InitialPanicMeter;
	bBeatResolvedThisWindow = false;
	LastBreathResultTime = -1.0f;

	// A brief backward stumble — "knocked to the ground" — same LaunchCharacter() mechanism
	// ABaseMonster::ApplyKnockback() uses on monsters, aimed away from the monster that caused it. Applied
	// BEFORE zeroing MaxWalkSpeed below, so the impulse isn't frozen mid-air — CharacterMovementComponent
	// still needs to process its own gravity/landing for this to actually play out and settle.
	const FVector KnockDirection = (GetActorLocation() - AttackingMonster->GetActorLocation()).GetSafeNormal2D();
	LaunchCharacter(KnockDirection * PanicKnockbackForce + FVector(0.0f, 0.0f, PanicKnockbackUpwardForce), true, true);

	PlayKnockdownAnimation();

	// Movement blocked (not full DisableInput()) — the Nose-Breathing mini-game needs the Space key
	// binding (OnBreathPressed()) to keep working the whole time.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		PrePanicMaxWalkSpeed = MoveComp->MaxWalkSpeed;
		MoveComp->MaxWalkSpeed = 0.0f;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: knocked down by %s — Nose-Breathing mini-game begins (PanicMeter=%.0f/%.0f)"),
		*GetName(), *AttackingMonster->GetName(), PanicMeter, PanicMeterMax);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(251, BreathRhythmInterval * 2.0f, FColor::Red, TEXT("KNOCKED DOWN — breathe through your NOSE (tap Space on the beat)!"));
	}

	const UWorld* World = GetWorld();
	NextBreathBeatTime = (World ? World->GetTimeSeconds() : 0.0f) + BreathRhythmInterval;
	GetWorldTimerManager().SetTimer(BreathBeatTimerHandle, this, &ABaseCharacter::OnBreathWindowClosed, BreathRhythmInterval + BreathToleranceWindow, false);
}

void ABaseCharacter::OnBreathPressed()
{
	if (!bIsPanicking || bBeatResolvedThisWindow)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : NextBreathBeatTime;
	const float Delta = FMath::Abs(CurrentTime - NextBreathBeatTime);

	if (Delta <= BreathToleranceWindow)
	{
		RegisterNoseBreath();
	}
	else
	{
		// Pressed too early/late for this beat's grace window — same outcome as never pressing at all
		// (OnBreathWindowClosed() would otherwise catch it), just resolved immediately instead of waiting
		// for the window to close on its own.
		RegisterMouthGasp();
	}
}

void ABaseCharacter::OnBreathWindowClosed()
{
	if (!bIsPanicking)
	{
		return;
	}
	if (!bBeatResolvedThisWindow)
	{
		RegisterMouthGasp();
	}
}

ABaseMonster* ABaseCharacter::FindNearbyMonster(float Radius) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> AllMonsters;
	UGameplayStatics::GetAllActorsOfClass(World, ABaseMonster::StaticClass(), AllMonsters);

	const float RadiusSq = FMath::Square(Radius);
	for (AActor* Actor : AllMonsters)
	{
		ABaseMonster* Monster = Cast<ABaseMonster>(Actor);
		if (!IsValid(Monster) || Monster->Health <= 0.0f)
		{
			continue;
		}
		if (FVector::DistSquared(Monster->GetActorLocation(), GetActorLocation()) <= RadiusSq)
		{
			return Monster;
		}
	}
	return nullptr;
}

float ABaseCharacter::GetBreathBeatProximity() const
{
	if (!bIsPanicking)
	{
		return 0.0f;
	}

	const UWorld* World = GetWorld();
	if (!World || BreathRhythmInterval <= 0.0f)
	{
		return 0.0f;
	}

	const float TimeToBeat = FMath::Abs(World->GetTimeSeconds() - NextBreathBeatTime);
	return FMath::Clamp(1.0f - (TimeToBeat / (BreathRhythmInterval * 0.5f)), 0.0f, 1.0f);
}

void ABaseCharacter::RegisterNoseBreath()
{
	bBeatResolvedThisWindow = true;
	PanicMeter = FMath::Max(0.0f, PanicMeter - PanicReliefPerBreath);

	bLastBreathWasHit = true;
	if (const UWorld* World = GetWorld())
	{
		LastBreathResultTime = World->GetTimeSeconds();
	}
	if (IsValid(NoseBreathSound))
	{
		UGameplayStatics::PlaySound2D(this, NoseBreathSound);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(252, BreathRhythmInterval, FColor::Green,
			FString::Printf(TEXT("Panic: %.0f/%.0f (nose — calming)"), PanicMeter, PanicMeterMax));
	}

	AdvanceToNextBeat();
}

void ABaseCharacter::RegisterMouthGasp()
{
	bBeatResolvedThisWindow = true;
	PanicMeter = FMath::Min(PanicMeterMax, PanicMeter + PanicPenaltyPerGasp);

	bLastBreathWasHit = false;
	if (const UWorld* World = GetWorld())
	{
		LastBreathResultTime = World->GetTimeSeconds();
	}
	if (IsValid(MouthGaspSound))
	{
		UGameplayStatics::PlaySound2D(this, MouthGaspSound);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(252, BreathRhythmInterval, FColor::Orange,
			FString::Printf(TEXT("Panic: %.0f/%.0f (MOUTH GASP)"), PanicMeter, PanicMeterMax));
	}

	// Loud enough to alert nearby monsters (same AlertRadius/ReceiveAlert() infrastructure as the original
	// blind touch) — but deliberately NOT an instant kill by itself anymore. A single missed beat just
	// raises PanicMeter; only reaching PanicMeterMax (see AdvanceToNextBeat() below) rolls the actual
	// proximity kill/survive check, in ForceEyesOpenFromPanic().
	if (BlindEncounterMonster.IsValid())
	{
		BlindEncounterMonster->BroadcastAlert(GetActorLocation());
	}

	AdvanceToNextBeat();
}

void ABaseCharacter::AdvanceToNextBeat()
{
	NextBreathBeatTime += BreathRhythmInterval;
	bBeatResolvedThisWindow = false;

	if (PanicMeter <= 0.0f)
	{
		EndPanicSafely();
		return;
	}
	if (PanicMeter >= PanicMeterMax)
	{
		ForceEyesOpenFromPanic();
		return;
	}

	const UWorld* World = GetWorld();
	const float Delay = (NextBreathBeatTime + BreathToleranceWindow) - (World ? World->GetTimeSeconds() : 0.0f);
	GetWorldTimerManager().SetTimer(BreathBeatTimerHandle, this, &ABaseCharacter::OnBreathWindowClosed, FMath::Max(0.01f, Delay), false);
}

void ABaseCharacter::EndPanicSafely()
{
	bIsPanicking = false;
	GetWorldTimerManager().ClearTimer(BreathBeatTimerHandle);

	// MaxWalkSpeed is deliberately NOT restored here — RecoverFromKnockdown() (below) defers that until the
	// Get Up animation actually finishes, so movement control comes back only once the character is
	// visibly back on its feet.

	if (BlindEncounterMonster.IsValid())
	{
		BlindEncounterMonster->OnBlindEncounterResolved();
	}
	BlindEncounterMonster.Reset();

	UE_LOG(LogTemp, Warning, TEXT("%s: fully calmed down — waiting for a forward press to get up"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(251, 2.0f, FColor::Green, TEXT("Calm — press forward to get up"));
	}

	// RecoverFromKnockdown() no longer fires automatically — see bIsAwaitingGetUp's own comment. Move()
	// calls it once a forward press actually lands.
	bIsAwaitingGetUp = true;
}

void ABaseCharacter::PlayKnockdownAnimation()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (IsValid(KnockdownFallAnimation) && IsValid(MeshComp))
	{
		MeshComp->PlayAnimation(KnockdownFallAnimation, false);
	}
}

void ABaseCharacter::RestoreAnimationBlueprintControl()
{
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	}
}

void ABaseCharacter::RecoverFromKnockdown()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (IsValid(GetUpAnimation) && IsValid(MeshComp))
	{
		MeshComp->PlayAnimation(GetUpAnimation, false);
		GetWorldTimerManager().SetTimer(GetUpTimerHandle, this, &ABaseCharacter::FinishRecoveryFromKnockdown, GetUpAnimation->GetPlayLength(), false);
	}
	else
	{
		// No Get Up clip assigned — nothing to wait for, snap straight back to standing.
		FinishRecoveryFromKnockdown();
	}
}

void ABaseCharacter::FinishRecoveryFromKnockdown()
{
	RestoreAnimationBlueprintControl();

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = PrePanicMaxWalkSpeed;
	}
}

void ABaseCharacter::ForceEyesOpenFromPanic()
{
	GetWorldTimerManager().ClearTimer(BreathBeatTimerHandle);

	// Involuntary — sets the eye-closed state/camera fade directly, deliberately NOT through OpenEyes()
	// itself (that has its own unconditional instant-kill check meant for a deliberate, voluntary open;
	// this gets its own proximity-based roll below instead).
	bIsEyesClosed = false;
	OnEyesClosedChanged(false);

	ABaseMonster* NearbyMonster = FindNearbyMonster(MonsterProximityKillRadius);
	// Captured before BlindEncounterMonster.Reset() below — the survival branch still needs to notify
	// whichever monster actually started this encounter (almost always still bIsScanning, per
	// ResolveBlindEncounterReaction()'s comment on suppressing its own timeout during Scenario B), since
	// nothing else does that cleanup here otherwise.
	ABaseMonster* EncounterMonster = BlindEncounterMonster.Get();

	bIsPanicking = false;
	BlindEncounterMonster.Reset();
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = PrePanicMaxWalkSpeed;
	}

	if (NearbyMonster)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: panic maxed out, eyes forced open with %s right there — killed"), *GetName(), *NearbyMonster->GetName());
		NearbyMonster->ResolveBlindEncounterKill(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: panic maxed out, eyes forced open — but everyone's gone. Lucky survival."), *GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(251, 2.0f, FColor::Yellow, TEXT("Eyes snapped open in terror... but nothing's there. Lucky."));
		}

		// Also a non-death exit from a knockdown (PlayKnockdownAnimation() switched the mesh to
		// AnimationSingleNode back in EnterPanicState()) — without this, the character would stay frozen in
		// the fallen pose forever despite MaxWalkSpeed already having been restored above. MaxWalkSpeed
		// restoration itself isn't deferred here the way RecoverFromKnockdown() defers it for
		// EndPanicSafely() — this is a startled, involuntary snap back up, not a controlled recovery.
		RestoreAnimationBlueprintControl();

		// EncounterMonster is still bIsScanning/bHasActiveBlindEncounter from OnCapsuleHit() — without
		// this, a monster whose target survives this way would be stuck mid-Scan forever, unable to ever
		// react to a fresh blind touch with anyone again.
		if (IsValid(EncounterMonster))
		{
			EncounterMonster->OnBlindEncounterResolved();
		}
	}
}

void ABaseCharacter::UpdateAimCrosshair() const
{
	if (!IsValid(CrosshairWidgetInstance))
	{
		return;
	}

	FVector TargetPoint;
	FHitResult AimHit;
	const bool bHitSomething = TraceAimTarget(TargetPoint, AimHit);

	// Standing state banner per this project's on-screen-debug convention — cheap, single-glance
	// confirmation of where a throw would actually land.
	const float DistanceToTarget = FVector::Dist(GetActorLocation(), TargetPoint);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(100, 0.0f, FColor::Green,
			FString::Printf(TEXT("AIMING ACTIVE - Crosshair Target: %s (%.0f units)"), *TargetPoint.ToCompactString(), DistanceToTarget));
	}

	// Classify what the trace hit, in priority order: a mountable ABaseMonster reads as GREEN (this
	// project's only "enemy"/rideable-creature class — Cast already matches any subclass of it, same
	// as GetClass()->IsChildOf() but yields a usable typed pointer directly, so no further tag/interface
	// check is needed to mean "any such creature"); an AHolyItem lying in the world, or anything tagged
	// "Target" (covers a Blueprint-only target dummy with no dedicated C++ class, e.g. BP_WobbleTarget —
	// add this tag to such a Blueprint's Class Defaults to have it register here), reads as RED; anything
	// else (world geometry, sky/no hit) keeps the neutral default.
	FLinearColor DotColor = CrosshairDefaultColor;
	if (bHitSomething && IsValid(AimHit.GetActor()))
	{
		if (IsValid(Cast<ABaseMonster>(AimHit.GetActor())))
		{
			DotColor = CrosshairMountableColor;
		}
		else if (IsValid(Cast<AHolyItem>(AimHit.GetActor())) || AimHit.GetActor()->ActorHasTag(TEXT("Target")))
		{
			DotColor = CrosshairTargetColor;
		}
	}

	// Visibility itself is handled centrally by Tick() right after this call (it depends on both
	// bIsAiming AND whether there's a prompt to show — see its own comment), not here.
	CrosshairWidgetInstance->DotColor = DotColor;
}

void ABaseCharacter::SpawnAimTargetMarker()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector TargetPoint;
	FHitResult AimHit;
	TraceAimTarget(TargetPoint, AimHit);

	// AlwaysSpawn: this is a debug placement at an arbitrary world point, which may well be inside
	// geometry (a wall/floor hit) — that's fine and expected, don't let collision handling silently
	// adjust or refuse the spawn location, since the whole point is to see exactly where TargetPoint
	// really is, not a nearby "safe" substitute.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Marker = World->SpawnActor<AStaticMeshActor>(TargetPoint, FRotator::ZeroRotator, SpawnParams);
	if (IsValid(Marker) && IsValid(Marker->GetStaticMeshComponent()))
	{
		Marker->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
		{
			Marker->GetStaticMeshComponent()->SetStaticMesh(SphereMesh);
		}
		Marker->SetActorScale3D(FVector(3.0f));
	}

	// This is the actual proof: a REAL actor's GetActorLocation(), read back immediately after
	// spawning it AT TargetPoint — a completely different code path from DrawDebug*/the line batcher.
	// If this ever disagrees with TargetPoint, or if the marker doesn't appear where its own logged
	// location says it should, that's an engine/spawn-level finding neither of us has hypothesized yet.
	UE_LOG(LogTemp, Error, TEXT("SpawnAimTargetMarker: TraceAimTarget()=%s -> spawned actor at %s"),
		*TargetPoint.ToString(), IsValid(Marker) ? *Marker->GetActorLocation().ToString() : TEXT("SPAWN FAILED"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(150, 15.0f, FColor::Magenta,
			FString::Printf(TEXT("Marker spawned at %s — find/select it in the World Outliner (search \"StaticMeshActor\") and check its Transform"),
				*TargetPoint.ToCompactString()));
	}
}
