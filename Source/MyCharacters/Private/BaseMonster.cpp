// Fill out your copyright notice in the Description page of Project Settings.

#include "BaseMonster.h"
#include "BaseCharacter.h"
#include "RiderCharacter.h"
#include "MountRiderComponent.h"
#include "Components/MonsterAudioComponent.h"
#include "HolyItem.h"
#include "DayNightManager.h"
#include "Engine/DirectionalLight.h"
#include "HolyDamageType.h"
#include "CenserDamageType.h"
#include "CandleDamageType.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "CollisionShape.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "EngineUtils.h"

ABaseMonster::ABaseMonster()
{
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Root cause of ABaseCharacter::TraceAimTarget()'s aim trace (ECC_Visibility) passing straight
	// through monsters: every ACharacter's capsule uses the engine's default "Pawn" collision profile,
	// which sets Visibility to Ignore (see Engine/Config/BaseEngine.ini's Pawn profile) — this is
	// deliberate engine-wide default behavior (so pawns don't block camera/visibility traces by
	// default), not something specific to this project, but it means a monster's capsule is invisible
	// to any ECC_Visibility trace until explicitly told otherwise. Overriding just this one channel
	// response is enough; the aim trace only needs to hit the capsule, not the per-poly skeletal mesh
	// (which keeps ignoring Visibility via the "CharacterMesh" profile, same as every other Character).
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Whisper monster model — replaces the mesh (previously entirely unset on this class) with the real
	// asset, and derives capsule size + the mesh's own relative offset from its ACTUAL bounds rather than
	// a flat guess (same "compute from real dimensions" reasoning as UMountRiderComponent::
	// MountRiderHeightBuffer/AHolyItem's candle prototype earlier this project). Reference-pose (T-pose)
	// bounds are reliable enough for HEIGHT/bottom placement — a T-pose doesn't distort a character's
	// height — but NOT for capsule RADIUS: a T-pose's arms-out span is much wider than the actual body
	// silhouette a gameplay capsule should represent, so radius is instead a fixed fraction of the derived
	// half-height, matching ACharacter's own default proportions (34 radius / 88 half-height ≈ 0.39) —
	// rather than trusting BoxExtent.X/Y directly, which would produce an absurdly wide capsule.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> WhisperMeshFinder(TEXT("/Game/Whisper/Base_Mesh/SK_Whisper.SK_Whisper"));
	if (WhisperMeshFinder.Succeeded() && IsValid(GetMesh()))
	{
		USkeletalMesh* WhisperMesh = WhisperMeshFinder.Object;
		GetMesh()->SetSkeletalMesh(WhisperMesh);

		const FBoxSphereBounds MeshBounds = WhisperMesh->GetBounds();
		const float NewCapsuleHalfHeight = FMath::Max(MeshBounds.BoxExtent.Z, 10.0f);
		const float MeshBottomZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;

		constexpr float StandardRadiusToHalfHeightRatio = 34.0f / 88.0f;
		const float NewCapsuleRadius = FMath::Max(NewCapsuleHalfHeight * StandardRadiusToHalfHeightRatio, 15.0f);
		GetCapsuleComponent()->InitCapsuleSize(NewCapsuleRadius, NewCapsuleHalfHeight);

		// Shifts the mesh so its own lowest point (whatever local Z that actually is for THIS mesh — not
		// assumed to be exactly 0) lands exactly at the capsule's bottom, i.e. flush on the ground
		// regardless of how SK_Whisper's own pivot was authored.
		GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -NewCapsuleHalfHeight - MeshBottomZ));

		// Standard Mannequin-style mesh-to-capsule yaw offset (matches ABaseCharacter's own identical -90
		// degree fix elsewhere in this project) — a reasonable starting point, NOT verified against
		// Whisper's own specific forward-facing convention (no visual inspection tool available to
		// confirm it). Re-check in PIE and adjust if Whisper turns out to walk sideways/backwards relative
		// to its actual movement direction.
		GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	}

	// See AttackAnimation's own header comment.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AttackAnimFinder(TEXT("/Game/Whisper/Animations/Anim_Whisper_Attack1.Anim_Whisper_Attack1"));
	if (AttackAnimFinder.Succeeded())
	{
		AttackAnimation = AttackAnimFinder.Object;
	}

	// VALIDATION: Anim_Whisper_Attack1 had never been used anywhere in this project before this attack
	// system was built, so its actual validity/skeleton compatibility had never been exercised either.
	// A skeleton MISMATCH is a completely silent failure elsewhere — PlaySlotAnimationAsDynamicMontage()
	// "succeeds" (returns a non-null UAnimMontage*) with no visible effect and no error/warning of its
	// own — so this is checked explicitly, once, right here at load time.
	if (!IsValid(AttackAnimation))
	{
		UE_LOG(LogTemp, Error, TEXT("ABaseMonster: AttackAnimation FAILED TO LOAD from /Game/Whisper/Animations/Anim_Whisper_Attack1 — the attack swing will never play."));
	}
	else
	{
		const USkeleton* AttackAnimSkeleton = AttackAnimation->GetSkeleton();
		UE_LOG(LogTemp, Warning, TEXT("ABaseMonster: AttackAnimation '%s' loaded. GetSkeleton() valid: %s (%s)"),
			*AttackAnimation->GetName(),
			IsValid(AttackAnimSkeleton) ? TEXT("YES") : TEXT("NO"),
			IsValid(AttackAnimSkeleton) ? *AttackAnimSkeleton->GetName() : TEXT("none"));

		// GetMesh()->SetSkeletalMesh(WhisperMesh) already ran above (this block only re-checks it, never
		// sets it), so this comparison is meaningful here despite still being inside the constructor.
		const USkeletalMesh* MonsterMeshAsset = IsValid(GetMesh()) ? GetMesh()->GetSkeletalMeshAsset() : nullptr;
		if (IsValid(MonsterMeshAsset))
		{
			const USkeleton* MonsterMeshSkeleton = MonsterMeshAsset->GetSkeleton();
			const bool bSkeletonsMatch = IsValid(AttackAnimSkeleton) && IsValid(MonsterMeshSkeleton) && AttackAnimSkeleton == MonsterMeshSkeleton;
			UE_LOG(LogTemp, Warning, TEXT("ABaseMonster: GetMesh() skeleton: %s | Matches AttackAnimation's skeleton: %s"),
				IsValid(MonsterMeshSkeleton) ? *MonsterMeshSkeleton->GetName() : TEXT("none"),
				bSkeletonsMatch ? TEXT("YES") : TEXT("NO — attack swing WILL NOT ANIMATE on this mesh"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ABaseMonster: GetMesh() has no SkeletalMeshAsset yet — cannot compare skeletons at construction time."));
		}
	}

	// BUG FIX: this was pointing at "/Game/Whisper/ABP_Whisper", but the actual asset lives at
	// "/Game/Whisper/Base_Mesh/ABP_Whisper" — FClassFinder silently fails on a wrong path (logs
	// "CDO Constructor (BaseMonster): Failed to find ..." at startup, easy to miss), which meant
	// GetMesh()->SetAnimInstanceClass() below was NEVER actually called: every ABaseMonster ran with NO
	// Animation Blueprint assigned in C++ at all (unless a Blueprint subclass happened to set one itself
	// in its own Class Defaults). This is almost certainly the real root cause behind "the attack
	// animation never seems to play" — PlaySlotAnimationAsDynamicMontage() has nothing to blend into
	// without an AnimBP graph containing the DefaultSlot node AttackAnimSlotName targets.
	static ConstructorHelpers::FClassFinder<UAnimInstance> WhisperAnimBPFinder(TEXT("/Game/Whisper/Base_Mesh/ABP_Whisper"));
	if (WhisperAnimBPFinder.Succeeded() && IsValid(GetMesh()))
	{
		GetMesh()->SetAnimInstanceClass(WhisperAnimBPFinder.Class);
	}

	MonsterAudioComponent = CreateDefaultSubobject<UMonsterAudioComponent>(TEXT("MonsterAudioComponent"));
}

void ABaseMonster::BeginPlay()
{
	Super::BeginPlay();

	GetWorldTimerManager().SetTimer(SightCheckTimerHandle, this, &ABaseMonster::CheckForPlayer, 0.1f, true);

	GetCharacterMovement()->MaxWalkSpeed = PatrolSpeed;

	// Guarantee AI possession: AutoPossessAI (set in the constructor) should already have handled
	// this via PostInitializeComponents, but a monster whose possession timing raced BeginPlay for
	// any reason can still reach here with no Controller at all — and nothing below (MoveToActor/
	// MoveToLocation, every AAIController cast in this class) can ever move a Pawn with no
	// Controller possessing it, which reads from the outside as a monster frozen in place forever.
	// Force it explicitly rather than silently idling.
	AAIController* AICon = Cast<AAIController>(GetController());
	if (!AICon)
	{
		UE_LOG(LogTemp, Error, TEXT("%s::BeginPlay: no Controller yet — forcing SpawnDefaultController()"), *GetName());
		SpawnDefaultController();
		AICon = Cast<AAIController>(GetController());
		if (!AICon)
		{
			UE_LOG(LogTemp, Error, TEXT("%s::BeginPlay: still no AIController after SpawnDefaultController() — this monster cannot move!"), *GetName());
		}
	}

	// Kickstart movement for a fresh spawn, forced unconditionally now that possession is
	// guaranteed above — an unplaced-patrol (bUseRandomWander, or empty PatrolPoints) monster
	// otherwise has nothing else to ever start it moving and would idle forever.
	if (State == EMonsterState::Patrol)
	{
		MoveToNextPatrolPoint();
	}

	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	// Every monster instance must be equally detectable by UMountRiderComponent's (ARiderCharacter's
	// mounting component) crosshair/ECC_Pawn checks, regardless of whatever Collision Preset a specific
	// Blueprint subclass/per-instance override was left at in the level — re-asserted HERE (not just the
	// constructor's SetCollisionResponseToChannel(ECC_Visibility, ...)) so it applies AFTER any Blueprint
	// Class Defaults/per-instance edits have already been applied, since those can otherwise silently
	// leave one monster's capsule not blocking Pawn (Ignore/Overlap) while another's still does — exactly
	// what made "some monsters can't be mounted at all" possible despite every monster sharing this same
	// C++ base.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Capsule->OnComponentHit.AddDynamic(this, &ABaseMonster::OnCapsuleHit);
	}

	// Uniform, Blueprint-independent tag so mount detection (and anything else that needs to identify
	// "this actor is a mountable monster") has a stable signal to check on top of the Cast<ABaseMonster>
	// UMountRiderComponent already does — every ABaseMonster gets this regardless of subclass/instance.
	Tags.AddUnique(TEXT("Mountable"));

	// Exactly one ADayNightManager is expected per level (see its own header comment) — GetActorOfClass()
	// returns the first one found, nullptr if none exists, in which case every sun-exposure feature below
	// stays a harmless no-op for this monster's whole lifetime.
	DayNightManager = Cast<ADayNightManager>(UGameplayStatics::GetActorOfClass(GetWorld(), ADayNightManager::StaticClass()));
	if (IsValid(DayNightManager))
	{
		DayNightManager->OnDawnStarted.AddDynamic(this, &ABaseMonster::HandleDawnStarted);
		DayNightManager->OnDayStarted.AddDynamic(this, &ABaseMonster::HandleDayStarted);
		DayNightManager->OnNightStarted.AddDynamic(this, &ABaseMonster::HandleNightStarted);

		// Seeds this monster's sun-exposure state to match whatever phase the world is ALREADY in at
		// spawn time, rather than leaving it in the Night/SafeInShadow defaults until the next actual
		// transition fires — a monster that spawns (or is placed in a level that starts) mid-Day should
		// start checking sun exposure immediately, not wait until the following Dawn->Day cycle.
		switch (DayNightManager->SunPhase)
		{
		case ESunPhase::Dawn:
			HandleDawnStarted();
			break;
		case ESunPhase::Day:
			HandleDayStarted();
			break;
		case ESunPhase::Night:
		default:
			HandleNightStarted();
			break;
		}

		UE_LOG(LogTemp, Warning, TEXT("%s::BeginPlay: bound to DayNightManager %s (starting phase %d)"),
			*GetName(), *DayNightManager->GetName(), static_cast<int32>(DayNightManager->SunPhase));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::BeginPlay: no ADayNightManager found in the level — sun exposure disabled"), *GetName());
	}
}

void ABaseMonster::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// See this override's own header comment — every FTimerHandle this class ever arms, cleared
	// unconditionally so none of them can fire against this actor's memory after it's gone (Destroy(),
	// level reload/reopen, PIE ending, etc.). ClearTimer() is itself safe to call on an already-expired/
	// never-armed handle, so no per-handle "is this even running" check is needed first.
	FTimerManager& TimerManager = GetWorldTimerManager();
	TimerManager.ClearTimer(AttackKillTimerHandle);
	TimerManager.ClearTimer(SightCheckTimerHandle);
	TimerManager.ClearTimer(WaitTimerHandle);
	TimerManager.ClearTimer(FlankRescanTimerHandle);
	TimerManager.ClearTimer(DestroyTimerHandle);
	TimerManager.ClearTimer(StunTimerHandle);
	TimerManager.ClearTimer(StaggerTimerHandle);
	TimerManager.ClearTimer(SunCheckTimerHandle);
	TimerManager.ClearTimer(BlindReactionTimerHandle);
	TimerManager.ClearTimer(ScanTimerHandle);
	TimerManager.ClearTimer(InvestigateTimerHandle);
	TimerManager.ClearTimer(InvestigateArrivalCheckTimerHandle);
	TimerManager.ClearTimer(PanicStrikeRearmTimerHandle);
	TimerManager.ClearTimer(EyePeekReactionTimerHandle);
	TimerManager.ClearTimer(EyePeekAttackDebugTimerHandle);
	TimerManager.ClearTimer(EyePeekAttackHitFallbackTimerHandle);
	TimerManager.ClearTimer(ProbeStartleTimerHandle);

	// Same reasoning, different mechanism: BeginPlay() above binds this monster to DayNightManager's own
	// multicast delegates, which would otherwise keep broadcasting into this actor's memory after
	// teardown exactly like a stray timer would (DayNightManager itself can easily outlive an individual
	// monster — it's one-per-level, not owned by any single monster).
	if (IsValid(DayNightManager))
	{
		DayNightManager->OnDawnStarted.RemoveDynamic(this, &ABaseMonster::HandleDawnStarted);
		DayNightManager->OnDayStarted.RemoveDynamic(this, &ABaseMonster::HandleDayStarted);
		DayNightManager->OnNightStarted.RemoveDynamic(this, &ABaseMonster::HandleNightStarted);
	}

	Super::EndPlay(EndPlayReason);
}

void ABaseMonster::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsValid(this) || IsPendingKillPending() || Health <= 0.0f)
	{
		return;
	}

	// DISABLED per "temporarily silence the green on-screen movement-diagnostics spam" request — this
	// used to fire every Tick() (0.0f lifetime = refreshed continuously) for every monster in the level
	// the instant bDebugSunTrace defaulted true, cluttering the screen with
	// "[Monster] Has Controller: ... | Has NavMesh: ... | MoveStatus: ... | Pos: ..." lines that had
	// nothing to do with whatever's actually being tested right now. Uncomment (and/or flip
	// bDebugSunTrace back to true) to bring this back for an actual sun/movement debugging session.
	// if (bDebugSunTrace && GEngine)
	// {
	// 	const AAIController* DebugAICon = Cast<AAIController>(GetController());
	// 	const UNavigationSystemV1* DebugNavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	//
	// 	const TCHAR* MoveStatusText = TEXT("NoController");
	// 	if (DebugAICon)
	// 	{
	// 		switch (DebugAICon->GetMoveStatus())
	// 		{
	// 		case EPathFollowingStatus::Idle:    MoveStatusText = TEXT("Idle"); break;
	// 		case EPathFollowingStatus::Waiting: MoveStatusText = TEXT("Waiting"); break;
	// 		case EPathFollowingStatus::Paused:  MoveStatusText = TEXT("Paused"); break;
	// 		case EPathFollowingStatus::Moving:  MoveStatusText = TEXT("Moving"); break;
	// 		default: MoveStatusText = TEXT("Unknown"); break;
	// 		}
	// 	}
	//
	// 	GEngine->AddOnScreenDebugMessage(static_cast<int32>(GetUniqueID()) + 1000, 0.0f, FColor::Emerald,
	// 		FString::Printf(TEXT("[%s] Has Controller: %s | Has NavMesh: %s | MoveStatus: %s | Pos: %s"),
	// 			*GetName(), DebugAICon ? TEXT("YES") : TEXT("NO"), DebugNavSys ? TEXT("YES") : TEXT("NO"),
	// 			MoveStatusText, *GetActorLocation().ToCompactString()));
	// }

	// TEMPORARY diagnostic — a floating status marker directly above this monster's head, refreshed
	// every Tick() (not just every ~0.1s CheckForPlayer() poll) so it stays pinned exactly over the mesh
	// while it moves, including during the whole "closing in" chase. Passing `this` as DrawDebugString()'s
	// TestBaseActor parameter is what makes the TextLocation offset track this actor's CURRENT transform
	// automatically, rather than freezing at wherever GetActorLocation() happened to be this one frame.
	// Nothing drawn in the ordinary Patrol/Stalk/Chase case — see bShowEyePeekDebug's own comment.
	if (bShowEyePeekDebug)
	{
		static const FVector HeadOffset(0.0f, 0.0f, 150.0f);
		if (bIsPlayingEyePeekAttackAnim)
		{
			// Bright red — the swing (Anim_Whisper_Attack1) is playing right now.
			DrawDebugString(GetWorld(), HeadOffset, TEXT("УДАР!"), this, FColor::Red, 0.0f, true, 1.5f);
		}
		else if (bHasActiveEyePeekEncounter)
		{
			// Bright magenta/pink — spotted a peeking player and is closing in (TryTriggerEyePeekEncounter()
			// through ResolveEyePeekReaction()'s reaction-delay windup).
			DrawDebugString(GetWorld(), HeadOffset, TEXT("НАШЁЛ! ИДУ АТАКОВАТЬ"), this, FColor::Magenta, 0.0f, true, 1.2f);
		}
	}

	if (bIsBeingRidden)
	{
		// Hard, every-tick override — belt-and-braces against whatever is still causing the reported
		// spin, even though OnMounted() already calls StopMovement()/ClearFocus() once at mount time. If
		// ANYTHING (a stray timer-driven MoveToLocation/MoveToActor call elsewhere, a Blueprint event, a
		// re-triggered Chase/Stalk decision this Tick() gate hasn't reached yet) issues a fresh AI move
		// request while ridden, this cancels it the very same frame instead of letting it run for even
		// one tick.
		if (AAIController* AICon = Cast<AAIController>(GetController()))
		{
			AICon->StopMovement();
		}

		// Root motion is a THIRD, entirely separate rotation source neither bOrientRotationToMovement
		// nor bUseControllerDesiredRotation gate at all — an Animation Blueprint montage/state with baked
		// root motion (e.g. a turn-in-place, idle fidget, or attack animation) applies its own Yaw
		// directly to the actor every frame it plays, completely bypassing CharacterMovementComponent's
		// rotation logic. If this monster's AnimBP has anything like that still playing while ridden,
		// forcing IgnoreRootMotion here stops it from fighting the rider's movement-driven rotation.
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
			{
				AnimInst->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
			}
		}

		return;
	}

	if (bIsStunned || bIsStaggered || bIsScanning || bIsInvestigatingAlert)
	{
		return;
	}

	if (bIsFleeingToShadow)
	{
		if (!bIsIdleInShadow)
		{
			// Still actively moving toward cover (or CheckSunExposure()'s retry logic just hasn't caught
			// up yet) — CheckSunExposure()'s own timer drives that, not Tick(), so there's nothing for
			// this to do until it settles into bIsIdleInShadow.
			return;
		}

		// Shadow-restricted wander: allowed to move AROUND within its shadow cover once safely idle
		// during Dawn/Day, instead of freezing indefinitely until Night — reuses the exact same
		// wait/arrival polling pattern as normal Patrol wander below (bIsWaiting / WaitTimerHandle /
		// AIController move status), but always through MoveToRandomWanderPoint()'s shadow-restricted
		// sampling directly (see its own comment), NOT MoveToNextPatrolPoint() — a monster configured
		// with fixed PatrolPoints has no guarantee any of them sit in shadow, so Day/Dawn shadow-wander
		// always uses random NavMesh sampling regardless of bUseRandomWander/PatrolPoints.
		if (bIsWaiting)
		{
			return;
		}

		AAIController* ShadowAICon = Cast<AAIController>(GetController());
		if (!ShadowAICon)
		{
			return;
		}

		if (ShadowAICon->GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			bIsWaiting = true;
			const float WaitTime = FMath::FRandRange(WanderWaitTimeMin, WanderWaitTimeMax);

			GetWorldTimerManager().SetTimer(WaitTimerHandle, [this]()
			{
				bIsWaiting = false;
				// Re-checked at fire time (not just when the timer was armed) — Night could have broken
				// out from under this monster while it was waiting, in which case HandleNightStarted()
				// already issued its own kickstart move and this should stay out of the way.
				if (bIsFleeingToShadow && bIsIdleInShadow)
				{
					MoveToRandomWanderPoint();
				}
			}, WaitTime, false);
		}
		return;
	}

	if (State == EMonsterState::Patrol)
	{
		if (bIsWaiting)
		{
			return;
		}

		// bUseRandomWander OR an empty PatrolPoints array (see that property's own comment on why the
		// latter is unconditional) — whichever fixed-point-vs-wander branch this reached point actually
		// came from, since MoveToNextPatrolPoint() decides the SAME way every time it's called.
		const bool bIsWandering = bUseRandomWander || PatrolPoints.Num() == 0;

		AAIController* AICon = Cast<AAIController>(GetController());
		if (!AICon)
		{
			return;
		}

		// Path following reports Moving while actively following a path; anything else
		// (Idle, Waiting, Paused) means the current MoveToActor/MoveToLocation request has finished.
		if (AICon->GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			bIsWaiting = true;

			// Random wander re-rolls a fresh wait duration in [WanderWaitTimeMin, WanderWaitTimeMax] on
			// every arrival, rather than reusing PatrolWaitTime's single fixed value — see
			// WanderWaitTimeMin/Max's own comment.
			const float WaitTime = bIsWandering ? FMath::FRandRange(WanderWaitTimeMin, WanderWaitTimeMax) : PatrolWaitTime;

			UE_LOG(LogTemp, Warning, TEXT("BaseMonster: reached %s point! Waiting %.1f sec..."),
				bIsWandering ? TEXT("wander") : TEXT("patrol"), WaitTime);

			GetWorldTimerManager().SetTimer(WaitTimerHandle, [this, bIsWandering]()
			{
				bIsWaiting = false;
				// CurrentPatrolIndex only means anything for the fixed PatrolPoints path — wandering picks
				// a fresh random point each time regardless, with no index to advance.
				if (!bIsWandering)
				{
					CurrentPatrolIndex = (CurrentPatrolIndex + 1) % PatrolPoints.Num();
				}
				MoveToNextPatrolPoint();
			}, WaitTime, false);
		}
		return;
	}

	// Stalk needs no Tick()-driven polling: MoveToLightBorder() re-arms FlankRescanTimerHandle itself
	// every FlankRescanIntervalMin-Max seconds for as long as State stays Stalk, independent of whether
	// the current move has been reached yet — see its comment for why that matters.
}

void ABaseMonster::CheckForPlayer()
{
	if (!IsValid(this) || IsPendingKillPending() || Health <= 0.0f)
	{
		return;
	}

	// Hard guard, checked before EVERYTHING else in this function — including the aura-proximity backup
	// below — so this monster cannot agro/stalk/chase the player under any circumstance while STILL
	// ACTIVELY FLEEING to shadow; escaping the sun takes absolute priority over combat while genuinely
	// mid-flight. Deliberate trade-off, not an oversight: a fleeing monster that brushes past the
	// player's held item no longer takes the usual aura pushback/contact damage from it either, since
	// CheckAuraProximity() below never runs while this returns early — acceptable since this monster is
	// already actively running away regardless. Once it's SAFELY ARRIVED in shadow (bIsIdleInShadow),
	// combat resumes below — but shadow-restricted (see bPursuitWouldLeaveShadow) so it never chases the
	// player back out into the sun that just sent it running.
	if (bIsFleeingToShadow && !bIsIdleInShadow)
	{
		return;
	}

	ABaseCharacter* Player = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));

	// Backup for HolyAuraSphere's physics-driven overlap: an item attached to and riding an animated
	// hand-socket bone doesn't reliably re-evaluate overlaps every frame the way a static/physically
	// simulating item on the ground does, so explicitly check proximity to whatever the player is
	// holding too. Runs even while stunned/eyes-closed/out of sight — the aura is a physical effect,
	// not a visual one.
	if (IsValid(Player))
	{
		if (IsValid(Player->EquippedRightHand))
		{
			Player->EquippedRightHand->CheckAuraProximity(this);
		}
		if (IsValid(Player->EquippedLeftHand))
		{
			Player->EquippedLeftHand->CheckAuraProximity(this);
		}
	}

	// PendingChaseAttackTarget.IsValid()/PendingNaskokAttackTarget.IsValid(): a swing from either
	// background attack source is already in flight — see their own header comments. Without this,
	// CheckForPlayer()'s own ~0.1s poll would re-enter the CatchDistance branch below and restart a swing
	// before the first one ever resolves (StartChasing()/StopMovement() alone don't stop this function
	// from running its own detection logic again next tick the way bIsScanning/bIsStunned already do for
	// other states).
	//
	// this == Player->PanicEpisodeOriginator.Get() && Player->bIsPanicking && still within its own
	// PanicStrikeEligibleTime cooldown: the monster that actually caused THIS panic episode already
	// delivered its own hit via TryStartChaseAttack()/TryStartNaskokAttack()/Eye Peek — nothing marks it
	// "busy" the way a MISS's EnterScanningState() (bIsScanning) does after a hit, and
	// PendingChaseAttackTarget/PendingNaskokAttackTarget are both already Reset() by the time
	// OnAttackHitNotify() finishes resolving them — so without this, THIS SAME monster's very next ~0.1s
	// poll would fall straight through bHasDetectedPlayer (still true — nothing about that check cares
	// about panic) into CatchDistance and call TryStartChaseAttack() again, restarting
	// PlayAttackAnimation() from frame 0 (the original regression this exact guard was first added to
	// fix). Bounded by PanicStrikeEligibleTime (not a blanket "for the whole episode" block) so this
	// monster ALSO becomes free — same as any other "piling on" monster — to fall through into the
	// standalone "piling on" check just below once its own cooldown elapses, even while the player is
	// STILL panicking: without the time bound, the initiator could never rejoin its own episode's
	// "piling on" wave at all (see ArmSelfPilingOnCooldown()'s own comment), defeating this whole
	// mechanism. Once bIsPanicking itself clears (panic resolved, independent of the cooldown timer) this
	// clause stops applying immediately regardless of the cooldown — what scenario (g)'s own "resume the
	// instant panic ends" regression coverage depends on. Scoped to ONLY this specific monster (not a
	// blanket Player->bIsPanicking return) so every OTHER monster is still free to reach the standalone
	// "piling on" check just below.
	// PendingPanicStrikeTarget.IsValid() is the same re-entrancy reasoning as PendingChaseAttackTarget/
	// PendingNaskokAttackTarget above, for that "piling on" swing specifically. PanicStrikeRearmTimerHandle
	// covers the GAP right after that: OnAttackHitNotify()'s own HIT branch already Reset()s
	// PendingPanicStrikeTarget before arming this timer (see its own comment) to delay the next
	// TryStartPanicStrike() by PanicStrikeRearmDelay — without checking it here too, THIS SAME monster's
	// very next ~0.1s poll finds PendingPanicStrikeTarget already clear and falls straight into the
	// standalone "piling on" check below, calling TryStartPanicStrike() again immediately instead of
	// waiting out the delay (the machine-gun-swing regression PanicStrikeRearmDelay was added to fix in the
	// first place, reopened by this specific gap).
	if (bIsStunned || bIsStaggered || bIsBeingRidden || bIsScanning || PendingChaseAttackTarget.IsValid()
		|| PendingNaskokAttackTarget.IsValid() || PendingPanicStrikeTarget.IsValid()
		|| GetWorldTimerManager().IsTimerActive(PanicStrikeRearmTimerHandle)
		|| (IsValid(Player) && Player->bIsPanicking && this == Player->PanicEpisodeOriginator.Get()
			&& GetWorld()->GetTimeSeconds() < PanicStrikeEligibleTime))
	{
		return;
	}

	// "Piling on": deliberately a SEPARATE detection channel from bHasDetectedPlayer below (which stays
	// eyes-open-only — that's for an honest FRESH sighting, untouched) — a monster already investigating
	// THIS alert (bIsInvestigatingAlert) or already Chasing this player (State == Chase, i.e. already
	// engaged before the player went down) has no business losing track of a target that's now lying in
	// Panic exactly where it was heading, closed eyes or not. GetWorld()->GetTimeSeconds() >=
	// PanicStrikeEligibleTime: for every monster OTHER than the episode's own initiator this defaults deep
	// in the past and is always true immediately; for the initiator itself (ArmSelfPilingOnCooldown(),
	// called from ABaseCharacter::EnterPanicState()) it holds this branch off until its own opening-hit
	// animation has had PanicStrikeRearmDelay seconds to finish — the SAME pause every other "piling on"
	// monster already waits between its own repeat swings — rather than excluding the initiator from this
	// wave forever. Deliberately does NOT drive movement here at all anymore — a monster physically cannot
	// chase a live position it can no longer see with the player's eyes closed; the actual approach to the
	// FIXED point where the player first went down is entirely ReceiveAlert()'s job (it issues the
	// MoveToLocation() toward the ring point the instant the alert is accepted). This block only fires the
	// swing once that path has already gotten this monster within CatchDistance — if it hasn't yet, this
	// simply does nothing and lets ReceiveAlert()'s own move keep running uninterrupted.
	//
	// PanicEpisodeId comparison (not !bIsPanicking) is what lets bHasMissedPanicStrikeThisEpisode reset for
	// a BRAND NEW episode even while this monster is still mid-Investigate/Chase from the previous one.
	// PilingOnWindowDuration caps how long after the episode started this monster is still allowed to take
	// its attempt — covers the real travel time across a level-sized distance, not just a token few seconds.
	//
	// Eligible-but-out-of-CatchDistance case (below): a monster that reaches this point has ALREADY
	// arrived once (via ReceiveAlert()'s own move for a recruit, or was already Chase-engaged for the
	// initiator) — but its own knockback from a prior hit (ABaseCharacter::PanicKnockbackForce, ~300)
	// routinely shoves the player back out past CatchDistance (150), and nothing after that first arrival
	// ever moves this monster again on its own. Explicitly closing that gap here (rather than a silent
	// return) is what stops a solo monster from freezing in place forever after its own first hit. NOT
	// "live chase" in the sense of the ordinary bHasDetectedPlayer branch below — the player is physically
	// down and immobile for the Nose-Breathing mini-game's whole duration while bIsPanicking is true (the
	// entry condition this whole block requires), so GetActorLocation() IS the fixed point, not a moving
	// target that needs re-tracking every poll.
	if (IsValid(Player) && Player->PanicEpisodeId > 0
		&& (bIsInvestigatingAlert || State == EMonsterState::Chase)
		&& GetWorld()->GetTimeSeconds() >= PanicStrikeEligibleTime
		&& !PendingPanicStrikeTarget.IsValid()
		&& (GetWorld()->GetTimeSeconds() - Player->LastPanicEpisodeStartTime) <= PilingOnWindowDuration)
	{
		if (LastSeenPanicEpisodeId != Player->PanicEpisodeId)
		{
			LastSeenPanicEpisodeId = Player->PanicEpisodeId;
			bHasMissedPanicStrikeThisEpisode = false; // новый эпизод — чистый лист
		}

		if (!bHasMissedPanicStrikeThisEpisode)
		{
			if (FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()) <= CatchDistance)
			{
				TryStartPanicStrike(Player);
			}
			else if (AAIController* AICon = Cast<AAIController>(GetController()))
			{
				// Only issue a fresh request while not already en route — re-arming MoveToLocation() every
				// ~0.1s poll toward a target that (per this branch's own comment) isn't moving anyway would
				// be pure churn. Same "half the target range" acceptance-radius convention
				// ResolveEyePeekReaction()'s own approach move already uses.
				if (AICon->GetMoveStatus() != EPathFollowingStatus::Moving)
				{
					RequestMoveToLocation(AICon, Player->GetActorLocation(), CatchDistance * 0.5f);
				}
			}
		}
		return;
	}

	// DISABLED per "strip debug down to just the head-marker text" request — the floor/around-monster
	// sight-cone visualization is gone, but bShowEyePeekDebug still drives Tick()'s DrawDebugString()
	// head marker; see that flag's own comment. Uncomment to bring the cone back for a future bug hunt.
	// if (bShowEyePeekDebug)
	// {
	// 	const float HalfAngleRadians = FMath::DegreesToRadians(FieldOfViewAngle * 0.5f);
	// 	DrawDebugCone(GetWorld(), GetActorLocation(), GetActorForwardVector(), SightDistance,
	// 		HalfAngleRadians, HalfAngleRadians, 24, FColor::Cyan, false, 0.15f, 0, 1.5f);
	// }

	// Eye Peek Risk: a player who is nominally "eyes closed" (the normal stealth state IsPlayerVisible()
	// requires below) but currently peeking (bIsAiming, e.g. lining up a throw) is still detectable if
	// inside this monster's own sight cone — a completely separate detection channel from the open-eyes
	// Chase/catch flow below, since IsPlayerVisible()/IsPlayerLookingAtMonster() both unconditionally
	// reject a closed-eyed player. See TryTriggerEyePeekEncounter()'s own comment for the delayed
	// reaction-matrix resolution (Fatality/Panic+Alert/Dodge depending on the player's state once the
	// monster's blind swing actually lands). Deliberately does NOT return here — IsPlayerVisible() below
	// will reject this same closed-eyed player anyway, so falling through is harmless.
	if (IsValid(Player) && Player->IsEyesClosed() && Player->GetIsAiming() && IsPlayerWithinSightCone(Player))
	{
		TryTriggerEyePeekEncounter(Player);
	}

	// Whether the player is currently protected by a held item's holy light (within HolyAuraRadius +
	// StalkSafetyBuffer of it). Computed once and checked in two places below: target retention (already
	// engaged) and again right before the kill-range decision on a freshly acquired sighting — that
	// second check is what stops a monster spotting an already-close, already-lit player for the first
	// time from instantly killing them before light protection was ever considered.
	const bool bTargetInLight = IsValid(Player) && IsTargetInLight(Player);

	// Shadow-restricted combat: once safely idle in shadow during Dawn/Day (bIsIdleInShadow), this
	// monster must never chase/hold position somewhere that pulls it out of cover — continuing to do so
	// is a death sentence the instant ApplySunDamage() catches up to it. True whenever either this
	// monster's own current position or the player's has stepped into direct sunlight during such an
	// episode; checked before BOTH the light-retention (Stalk) branch below and fresh detection (Chase),
	// so an already-engaged monster breaks pursuit the moment either steps into the sun, not just when
	// refusing to newly engage.
	const bool bPursuitWouldLeaveShadow = bIsFleeingToShadow && bIsIdleInShadow && IsValid(Player)
		&& (!IsLocationInShadow(GetActorLocation()) || !IsLocationInShadow(Player->GetActorLocation()));

	if (bPursuitWouldLeaveShadow && State != EMonsterState::Patrol)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: breaking pursuit — continuing would leave shadow cover during Dawn/Day"), *GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(313, 2.0f, FColor::Orange,
				FString::Printf(TEXT("%s: pursuit broken — staying in shadow"), *GetName()));
		}
	}

	// Target retention: once the monster already has a target (Chase or Stalk — i.e. it has genuinely
	// spotted the player at least once via IsPlayerVisible()), the player being inside/near their own
	// holy light is a reason to hold position at its edge, never a reason to give up and go back to
	// Patrol. This replaces the old "treats the light like a wall and walks away" behavior: losing
	// line of sight right as it nears the light no longer drops the target the way it used to. Skipped
	// entirely while bPursuitWouldLeaveShadow — holding at the light's edge is still "pursuit" if that
	// edge sits in direct sunlight, and falls through to the detection block below (which also respects
	// the same guard) instead.
	if (bTargetInLight && State != EMonsterState::Patrol && !bPursuitWouldLeaveShadow)
	{
		EnterStalkState(Player);
		return;
	}

	// Agro (Chase/Stalk) triggers on EITHER detection channel: the monster spotting the player itself
	// (IsPlayerVisible — its own sight cone), OR the player making "eye contact" with the monster
	// (IsPlayerLookingAtMonster — independent of whether the monster's own sight cone covers them). Stays
	// eyes-open-only, deliberately unchanged — that's what makes it correct for an honest FRESH sighting.
	// The "piling on" standalone check above this function's Eye Peek Risk block is the ONLY place a
	// monster can still reach an already-panicking player with their eyes closed, and only if that
	// specific monster already had a stake in this encounter (investigating this alert, or already
	// Chasing) — see that check's own comment.
	bool bHasDetectedPlayer = Player && (IsPlayerVisible(Player) || IsPlayerLookingAtMonster(Player));

	// Shadow-restricted combat (continued): treating the player as "undetected" this tick whenever
	// pursuing them would leave shadow reuses the exact same fall-through-to-Patrol cleanup at the
	// bottom of this function that losing sight/eye-contact already uses — a monster mid-Chase breaks
	// pursuit and drops cleanly back into shadow-wandering (Tick()'s bIsIdleInShadow branch) instead of
	// needing a separate bespoke "abort chase" path, and a monster that's only just spotted a
	// sun-exposed player never starts chasing them out into it in the first place.
	if (bPursuitWouldLeaveShadow)
	{
		bHasDetectedPlayer = false;
	}

	if (bHasDetectedPlayer)
	{
		// Light protection strictly overrides the attack decision below — even on the very tick the
		// monster first spots the player, even if they're already within CatchDistance. Must be checked
		// before, never after, OnKillPlayer().
		if (bTargetInLight)
		{
			EnterStalkState(Player);
			return;
		}

		if (State == EMonsterState::Stalk)
		{
			GetWorldTimerManager().ClearTimer(FlankRescanTimerHandle);
		}
		if (State != EMonsterState::Chase)
		{
			// Background "open eyes in sight = attack" rule, first Group Aggro call: fires exactly once per
			// fresh engagement (gated on the same State != Chase transition StartChasing() itself is gated
			// on, not every ~0.1s poll for the whole chase) — same BroadcastAlert()/ReceiveAlert()
			// infrastructure TryTriggerEyePeekEncounter()/the blind-touch encounter already use for pack
			// calls, just triggered by the OTHER (open-eyes) detection channel this time.
			BroadcastAlert(Player->GetActorLocation());
			StartChasing();
		}

		const float DistanceToPlayer = FVector::Dist2D(GetActorLocation(), Player->GetActorLocation());
		if (DistanceToPlayer <= CatchDistance)
		{
			// Last-line-of-defense, independent of the bTargetInLight computed earlier this tick: this
			// is the actual kill trigger, so re-check light protection live, immediately before calling
			// it, rather than trusting that nothing between here and there could invalidate the earlier
			// result. If still protected, abort the attack outright instead of ever calling
			// OnKillPlayer() — the monster falls back to holding at the light's edge instead.
			if (IsTargetInLight(Player))
			{
				EnterStalkState(Player);
				return;
			}

			// Moonshine Rage Dash i-frames (ABaseCharacter::IsImmuneToMonsterCatch()'s own comment): skip
			// the kill entirely rather than ever calling OnKillPlayer() — the physical capsule contact
			// that put Player within CatchDistance is handled separately by UMoonshineComponent::
			// OnOwnerCapsuleHit(), which stuns+knocks back THIS monster instead. Deliberately no state
			// change here (stays in Chase) — a monster whose ram gets dodged keeps pursuing normally.
			if (Player->IsImmuneToMonsterCatch())
			{
				return;
			}

			// Background "open eyes in sight = attack" rule: catching the player open-eyed at CatchDistance
			// no longer kills outright — TryStartChaseAttack() starts the swing and defers the actual
			// crouch-dodge-vs-hit decision to its real point of contact (same OnAttackHitNotify()/
			// DoesEyePeekAttackConnect() infrastructure Eye Peek Risk uses) — see its own comment.
			TryStartChaseAttack(Player);
			return;
		}

		if (AAIController* AICon = Cast<AAIController>(GetController()))
		{
			RequestMoveToActor(AICon, Player, 5.0f);
			AICon->SetFocus(Player);
		}
		return;
	}

	if (State == EMonsterState::Chase || State == EMonsterState::Stalk)
	{
		// Eyes closed, player left range/FOV/line of sight AND broke eye contact (and the light-retention
		// check above didn't apply), OR (see bPursuitWouldLeaveShadow above) continuing would pull this
		// monster out of shadow during Dawn/Day: truly lose them and resume patrolling.
		GetWorldTimerManager().ClearTimer(FlankRescanTimerHandle);
		if (AAIController* AICon = Cast<AAIController>(GetController()))
		{
			AICon->ClearFocus(EAIFocusPriority::Gameplay);
		}
		State = EMonsterState::Patrol;
		GetCharacterMovement()->MaxWalkSpeed = PatrolSpeed;
		bIsWaiting = false;

		// Shadow-restricted: route straight to MoveToRandomWanderPoint() rather than
		// MoveToNextPatrolPoint() — a monster configured with real, fixed PatrolPoints has no shade
		// guarantee on any of them, and MoveToNextPatrolPoint() would happily send it toward one anyway.
		if (bIsFleeingToShadow && bIsIdleInShadow)
		{
			MoveToRandomWanderPoint();
		}
		else
		{
			MoveToNextPatrolPoint();
		}
		return;
	}
}

bool ABaseMonster::IsPlayerWithinSightCone(const ABaseCharacter* Player) const
{
	if (!Player)
	{
		return false;
	}

	const FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
	const float DistanceToPlayer = ToPlayer.Size();
	if (DistanceToPlayer > SightDistance)
	{
		return false;
	}

	const FVector DirectionToPlayer = ToPlayer.GetSafeNormal();
	const float AngleToPlayer = UKismetMathLibrary::DegAcos(FVector::DotProduct(GetActorForwardVector(), DirectionToPlayer));
	if (AngleToPlayer > FieldOfViewAngle * 0.5f)
	{
		return false;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Player);

	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), Player->GetActorLocation(), ECC_Visibility, QueryParams);
	if (bBlocked)
	{
		// Something (e.g. a wall) is blocking the line of sight to the player.
		return false;
	}

	return true;
}

bool ABaseMonster::IsPlayerVisible(const ABaseCharacter* Player) const
{
	if (!Player || Player->IsEyesClosed() || Player->IsHiddenInSpot())
	{
		return false;
	}

	// Checked FIRST, not as a fallback after the direct check fails below — a ridden monster is
	// typically the bigger, easier-to-spot target of the two, and this keeps both checks independent
	// (see its own comment for why).
	if (IsRiddenMonsterOfPlayerVisible(Player))
	{
		return true;
	}

	return IsPlayerWithinSightCone(Player);
}

bool ABaseMonster::IsRiddenMonsterOfPlayerVisible(const ABaseCharacter* Player) const
{
	// Phase 1 mounting is player-only, so CurrentRider will always resolve to Player in practice when
	// bIsBeingRidden — checked exactly (Get() != Player) rather than assumed, in case that ever changes.
	for (TActorIterator<ABaseMonster> It(GetWorld()); It; ++It)
	{
		ABaseMonster* OtherMonster = *It;
		if (!IsValid(OtherMonster) || OtherMonster == this || OtherMonster->IsPendingKillPending() || !OtherMonster->bIsBeingRidden)
		{
			continue;
		}
		if (OtherMonster->CurrentRider.Get() != Player)
		{
			continue;
		}

		// Same range/FOV/LOS checks as the direct player check above, just re-targeted at OtherMonster's
		// location instead of Player's.
		const FVector ToMonster = OtherMonster->GetActorLocation() - GetActorLocation();
		const float DistanceToMonster = ToMonster.Size();
		if (DistanceToMonster > SightDistance)
		{
			continue;
		}

		const FVector DirectionToMonster = ToMonster.GetSafeNormal();
		const float AngleToMonster = UKismetMathLibrary::DegAcos(FVector::DotProduct(GetActorForwardVector(), DirectionToMonster));
		if (AngleToMonster > FieldOfViewAngle * 0.5f)
		{
			continue;
		}

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);
		QueryParams.AddIgnoredActor(OtherMonster);
		QueryParams.AddIgnoredActor(Player);

		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), OtherMonster->GetActorLocation(), ECC_Visibility, QueryParams);
		if (!bBlocked)
		{
			return true;
		}
	}

	return false;
}

bool ABaseMonster::IsPlayerLookingAtMonster(const ABaseCharacter* Player) const
{
	if (!IsValid(Player) || Player->IsEyesClosed() || Player->IsHiddenInSpot())
	{
		return false;
	}

	const FVector ToMonster = GetActorLocation() - Player->GetActorLocation();
	const float DistanceToMonster = ToMonster.Size();
	if (DistanceToMonster > DetectionDistance)
	{
		return false;
	}

	const FVector DirectionToMonster = ToMonster.GetSafeNormal();
	const float AngleToMonster = UKismetMathLibrary::DegAcos(FVector::DotProduct(Player->GetActorForwardVector(), DirectionToMonster));
	if (AngleToMonster > EyeContactAngle * 0.5f)
	{
		return false;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Player);

	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Player->GetActorLocation(), GetActorLocation(), ECC_Visibility, QueryParams);
	if (bBlocked)
	{
		// Something (e.g. a wall) is blocking the player's line of sight to the monster.
		return false;
	}

	return true;
}

bool ABaseMonster::IsTargetInLight(const ABaseCharacter* Player) const
{
	if (!IsValid(Player))
	{
		return false;
	}

	const bool bProtected = IsLocationProtected(Player, GetActorLocation());

#if WITH_EDITOR
	// Gated behind bDrawDebugRadius (checked off whichever equipped item this is — the same flag
	// AHolyItem::Tick() gates its own aura sphere/cone draw behind) so a single switch controls every
	// light-tracking debug visual, monster-side included, rather than this one being permanently on
	// regardless of that setting. Defaults false — see bDrawDebugRadius's own comment.
	const bool bShowLightDebug = (IsValid(Player->EquippedRightHand) && Player->EquippedRightHand->bDrawDebugRadius)
		|| (IsValid(Player->EquippedLeftHand) && Player->EquippedLeftHand->bDrawDebugRadius);

	if (bShowLightDebug)
	{
		// Ground-truth visualization, centered on the PLAYER — not the item/hand socket, which is where
		// HolyItem::Tick()'s sphere/cone debug draw is centered. Bright red = currently protected, white =
		// not. Radius shown is whichever hand's is larger, purely for a representative visual — the actual
		// bProtected result above is computed per-item, per the comment on IsProtectedByHeldItem().
		float LightRadius = 0.0f;
		if (IsValid(Player->EquippedRightHand) && IsValid(Player->EquippedRightHand->HolyAuraSphere))
		{
			LightRadius = FMath::Max(LightRadius, Player->EquippedRightHand->HolyAuraSphere->GetScaledSphereRadius());
		}
		if (IsValid(Player->EquippedLeftHand) && IsValid(Player->EquippedLeftHand->HolyAuraSphere))
		{
			LightRadius = FMath::Max(LightRadius, Player->EquippedLeftHand->HolyAuraSphere->GetScaledSphereRadius());
		}
		if (LightRadius > 0.0f)
		{
			// NOTE this sphere is centered on Player->GetActorLocation() — the player's own root/capsule,
			// NOT a hand or a small reticle point — and runs every ~0.1s via CheckForPlayer() for as long
			// as ANY monster and player both exist, completely independent of ABaseCharacter::bIsAiming.
			UE_LOG(LogTemp, Warning, TEXT("MONSTER LIGHT-CHECK SPHERE DRAWING at Player location %s radius=%.0f protected=%s"),
				*Player->GetActorLocation().ToCompactString(), LightRadius + StalkSafetyBuffer, bProtected ? TEXT("true") : TEXT("false"));

			DrawDebugSphere(GetWorld(), Player->GetActorLocation(), LightRadius + StalkSafetyBuffer, 32,
				bProtected ? FColor::Red : FColor::White, false, 0.05f, 0, 3.0f);
		}
	}
#endif

	return bProtected;
}

bool ABaseMonster::IsProtectedByHeldItem(const AHolyItem* Item, const ABaseCharacter* Player, const FVector& MonsterLocation) const
{
	if (!IsValid(Item) || !IsValid(Item->HolyAuraSphere) || (!Item->bIsLit && !Item->bIsAwaitingLegSwipeRelight))
	{
		// bIsLit is the real "is this item currently lit" signal (see AHolyItem::Extinguish()/LightUp()) —
		// Extinguish() never zeroes HolyAuraSphere's own radius, only its collision/overlap generation, so
		// GetScaledSphereRadius() alone can't tell an extinguished item from a lit one.
		// bIsAwaitingLegSwipeRelight: a short grace window (see its own header comment) — an item just
		// extinguished by an on-foot leg swipe (AHolyItem::CheckAuraProximity()) still counts as
		// protecting the player until its scheduled self-relight resolves one way or the other. Every
		// OTHER extinguish path (fuel out, manual Extinguish(), the holy explosion) leaves this flag
		// false, so protection still drops immediately for those.
		return false;
	}

	const float ItemRadius = Item->HolyAuraSphere->GetScaledSphereRadius();
	if (ItemRadius <= 0.0f)
	{
		// Not lit.
		return false;
	}

	const float DistanceToPlayer = FVector::Dist(MonsterLocation, Player->GetActorLocation());
	if (DistanceToPlayer > ItemRadius + StalkSafetyBuffer)
	{
		return false;
	}

	// Within radius, but outside the cone (e.g. sneaking up from behind an item with
	// bUseConeProtection=true) does NOT count as protected — Item->IsLocationInProtectionCone() is
	// always true for an omnidirectional item (bUseConeProtection=false, like the Censer).
	return Item->IsLocationInProtectionCone(MonsterLocation);
}

bool ABaseMonster::IsLocationProtected(const ABaseCharacter* Player, const FVector& Location) const
{
	if (!IsValid(Player))
	{
		return false;
	}

	// Each hand is checked as a fully self-contained unit (its own radius AND its own cone) rather than
	// combining a shared "largest radius" with a shared cone result — otherwise a wide-radius Censer
	// pointed the wrong way could be mistaken for covering an angle only a narrower, correctly-facing
	// Candle actually protects, or vice versa.
	return IsProtectedByHeldItem(Player->EquippedRightHand, Player, Location)
		|| IsProtectedByHeldItem(Player->EquippedLeftHand, Player, Location);
}

bool ABaseMonster::IsPointNavigable(const FVector& Point, FVector& OutNavigablePoint) const
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys)
	{
		// No navigation system present (shouldn't normally happen) — don't block movement entirely on
		// a missing subsystem, just pass the raw point through unchanged.
		OutNavigablePoint = Point;
		return true;
	}

	// Resolve NavData using THIS monster's own nav agent properties (capsule radius/height), not
	// whichever ANavigationData happens to be registered as "default". AAIController::MoveToLocation()
	// resolves nav data the same agent-specific way internally — if this monster's agent doesn't match
	// the default agent config (multiple supported agents in Project Settings > Navigation Mesh), testing
	// against the wrong ANavigationData instance here is exactly how a candidate could look perfectly
	// valid to ProjectPointToNavigation()/TestPathSync() yet still fail the moment MoveToLocation()
	// actually issues the move against the pawn's real agent's navigation data.
	const FNavAgentProperties& AgentProps = GetNavAgentPropertiesRef();
	ANavigationData* NavData = NavSys->GetNavDataForProps(AgentProps, GetActorLocation());
	if (!NavData)
	{
		NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}
	if (!NavData)
	{
		OutNavigablePoint = Point;
		return true;
	}

	// Resolve the SAME query filter AAIController::MoveToLocation() would use — if left null here,
	// ProjectPointToNavigation()/TestPathSync() fall back to NavData's raw default filter, which can
	// differ from the controller's DefaultNavigationFilterClass (e.g. one that excludes
	// UNavArea_Obstacle outright for this agent instead of just costing it heavily). That mismatch is
	// another way a point can pass every check here and still fail the instant MoveToLocation() issues
	// the real move with the controller's actual filter.
	FSharedConstNavQueryFilter QueryFilter = nullptr;
	if (const AAIController* AICon = Cast<AAIController>(GetController()))
	{
		QueryFilter = UNavigationQueryFilter::GetQueryFilter(*NavData, this, AICon->GetDefaultNavigationFilterClass());
	}

	FNavLocation NavLocation;
	static const FVector QueryExtent(150.0f, 150.0f, 300.0f);
	if (!NavSys->ProjectPointToNavigation(Point, NavLocation, QueryExtent, &AgentProps, QueryFilter))
	{
		return false;
	}

	// Being NEAR a NavMesh polygon isn't the same as being reachable FROM here — two disconnected
	// NavMesh islands, or a point cut off by an obstacle, can both individually pass the projection
	// above yet have no actual path between them. TestPathSync() is the definitive reachability check;
	// ProjectPointToNavigation() alone was what let an unreachable-but-nearby-a-polygon point through.
	const FPathFindingQuery Query(*this, *NavData, GetActorLocation(), NavLocation.Location, QueryFilter);
	if (!NavSys->TestPathSync(Query))
	{
		return false;
	}

	OutNavigablePoint = NavLocation.Location;
	return true;
}

void ABaseMonster::StartChasing()
{
	State = EMonsterState::Chase;
	GetWorldTimerManager().ClearTimer(WaitTimerHandle);
	GetWorldTimerManager().ClearTimer(FlankRescanTimerHandle);
	GetCharacterMovement()->MaxWalkSpeed = ChaseSpeed;
}

void ABaseMonster::StartStalking()
{
	State = EMonsterState::Stalk;
	GetWorldTimerManager().ClearTimer(WaitTimerHandle);
	bIsWaiting = false;
	GetCharacterMovement()->MaxWalkSpeed = ChaseSpeed;

	// Persistent sweep direction/angle for MoveToLightBorder()'s boundary-patrol stepping, reset fresh
	// each time Stalk is (re)entered so it reads as one smooth circle rather than flip-flopping randomly
	// or picking up mid-sweep from a stale previous episode.
	BoundaryPatrolDirection = FMath::RandBool() ? 1.0f : -1.0f;
	CurrentPatrolAngle = 0.0f;
	CurrentTargetLocation = GetActorLocation();
	ConsecutiveMoveFailures = 0;

	// Kill any Chase-state momentum immediately, before issuing the boundary move below. AAIController's
	// StopMovement() only cancels the active path-following request (see the same note on ApplyStun())
	// — it does NOT zero the character's physical velocity, so without StopMovementImmediately() too, a
	// monster closing fast at ChaseSpeed could still coast forward on inertia and reach attack range
	// right as it's deciding to Stalk instead.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();

		// Lets the AI orbit smoothly around obstacles/the light boundary while flanking instead of
		// stopping to pivot on the spot for every direction change.
		AICon->bAllowStrafe = true;
	}
	GetCharacterMovement()->StopMovementImmediately();

	UE_LOG(LogTemp, Warning, TEXT("BaseMonster: Stalking at the light's edge instead of retreating to Patrol."));

	// The very first move of a Stalk episode is a pure lateral tangent step, NOT the normal boundary-step
	// search — an approach interrupted right at the boundary means this monster's own bearing from the
	// player is barely outside the aura already, so an immediate search can return a point only a token
	// distance away (or one that needs correcting after NavMesh snapping), which reads as "step back and
	// freeze" rather than a clean break from the boundary. A guaranteed sideways step away from wherever
	// this monster is standing right now gives it real, visible motion before the normal search logic
	// (MoveToLightBorder(), scheduled below) takes over on the next rescan.
	ABaseCharacter* Player = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (IsValid(Player))
	{
		const FVector TangentTarget = GetActorLocation() + (GetActorRightVector() * BoundaryPatrolDirection * 300.0f);
		FVector NavigableTangentTarget;
		const FVector MoveTarget = IsPointNavigable(TangentTarget, NavigableTangentTarget) ? NavigableTangentTarget : TangentTarget;

		if (AAIController* AICon = Cast<AAIController>(GetController()))
		{
			RequestMoveToLocation(AICon, MoveTarget, StalkAcceptanceRadius, /*bStopOnOverlap=*/true, /*bUsePathfinding=*/true,
				/*bProjectDestinationToNavigation=*/false, /*bCanStrafe=*/true);
			AICon->SetFocus(Player);
		}

		CurrentTargetLocation = MoveTarget;
	}

	// The normal rescan cycle (boundary-step search) takes over from here on out — MoveToLightBorder()
	// re-arms FlankRescanTimerHandle itself every FlankRescanIntervalMin-Max seconds; this just isn't
	// the very first call anymore.
	const float FirstRescanInterval = FMath::FRandRange(FlankRescanIntervalMin, FlankRescanIntervalMax);
	GetWorldTimerManager().SetTimer(FlankRescanTimerHandle, this, &ABaseMonster::MoveToLightBorder, FirstRescanInterval, false);
}

void ABaseMonster::EnterStalkState(ABaseCharacter* Player)
{
	if (State != EMonsterState::Stalk)
	{
		StartStalking();
	}
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->SetFocus(Player);
	}
}

void ABaseMonster::MoveToLightBorder()
{
	// A rescan that fired right as the monster stopped Stalking (stunned, or already transitioned to
	// Chase/Patrol elsewhere) should just quietly stop re-arming itself instead of moving the monster
	// or scheduling yet another rescan. bIsBeingRidden here is the actual fix for the mounted spinning
	// bug: this function is driven entirely by its own self-re-arming FlankRescanTimerHandle, completely
	// independent of Tick()/CheckForPlayer() — so if this monster was Stalking at any point (even before
	// being mounted), that timer keeps firing AICon->MoveToLocation() every FlankRescanIntervalMin-Max
	// seconds forever, bypassing every other bIsBeingRidden guard added elsewhere. Returning early here
	// (without re-arming below) is what actually breaks that chain.
	if (bIsStunned || bIsBeingRidden || State != EMonsterState::Stalk)
	{
		return;
	}

	ABaseCharacter* Player = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (!IsValid(Player))
	{
		return;
	}

	float LightRadius = 0.0f;
	if (IsValid(Player->EquippedRightHand) && IsValid(Player->EquippedRightHand->HolyAuraSphere))
	{
		LightRadius = FMath::Max(LightRadius, Player->EquippedRightHand->HolyAuraSphere->GetScaledSphereRadius());
	}
	if (IsValid(Player->EquippedLeftHand) && IsValid(Player->EquippedLeftHand->HolyAuraSphere))
	{
		LightRadius = FMath::Max(LightRadius, Player->EquippedLeftHand->HolyAuraSphere->GetScaledSphereRadius());
	}

	if (LightRadius <= 0.0f)
	{
		// The light went away (item dropped/swapped) — let the next CheckForPlayer() tick re-route
		// this monster into Chase or Patrol instead of moving it anywhere from here. No rescan re-arm:
		// there's nothing to re-evaluate until CheckForPlayer() puts it back into Stalk with a fresh
		// light to work with.
		return;
	}

	AAIController* AICon = Cast<AAIController>(GetController());
	const EPathFollowingStatus::Type MoveStatus = AICon ? AICon->GetMoveStatus() : EPathFollowingStatus::Idle;

	if (MoveStatus == EPathFollowingStatus::Idle)
	{
		// Force real search progress before even calculating this cycle's candidate: a stalled/
		// never-started path follower means whatever we tried last cycle didn't work, so recomputing
		// from the exact same unmoved position/angle would just find the same candidate again. Bumping
		// the angle here guarantees the search moves on regardless of which branch (flank or patrol)
		// ends up producing this cycle's target.
		CurrentPatrolAngle += 45.0f * BoundaryPatrolDirection;
	}

	const float OrbitDistance = LightRadius + StalkSafetyBuffer;
	const float MinDistanceFromPlayerSq = FMath::Square(LightRadius);
	const float WorldTime = GetWorld()->GetTimeSeconds();

	// CALCULATE this cycle's candidate target: step CurrentPatrolAngle another BoundaryPatrolStepDegrees
	// around the boundary ring, in the locked BoundaryPatrolDirection, NavMesh-validated. The SAME
	// mechanism regardless of item type — no separate attempt to jump straight to a computed
	// "unprotected" point on the far side of the player first. That approach (previously
	// FindFlankingLocation()) meant a single MoveToLocation() to a point that could be most of the way
	// around the circle, which is exactly what kept failing pathfinding for a directional Candle (an
	// omnidirectional Censer never had an unprotected point to jump to in the first place, which is
	// exactly why it was already using this same stepping fallback all along and never had the problem).
	// Whether this monster is still protected at each step isn't checked here at all: CheckForPlayer()
	// already re-evaluates IsTargetInLight() from this monster's actual current position every 0.1s (far
	// more often than this rescan runs) and switches it straight into Chase — and, per the
	// last-line-of-defense check right before OnKillPlayer(), straight to a kill if already in range —
	// the instant a step lands somewhere the light no longer covers.
	FVector NewTargetLocation;
	bool bHaveDestination = false;

	static constexpr int32 MaxPatrolSearchSteps = 24; // comfortably more than a full circle's worth of steps
	for (int32 SearchStep = 0; SearchStep < MaxPatrolSearchSteps; ++SearchStep)
	{
		CurrentPatrolAngle += BoundaryPatrolStepDegrees * BoundaryPatrolDirection;
		const FVector PatrolDirection = Player->GetActorForwardVector().RotateAngleAxis(CurrentPatrolAngle, FVector::UpVector);
		const FVector CandidatePatrolLocation = Player->GetActorLocation() + PatrolDirection * OrbitDistance;

		FVector NavigableLocation;
		if (!IsPointNavigable(CandidatePatrolLocation, NavigableLocation))
		{
			continue;
		}
		if (FVector::DistSquared(NavigableLocation, Player->GetActorLocation()) < MinDistanceFromPlayerSq)
		{
			continue;
		}

		NewTargetLocation = NavigableLocation;
		bHaveDestination = true;
		break;
	}

	if (GEngine)
	{
		// Fixed key (not -1): -1 appends a new, non-overwriting line every single call, and this fires
		// every FlankRescanIntervalMin-Max seconds (0.5-1s) for as long as this monster is Stalking —
		// left as -1, that floods the on-screen message list fast enough to crowd out/push off other
		// systems' fixed-key messages (e.g. ABaseCharacter's aim-reticle diagnostics) during exactly
		// the kind of active-engagement testing that also involves aiming/throwing at this monster.
		GEngine->AddOnScreenDebugMessage(200, FlankRescanIntervalMax,
			bHaveDestination ? FColor::Orange : FColor::Red,
			bHaveDestination ? TEXT("MoveToLightBorder: Patrolling Border") : TEXT("MoveToLightBorder: No navigable patrol point found"));
	}

	if (bHaveDestination && AICon)
	{
		// Step 2: only actually ISSUE a new MoveToLocation() while the path follower is Idle, Waiting, or
		// Paused — i.e. anything other than actively Moving. This is NOT a lock that can leave the AI
		// stuck: Idle/Waiting/Paused are exactly the states a failed or not-yet-started request leaves it
		// in, and MoveToLightBorder() re-arms itself every FlankRescanIntervalMin-Max seconds regardless
		// of outcome, so a non-Moving status gets a fresh MoveToLocation() attempt every single rescan.
		// What this DOES still prevent is interrupting a path that's actively executing just because a
		// newly calculated candidate differs — StopMovement()+MoveToLocation() every single rescan
		// regardless of status was the original approach, and it kept cancelling the path follower before
		// it ever got to execute a path at all.
		const bool bCanIssueMove = (MoveStatus == EPathFollowingStatus::Idle)
			|| (MoveStatus == EPathFollowingStatus::Waiting)
			|| (MoveStatus == EPathFollowingStatus::Paused);

		if (bCanIssueMove)
		{
			const EPathFollowingRequestResult::Type MoveResult = RequestMoveToLocation(AICon,
				NewTargetLocation, StalkAcceptanceRadius, /*bStopOnOverlap=*/true, /*bUsePathfinding=*/true,
				/*bProjectDestinationToNavigation=*/false, /*bCanStrafe=*/true);

			if (MoveResult == EPathFollowingRequestResult::Failed)
			{
				++ConsecutiveMoveFailures;

				// Requested diagnostic: exact distances involved, so a real geometry problem (target
				// too close to the player/monster, or absurdly far) is visible at a glance rather than
				// inferred from Idle status alone.
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(201, 0.5f, FColor::Red,
						FString::Printf(TEXT("Failed! DistToTarget: %.1f, DistToPlayer: %.1f"),
							FVector::Dist(GetActorLocation(), NewTargetLocation), FVector::Dist(Player->GetActorLocation(), NewTargetLocation)));
				}

				// Simple-movement fallback: don't wait for the next rescan to try something else — right
				// now, sidestep this exact spot with a short, pathfinding-free move (needs no NavMesh
				// query at all, so it sidesteps whatever mismatch let earlier checks pass a point
				// MoveToLocation()'s own pathfinding then refused). A lateral step relative to this
				// monster's own facing, not toward the failed target, so it makes progress regardless of
				// what was wrong with that target specifically.
				const FVector SidestepLocation = GetActorLocation() + (GetActorRightVector() * BoundaryPatrolDirection * 200.0f);
				RequestMoveToLocation(AICon, SidestepLocation, StalkAcceptanceRadius, /*bStopOnOverlap=*/true,
					/*bUsePathfinding=*/false, /*bProjectDestinationToNavigation=*/false, /*bCanStrafe=*/true);
			}

			if (MoveResult == EPathFollowingRequestResult::Failed || MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
			{
				// Either this destination can't actually be reached after all (Failed) or the monster
				// is already sitting on it without ever finding a real gap (AlreadyAtGoal). Either
				// way, don't wait for the next rescan to notice: jump CurrentPatrolAngle a full 30
				// degrees ahead right now so it isn't just re-tried at the same spot.
				CurrentPatrolAngle += 30.0f * BoundaryPatrolDirection;

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(202, FlankRescanIntervalMax, FColor::Red, TEXT("MoveToLightBorder: Movement Failed"));
				}
			}
			else
			{
				ConsecutiveMoveFailures = 0;
			}

			CurrentTargetLocation = NewTargetLocation;
		}

		AICon->SetFocus(Player);

		if (GEngine)
		{
			const TCHAR* StatusText = TEXT("Unknown");
			switch (MoveStatus)
			{
				case EPathFollowingStatus::Idle: StatusText = TEXT("Idle"); break;
				case EPathFollowingStatus::Waiting: StatusText = TEXT("Waiting"); break;
				case EPathFollowingStatus::Paused: StatusText = TEXT("Paused"); break;
				case EPathFollowingStatus::Moving: StatusText = TEXT("Moving"); break;
				default: break;
			}
			const bool bShieldCooldownActive = WorldTime < ShieldBounceCooldownUntil;
			GEngine->AddOnScreenDebugMessage(203, FlankRescanIntervalMax, FColor::Cyan,
				FString::Printf(TEXT("MoveToLightBorder: Status=%s Target=%s ShieldCooldown=%s"),
					StatusText, *NewTargetLocation.ToCompactString(), bShieldCooldownActive ? TEXT("Active") : TEXT("Inactive")));
		}
	}

	// Re-arm for another rescan a short, randomized interval away — independent of whether this move
	// is ever reached — so a gap opening mid-approach (the player turning, dropping an item, etc.) is
	// caught within roughly FlankRescanIntervalMin-Max seconds instead of only after fully arriving
	// somewhere and running out a separate wait.
	const float NextInterval = FMath::FRandRange(FlankRescanIntervalMin, FlankRescanIntervalMax);
	GetWorldTimerManager().SetTimer(FlankRescanTimerHandle, this, &ABaseMonster::MoveToLightBorder, NextInterval, false);
}

void ABaseMonster::MoveToNextPatrolPoint()
{
	// Same crash hardening as MoveToRandomWanderPoint() (see its own comment) — this can also be reached
	// through a stale queued call (Tick's WaitTimerHandle lambda) after something upstream has left this
	// monster mid-teardown.
	if (!IsValid(this))
	{
		return;
	}

	// Guards the case where a call to this was already queued (e.g. Tick's WaitTimerHandle lambda)
	// before the monster got stunned in the meantime — without this it would still issue a fresh
	// move command while bIsStunned is true.
	if (bIsStunned)
	{
		return;
	}

	bIsWaiting = false;

	// bUseRandomWander OR an empty PatrolPoints array (see that property's own comment) — same decision
	// Tick()'s Patrol handling already made for this same reached point.
	if (bUseRandomWander || PatrolPoints.Num() == 0)
	{
		MoveToRandomWanderPoint();
		return;
	}

	CurrentPatrolIndex = FMath::Clamp(CurrentPatrolIndex, 0, PatrolPoints.Num() - 1);

	// IsValid() before Cast<>() — see MoveToRandomWanderPoint()'s identical AICon comment for why a bare
	// non-null check on GetController() isn't enough.
	AController* RawController = GetController();
	AAIController* AICon = IsValid(RawController) ? Cast<AAIController>(RawController) : nullptr;
	if (AICon && PatrolPoints[CurrentPatrolIndex])
	{
		RequestMoveToActor(AICon, PatrolPoints[CurrentPatrolIndex], PatrolAcceptanceRadius);
		UE_LOG(LogTemp, Warning, TEXT("BaseMonster: Moving to point %d (%s)"), CurrentPatrolIndex, *PatrolPoints[CurrentPatrolIndex]->GetName());
	}
}

void ABaseMonster::MoveToRandomWanderPoint()
{
	// Crash hardening (ACCESS_VIOLATION reported here on a character switch): this function is reached
	// both directly and via its own retry timer (WaitTimerHandle, below), which can still fire after
	// something upstream of this monster — a level-wide respawn/reinit triggered by swapping the playable
	// character, per the report — has left it (or one of the pointers it touches) mid-teardown. IsValid(this)
	// first, since every check after this assumes the monster itself is still a genuinely live actor, not
	// just a non-null "this" whose memory is on its way out.
	if (!IsValid(this))
	{
		return;
	}

	// Same bIsStunned guard as MoveToNextPatrolPoint() itself — this function is reached both THROUGH
	// that one and directly (its own retry timer, and Tick()'s shadow-wander branch), so re-checked here
	// too since it's also where the actual AICon->MoveToLocation() call lives, matching this class's
	// general "check again right where the side effect happens" style.
	if (bIsStunned)
	{
		return;
	}

	// Cleared unconditionally on entry, same as MoveToNextPatrolPoint() does — load-bearing for the
	// direct-retry path (this function re-arms its OWN retry timer against itself, not through
	// MoveToNextPatrolPoint(), to stay shadow-restricted across retries — see that timer's comment),
	// which would otherwise leave bIsWaiting stuck true forever after a successful retry, since nothing
	// else downstream of a direct call here ever clears it.
	bIsWaiting = false;

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogTemp, Error, TEXT("%s: MoveToRandomWanderPoint() — no valid World, cannot wander"), *GetName());
		return;
	}

	// Guard against a physically-blocked CharacterMovementComponent — a MaxWalkSpeed of 0 (or an
	// update still mid-flight) would let AICon->MoveToLocation() below succeed and report a valid
	// path while the monster's world transform never actually advances, which is indistinguishable
	// from "AI is stuck" from the outside. IsValid() (not a bare non-null check) — GetCharacterMovement()
	// returns whatever raw pointer this actor's CharacterMovementComponent member last held, which can
	// still be non-null but already pending-kill/garbage if the component was destroyed out from under
	// this actor (e.g. mid character-switch teardown elsewhere) — the exact ACCESS_VIOLATION reported here.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement(); IsValid(MoveComp))
	{
		if (MoveComp->MaxWalkSpeed <= 0.0f)
		{
			UE_LOG(LogTemp, Error, TEXT("%s: MaxWalkSpeed was %.1f — forcing back to PatrolSpeed (%.1f)"),
				*GetName(), MoveComp->MaxWalkSpeed, PatrolSpeed);
			MoveComp->MaxWalkSpeed = PatrolSpeed;
		}
		if (MoveComp->IsMovementInProgress())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: MoveToRandomWanderPoint() called while CharacterMovementComponent is mid-update"), *GetName());
		}
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!IsValid(NavSys))
	{
		UE_LOG(LogTemp, Error, TEXT("%s: no UNavigationSystemV1 present — cannot wander"), *GetName());
		return;
	}

	// Shadow-restricted wander: while safely idle in shadow during Dawn/Day (bIsIdleInShadow), every
	// sampled candidate must ALSO pass a sun-trace itself before being accepted — an ordinary random
	// wander point could otherwise send this monster right back out into direct sunlight the moment it
	// arrives, undoing the entire reason it fled to shadow in the first place. Ordinary Night wander
	// (bIsIdleInShadow false) keeps sampling exactly once, same as before.
	const bool bRestrictToShadow = bIsFleeingToShadow && bIsIdleInShadow;
	const int32 MaxAttempts = bRestrictToShadow ? MaxShadowSearchAttempts : 1;

	FNavLocation RandomNavLocation;
	bool bFoundValidPoint = false;
	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		if (!NavSys->GetRandomReachablePointInRadius(GetActorLocation(), WanderRadius, RandomNavLocation))
		{
			continue;
		}

		if (!bRestrictToShadow || IsLocationInShadow(RandomNavLocation.Location))
		{
			bFoundValidPoint = true;
			break;
		}
	}

	if (!bFoundValidPoint)
	{
		// Either no reachable point was found at all, or (while shadow-restricted) none of the reachable
		// points sampled stayed shaded — the shadow area may simply be too small to wander within right
		// now. Retry shortly rather than leaving this monster frozen with no move ever issued, same
		// fallback AEnemyAIController::MoveToNewPatrolPoint() already uses for the identical failure
		// case. bIsWaiting=true for the retry window is load-bearing, not just bookkeeping: Tick()'s
		// Patrol/shadow-wander handling checks GetMoveStatus() != Moving to detect "reached a point" —
		// with no move ever actually issued here, it would read as Idle and Tick() would try to react to
		// that itself on the very next frame, racing this explicit retry timer, if bIsWaiting didn't tell
		// it to skip this monster entirely until the retry fires. Cleared again the moment the retry
		// actually fires (this function clears it unconditionally on entry), so this can never get
		// permanently stuck waiting. Retries THIS function directly (not MoveToNextPatrolPoint()) — a
		// monster configured with real PatrolPoints could otherwise have its shadow-restricted retry
		// divert straight to a fixed patrol point with no shade guarantee at all.
		bIsWaiting = true;
		const float RetryDelay = FMath::FRandRange(0.5f, 1.0f);
		GetWorldTimerManager().SetTimer(WaitTimerHandle, this, &ABaseMonster::MoveToRandomWanderPoint, RetryDelay, false);

		UE_LOG(LogTemp, Error, TEXT("%s: %swander point search failed at %s (radius %.0f) — retrying in %.2fs"),
			*GetName(), bRestrictToShadow ? TEXT("shadow-restricted ") : TEXT(""), *GetActorLocation().ToCompactString(), WanderRadius, RetryDelay);
		if (bDebugSunTrace && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(210, RetryDelay, FColor::Red,
				FString::Printf(TEXT("%s: %swander point search FAILED — retrying in %.2fs"),
					*GetName(), bRestrictToShadow ? TEXT("shadow-restricted ") : TEXT(""), RetryDelay));
		}
		return;
	}

	// IsValid() on the raw AController* BEFORE Cast<>() — Cast<> dereferences the object's UClass to check
	// its type, which would itself crash if GetController() ever returned a stale/pending-kill pointer
	// rather than a clean nullptr (the other half of this same hardening pass — see MoveComp's own
	// comment above for the identical raw-pointer-vs-IsValid() distinction).
	AController* RawController = GetController();
	AAIController* AICon = IsValid(RawController) ? Cast<AAIController>(RawController) : nullptr;
	if (AICon)
	{
		RequestMoveToLocation(AICon, RandomNavLocation.Location, PatrolAcceptanceRadius);

		UE_LOG(LogTemp, Warning, TEXT("%s: %swandering to random point %s"),
			*GetName(), bRestrictToShadow ? TEXT("(shadow-restricted) ") : TEXT(""), *RandomNavLocation.Location.ToCompactString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(211, 2.0f, FColor::Green,
				FString::Printf(TEXT("%s: %swandering to %s"),
					*GetName(), bRestrictToShadow ? TEXT("(shadow) ") : TEXT(""), *RandomNavLocation.Location.ToCompactString()));
		}
	}
}

float ABaseMonster::GetMovementSpeed() const
{
	return GetVelocity().Size2D();
}

void ABaseMonster::PlayAttackAnimation()
{
	if (!IsValid(AttackAnimation) || !IsValid(GetMesh()))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::PlayAttackAnimation: refused — AttackAnimation valid: %s, GetMesh() valid: %s"),
			*GetName(), IsValid(AttackAnimation) ? TEXT("YES") : TEXT("NO"), IsValid(GetMesh()) ? TEXT("YES") : TEXT("NO"));
		return;
	}

	UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInst)
	{
		UE_LOG(LogTemp, Error, TEXT("%s::PlayAttackAnimation: GetMesh()->GetAnimInstance() is null — no Animation Blueprint assigned (see the ABP_Whisper path fix in the constructor)."), *GetName());
		return;
	}

	// No pre-authored Montage asset needed — PlaySlotAnimationAsDynamicMontage() builds one on the fly
	// from AttackAnimation directly. Requires a Slot node named AttackAnimSlotName somewhere in the
	// AnimGraph (fed from the locomotion state machine's output, in turn feeding Output Pose) —
	// otherwise this "succeeds" (still returns a valid UAnimMontage*) with no visible effect, since
	// nothing in the graph is listening on that slot. Explicit parameters here (rather than relying on
	// the 2-argument overload's own 0.25s/0.25s/1.0x defaults) purely so every value driving the swing is
	// visible at this one call site; a snappier 0.05s blend in/out reads as a punchier hit than the
	// default 0.25s.
	UAnimMontage* PlayedMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(
		AttackAnimation,
		AttackAnimSlotName,
		0.05f,  // BlendInTime
		0.05f,  // BlendOutTime
		1.0f,   // InPlayRate
		1,      // LoopCount
		-1.0f,  // BlendOutTriggerTime
		0.0f);  // InTimeToStartMontageAt

	// PlaySlotAnimationAsDynamicMontage() returns a UAnimMontage*, not a duration directly — a null
	// return (rather than any particular Duration value) is what actually signals failure; GetPlayLength()
	// is the montage's own real duration once it did start.
	const float Duration = IsValid(PlayedMontage) ? PlayedMontage->GetPlayLength() : 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("%s::PlayAttackAnimation: played '%s' on slot '%s'. Returned Duration: %f"),
		*GetName(), *AttackAnimation->GetName(), *AttackAnimSlotName.ToString(), Duration);

	if (!IsValid(PlayedMontage))
	{
		UE_LOG(LogTemp, Error, TEXT("%s::PlayAttackAnimation: PlaySlotAnimationAsDynamicMontage returned NO montage — it did not actually start."), *GetName());
	}
}

void ABaseMonster::OnKillPlayer(ABaseCharacter* Player)
{
	// See bHasCaughtPlayer's own comment — without this, CheckForPlayer()'s ~0.1s poll would keep
	// re-entering this function for as long as the (now-stationary) ragdolled Player stays within
	// CatchDistance, which is effectively forever.
	if (bHasCaughtPlayer)
	{
		return;
	}
	bHasCaughtPlayer = true;

	// Stop navigating immediately: CheckForPlayer()'s CatchDistance branch (the call site just before
	// this) doesn't itself cancel an already-in-flight Chase move request, so without this the monster
	// can keep sweeping its capsule forward — straight into the Player's ragdoll — for a tick or two
	// after the catch registers.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
	}
	if (UCharacterMovementComponent* MonsterMoveComp = GetCharacterMovement())
	{
		MonsterMoveComp->StopMovementImmediately();
	}

	// Ragdolls immediately (not delayed) — the player's death should register the instant the catch
	// triggers; it's only the LEVEL RELOAD below that waits, so the ragdoll's fall (and this monster's
	// attack animation, already started via PlayAttackAnimation() at the call site) both have time to
	// actually be seen instead of the level cutting away the same tick.
	if (IsValid(Player))
	{
		Player->HandleDeath();

		// Collision: HandleDeath() just switched Player's mesh to the "Ragdoll" profile, which now
		// physically simulates and would otherwise clip/bounce against this still-standing monster's
		// capsule for the rest of the kill sequence. IgnoreActorWhenMoving() would only stop THIS
		// monster's own sweep-based movement queries from being blocked by Player — it does nothing for
		// the ragdoll's independent physics simulation colliding with this monster's (non-simulating,
		// still world-colliding) capsule, which is resolved purely by collision channel/response, hence
		// the explicit per-channel Ignore below instead. Read the object types AFTER HandleDeath() so
		// the ragdoll's actual post-profile-switch channel is used, not whatever the mesh had before.
		if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh())
		{
			if (UCapsuleComponent* MonsterCapsule = GetCapsuleComponent())
			{
				MonsterCapsule->SetCollisionResponseToChannel(PlayerMesh->GetCollisionObjectType(), ECR_Ignore);
				PlayerMesh->SetCollisionResponseToChannel(MonsterCapsule->GetCollisionObjectType(), ECR_Ignore);
			}
			if (USkeletalMeshComponent* MonsterMesh = GetMesh())
			{
				MonsterMesh->SetCollisionResponseToChannel(PlayerMesh->GetCollisionObjectType(), ECR_Ignore);
				PlayerMesh->SetCollisionResponseToChannel(MonsterMesh->GetCollisionObjectType(), ECR_Ignore);
			}
		}
	}

	GetWorldTimerManager().SetTimer(AttackKillTimerHandle, this, &ABaseMonster::ReloadLevelAfterDeath, DeathReloadDelay, false);
}

void ABaseMonster::ReloadLevelAfterDeath()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::OpenLevel(this, FName(*World->GetName()));
	}
}

float ABaseMonster::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Diagnostic for the mounted-throw investigation: identifies exactly WHO called ApplyDamage() on
	// this monster and whether it was currently being ridden at the time, so a still-reproducing "mount
	// dies when its own rider throws" report can be pinned to its actual caller instead of theorized
	// about. DamageCauser here is the `this` AHolyItem passed to UGameplayStatics::ApplyDamage() in
	// every AHolyItem damage path — if this fires with DamageCauser naming a HolyItem AND
	// bIsBeingRidden true, that specific call site's bIsBeingRidden/ThrowerMountedMonster guard didn't
	// catch it and needs to be re-checked directly.
	UE_LOG(LogTemp, Warning, TEXT("%s::TakeDamage: %.1f dmg from %s (type %s), bIsBeingRidden=%s"),
		*GetName(), DamageAmount, DamageCauser ? *DamageCauser->GetName() : TEXT("None"),
		DamageEvent.DamageTypeClass ? *DamageEvent.DamageTypeClass->GetName() : TEXT("None"),
		bIsBeingRidden ? TEXT("true") : TEXT("false"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(223, 4.0f, bIsBeingRidden ? FColor::Red : FColor::White,
			FString::Printf(TEXT("TakeDamage: %s -> %.1f dmg from %s (ridden=%s)"),
				*GetName(), DamageAmount, DamageCauser ? *DamageCauser->GetName() : TEXT("None"),
				bIsBeingRidden ? TEXT("TRUE") : TEXT("false")));
	}

	if (!DamageEvent.DamageTypeClass || !DamageEvent.DamageTypeClass->IsChildOf(UHolyDamageType::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("BaseMonster ignored non-holy damage!"));
		return 0.0f;
	}

	// Getting knocked off a mount on any hit while riding, per the mount spec — this monster receiving
	// damage while bIsBeingRidden dismounts and knocks back whoever's riding it, same as
	// ABaseCharacter::TakeDamage() does for damage taken directly by the player. Checked before the
	// Health/state processing below so it fires regardless of damage type/amount.
	if (bIsBeingRidden)
	{
		// Only ARiderCharacter can ever be bIsBeingRidden's rider (see UMountRiderComponent) — cast to
		// that specifically (not just ABaseCharacter) since MountKnockOffForce/UpwardForce now live on
		// its MountRiderComponent, isolated out of ABaseCharacter entirely.
		if (ARiderCharacter* Rider = Cast<ARiderCharacter>(CurrentRider.Get()))
		{
			const FVector KnockDirection = IsValid(DamageCauser)
				? (Rider->GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal2D()
				: -Rider->GetActorForwardVector();

			Rider->Dismount();

			if (IsValid(Rider->MountRiderComponent))
			{
				Rider->LaunchCharacter(KnockDirection * Rider->MountRiderComponent->MountKnockOffForce
					+ FVector(0.0f, 0.0f, Rider->MountRiderComponent->MountKnockOffUpwardForce), true, true);
			}

			UE_LOG(LogTemp, Warning, TEXT("%s: ridden monster took damage — knocked %s off!"), *GetName(), *Rider->GetName());
		}
	}

	// Censer damage is now amount-based (was an unconditional Health=0/HandleDeath() here regardless of
	// DamageAmount) — AHolyItem::CenserThrowDamage (500) still one-shots a regular 100-HP ABaseMonster
	// via the Health<=0 check below, same as before, but a future boss subclass with higher MaxHealth
	// can now survive a single Censer hit and take repeated hits instead of always dying to the first.
	Health -= DamageAmount;

	UE_LOG(LogTemp, Warning, TEXT("BaseMonster took %.1f Holy Damage! HP left: %.1f"), DamageAmount, Health);

	if (DamageEvent.DamageTypeClass->IsChildOf(UCandleDamageType::StaticClass()))
	{
		// ShieldBounceCooldown: Health still took the hit above, but suppress ApplyStun()/StopMovement()
		// itself while the cooldown from a previous stun's recovery is still active — see
		// ShieldBounceCooldown's comment for why.
		const bool bShieldCooldownActive = GetWorld()->GetTimeSeconds() < ShieldBounceCooldownUntil;
		if (!bIsDead && !bIsStunned && !bShieldCooldownActive)
		{
			ApplyStun();
		}
	}

	// This is the actual "fear of light" mechanism in this codebase (no separate Light Reaction
	// Component) — AICon->MoveToLocation() issues an AI nav-move request that would fight the rider's
	// own ABaseCharacter::AddMovementInput() passthrough every tick while ridden, which is exactly the
	// kind of two-sources-driving-movement-at-once conflict that reads as chaotic spinning once
	// bOrientRotationToMovement turns that fight into visible rotation.
	if (!bIsStunned && !bIsBeingRidden && DamageCauser)
	{
		const FVector FleeDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal2D();
		const FVector FleeTarget = GetActorLocation() + FleeDirection * 600.0f;

		if (AAIController* AICon = Cast<AAIController>(GetController()))
		{
			RequestMoveToLocation(AICon, FleeTarget);
			UE_LOG(LogTemp, Warning, TEXT("BaseMonster is fleeing from Holy Light!"));
		}
	}

	if (Health <= 0.0f)
	{
		HandleDeath(DamageCauser);
	}

	return DamageAmount;
}

void ABaseMonster::HandleDeath(AActor* DamageCauser)
{
	// Guard against being called while the actor is already mid-destruction (e.g. a
	// queued overlap/damage event resolving after Destroy() has been requested), which
	// would otherwise touch components/timers on an object UObjectArray is tearing down.
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}
	bIsDead = true;

	// WaitTimerHandle/DestroyTimerHandle are TFunction-based timers that capture `this` by raw
	// pointer, so unlike the UFUNCTION-bound timers they are NOT auto-invalidated if this actor
	// gets destroyed while they're still pending. Clear everything now so nothing fires late and
	// touches a torn-down UObject (the UObjectArray "Index >= 0" crash this guards against).
	GetWorldTimerManager().ClearAllTimersForObject(this);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->DisableMovement();
		MoveComp->StopMovementImmediately();
	}

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
		AICon->UnPossess();
	}

	// Disabled explicitly (not just left to Destroy() after DeathDestroyDelay) so a dead-but-not-yet-
	// destroyed body immediately stops blocking the player/other actors — most noticeable for Die()'s
	// instant-kill path (a thrown Censer), where the monster can be standing right in the player's way
	// the instant it dies.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	UE_LOG(LogTemp, Warning, TEXT("BaseMonster was completely destroyed by Holy Light!"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(204, 5.0f, FColor::Red, TEXT("BaseMonster burned to ashes!"));
	}

	if (BurnEffect)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BurnEffect, GetActorLocation(), GetActorRotation());
	}

	if (BurnNiagaraEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), BurnNiagaraEffect, GetActorLocation(), GetActorRotation());
	}

	if (DeathEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), DeathEffect, GetActorLocation());
	}

	if (BurnSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(GetWorld(), BurnSound, GetActorLocation());
	}

	GetWorldTimerManager().SetTimer(DestroyTimerHandle, [this]()
	{
		Destroy();
	}, DeathDestroyDelay, false);
}

void ABaseMonster::ApplyStun()
{
	// Contact-stun path (candle aura/collision damage via TakeDamage()) always uses this monster's
	// own StunDuration — Stun() is the shared primitive; see its header comment for why a caller like
	// a thrown item's impact needs its own explicit duration instead.
	Stun(StunDuration);
}

void ABaseMonster::Stun(float Duration)
{
	if (!IsValid(this) || IsPendingKillPending() || Health <= 0.0f || bIsDead)
	{
		return;
	}

	// Absolute immunity while ridden — this is the actual shared primitive both ApplyStun() (the
	// contact-stun path) AND external callers (e.g. AHolyItem::OnItemHit()'s thrown-Candle AoE, which
	// bypasses TakeDamage()/ApplyStun() entirely) funnel through, so guarding here is the one choke
	// point that covers every possible stun source at once.
	if (bIsBeingRidden)
	{
		return;
	}

	bIsStunned = true;

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		// StopMovement() only cancels the current AI move request; capsule/WorldStatic collision
		// and CharacterMovementComponent's gravity/walking mode are left as-is on purpose, so a
		// stunned monster stays standing on the floor instead of falling through it.
		AICon->StopMovement();
	}

	UE_LOG(LogTemp, Warning, TEXT("BaseMonster is stunned for %.1f seconds!"), Duration);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(205, Duration, FColor::Yellow, TEXT("BaseMonster is stunned!"));
	}

	GetWorldTimerManager().SetTimer(StunTimerHandle, this, &ABaseMonster::ResetStun, Duration, false);
}

void ABaseMonster::Die()
{
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}
	Health = 0.0f;
	HandleDeath(nullptr);
}

void ABaseMonster::ApplyKnockback(FVector ImpactDirection, float PushForce, float UpwardForce)
{
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}

	// Absolute immunity while ridden — every knockback source (held-Censer pushback, melee swing, a
	// thrown item's impact, etc.) funnels through this one function, so guarding here catches all of
	// them regardless of which one the rider happens to trigger.
	if (bIsBeingRidden)
	{
		return;
	}

	// Flattened first: a caller passing a direction with a large/zero Z component (e.g. straight up, or
	// a raw Monster-minus-Item vector where the item sits noticeably above/below the monster's origin)
	// would otherwise skew or degenerate the launch — GetSafeNormal2D() drops Z entirely and normalizes
	// the horizontal component, with UpwardForce below adding the vertical "pop" back in separately and
	// explicitly. Falls back to pushing straight back along this monster's own forward vector if the
	// flattened input is (nearly) zero, e.g. the impact came from directly above/below.
	const FVector FlattenedDirection = ImpactDirection.GetSafeNormal2D();
	const FVector PushDirection = FlattenedDirection.IsNearlyZero() ? -GetActorForwardVector() : FlattenedDirection;

	const FVector LaunchVel = (PushDirection * PushForce) + FVector(0.0f, 0.0f, UpwardForce);
	LaunchCharacter(LaunchVel, true, true);

	// See bIsStaggered's header comment: without this, a Chase-state monster's own pathing would start
	// correcting back toward the player the very same tick, masking the launch. Re-arming (not
	// no-opping) on a repeat call while already staggered is deliberate — a second knockback should
	// extend the suppression window to match, not let it expire mid-launch.
	bIsStaggered = true;
	GetWorldTimerManager().SetTimer(StaggerTimerHandle, this, &ABaseMonster::ResetStagger, StaggerDuration, false);

	UE_LOG(LogTemp, Warning, TEXT("BaseMonster knocked back: direction=%s push=%.0f up=%.0f"),
		*PushDirection.ToCompactString(), PushForce, UpwardForce);
}

void ABaseMonster::ResetStagger()
{
	bIsStaggered = false;
}

void ABaseMonster::OnCapsuleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (Health <= 0.0f || bIsDead)
	{
		return;
	}

	ABaseCharacter* Player = Cast<ABaseCharacter>(OtherActor);
	if (!IsValid(Player) || Player->bIsDead)
	{
		return;
	}

	// Moonshine Rage Dash i-frames (ABaseCharacter::IsImmuneToMonsterCatch()'s own comment): a Ram is
	// never either kind of encounter below, from any angle of impact — UMoonshineComponent::
	// OnOwnerCapsuleHit() (bound to the PLAYER's own capsule, fires independently of this function)
	// already handles it via knockback+stun. Checked ONCE here, shared by both branches, per this
	// function's own header comment.
	if (Player->IsImmuneToMonsterCatch())
	{
		return;
	}

	if (Player->IsEyesClosed())
	{
		// Повторное касание ТЕМ ЖЕ игроком, пока уже идёт Blind Encounter именно с ним — наказываем,
		// как повторный Naskok/Probe во время bIsScanning. Если bHasActiveBlindEncounter активен с
		// ДРУГИМ игроком — просто игнорируем контакт, как и раньше.
		if (bHasActiveBlindEncounter)
		{
			// !IsTimerActive(BlindReactionTimerHandle): don't punish while still inside the original
			// BlindTouchReactionDelay window — ResolveBlindEncounterReaction() hasn't decided Scenario A/B
			// yet, so a second hit event this soon is almost certainly a leftover artifact of the SAME
			// physical contact (e.g. multiple substep hits), not a genuinely new provocation.
			// !Player->bIsPanicking: don't punish while the player is already down from Scenario B's own
			// knockdown — same "don't double-punish a state that's already resolving" reasoning
			// CheckForPlayer() uses elsewhere.
			if (CurrentScanTarget.Get() == Player
				&& !GetWorldTimerManager().IsTimerActive(BlindReactionTimerHandle)
				&& !Player->bIsPanicking)
			{
				PunishRepeatProvocation(Player);
			}
			return;
		}

		// ---- Blind Encounter — UNCHANGED, just moved under this eyes-closed branch ----

		// Already mid-encounter with some OTHER monster — don't let a second monster's capsule contact
		// (e.g. bumping past it while fleeing the first) start a competing encounter.
		if (Player->BlindEncounterMonster.IsValid())
		{
			return;
		}

		bHasActiveBlindEncounter = true;

		UE_LOG(LogTemp, Warning, TEXT("%s: blind touch with %s — broadcasting alert and deciding reaction in %.1fs"),
			*GetName(), *Player->GetName(), BlindTouchReactionDelay);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(250, 3.0f, FColor::Purple,
				FString::Printf(TEXT("%s: blind touch with %s"), *GetName(), *Player->GetName()));
		}

		BroadcastAlert(GetActorLocation());

		// The monster this player actually touched reacts immediately by searching the area — decoupled from
		// whichever Scenario ResolveBlindEncounterReaction() decides on below, so this monster never just
		// stands there doing nothing during a Scenario B knockdown either.
		EnterScanningState(Player);

		TWeakObjectPtr<ABaseCharacter> WeakPlayer = Player;
		GetWorldTimerManager().SetTimer(BlindReactionTimerHandle,
			FTimerDelegate::CreateUObject(this, &ABaseMonster::ResolveBlindEncounterReaction, WeakPlayer),
			BlindTouchReactionDelay, false);
		return;
	}

	// ---- Naskok — eyes open, see TryStartNaskokAttack()'s own comment ----
	// Light-protected physical contact is a no-op — the monster shouldn't have been able to reach the
	// player at all (verhom the Censer's own pushback already prevents this; on foot/Candle there's no
	// physical push, so this is what actually stops the touch from landing a Naskok hit).
	if (IsTargetInLight(Player))
	{
		return;
	}
	TryStartNaskokAttack(Player);
}

// Naskok — see this function's own header comment for the full design.
void ABaseMonster::TryStartNaskokAttack(ABaseCharacter* Player)
{
	if (!IsValid(Player) || PendingNaskokAttackTarget.IsValid())
	{
		return;
	}

	// Repeat contact while THIS monster is already bIsScanning FOR THIS SAME PLAYER — i.e. a second Ram
	// landing while still riled up/searching from an earlier Naskok miss. No second chance to dodge here:
	// immediate hit, exactly like the design calls for.
	if (bIsScanning && CurrentScanTarget.Get() == Player)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: repeat ram from %s while still scanning — instant hit (PANIC)"),
			*GetName(), *Player->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(342, 3.0f, FColor::Red, TEXT("RAMMED AGAIN — no time to dodge!"));
		}

		// Same "captured before ApplyDamage()" mount-dismount reasoning as the Eye Peek/Chase-attack hit
		// branches in OnAttackHitNotify() — see their own comments.
		const bool bWasMountedBeforeHit = Player->IsMounted();
		UGameplayStatics::ApplyDamage(Player, ChaseAttackDamage, GetInstigatorController(), this, UDamageType::StaticClass());
		if (!bWasMountedBeforeHit)
		{
			Player->EnterPanicState(this);
		}
		BroadcastAlert(GetActorLocation());
		return;
	}

	// First contact — the usual "swing with a chance to dodge", resolved later at the animation's real
	// point of contact via OnAttackHitNotify()/DoesEyePeekAttackConnect(). Deliberately does NOT
	// BroadcastAlert() here (unlike TryStartChaseAttack(), which alerts immediately on swing start) — see
	// this function's own header comment: only a CONFIRMED hit calls the pack in, not a mere attempt.
	UE_LOG(LogTemp, Warning, TEXT("%s: rammed by %s with eyes open — swinging"), *GetName(), *Player->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(341, 3.0f, FColor::Orange,
			FString::Printf(TEXT("%s: RAMMED (eyes open) — swinging!"), *GetName()));
	}

	// Stop navigating before playing the swing — same reasoning as TryStartChaseAttack()'s own equivalent.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
	}
	if (UCharacterMovementComponent* MonsterMoveComp = GetCharacterMovement())
	{
		MonsterMoveComp->StopMovementImmediately();
	}

	bNaskokAttackTargetWasCrouchedAtSwingStart = Player->IsCrouched();
	PendingNaskokAttackTarget = Player;
	PlayAttackAnimation();

	// FALLBACK — same idempotent role as ChaseAttackHitFallbackTimerHandle/EyePeekAttackHitFallbackTimerHandle
	// (see their own comments). Reuses ChaseAttackHitFallbackDelay for the delay value itself (a separate
	// FTimerHandle only, per this feature's own design) — a swing's windup timing doesn't need its own
	// tunable just because it comes from a third source now.
	GetWorldTimerManager().SetTimer(NaskokAttackHitFallbackTimerHandle, this, &ABaseMonster::OnAttackHitNotify,
		ChaseAttackHitFallbackDelay, false);
}

// Same "second provocation while already alert to this same player = instant hit" punishment as
// TryStartNaskokAttack()'s own repeat-physical-contact branch above (see its own comment) — kept as a
// SEPARATE function rather than a refactor of that block, deliberately, so as not to risk already-tested
// code. Called from three places: UProbeComponent::Probe() (repeat probe while bIsScanning OR while
// ReactToProbe()'s startle window is still active — IsScanningForPlayer()/IsStartledByProbe()), and
// OnCapsuleHit() (repeat physical contact by the same player during an active Blind Encounter with them).
void ABaseMonster::PunishRepeatProvocation(ABaseCharacter* Player)
{
	if (!IsValid(Player))
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: probed again by %s while still alert — instant hit (PANIC)"),
		*GetName(), *Player->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(345, 3.0f, FColor::Red, TEXT("PROBED AGAIN — no time to dodge!"));
	}

	const bool bWasMountedBeforeHit = Player->IsMounted();
	UGameplayStatics::ApplyDamage(Player, ChaseAttackDamage, GetInstigatorController(), this, UDamageType::StaticClass());
	if (!bWasMountedBeforeHit)
	{
		Player->EnterPanicState(this);
	}
	BroadcastAlert(GetActorLocation());
}

void ABaseMonster::ReactToProbe(ABaseCharacter* Player)
{
	if (!IsValid(Player) || bIsScanning || bIsStunned || bIsStaggered || bIsBeingRidden)
	{
		return;
	}

	ProbeStartleTarget = Player;

	// Останавливаем движение, пока держим фокус — та же причина, по которой EnterScanningState() делает
	// то же самое: SetFocus()/bUseControllerDesiredRotation крутит модель к игроку НЕЗАВИСИМО от того,
	// куда AI сейчас едет по навигации — без остановки монстр физически продолжает идти к своей старой
	// точке патруля, просто теперь задом наперёд, лицом к игроку. StopMovement() отменяет текущий
	// move-request; обычная Patrol/wander-логика в Tick() сама подхватит и выберет новую точку, как
	// только фокус снимется (EndProbeStartle()) — тот же паттерн восстановления, что уже используют
	// стан/стаггер/другие временные прерывания движения в этом классе.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
		AICon->SetFocus(Player, EAIFocusPriority::Gameplay);
	}
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	GetWorldTimerManager().SetTimer(ProbeStartleTimerHandle, this, &ABaseMonster::EndProbeStartle, ProbeStartleDuration, false);
}

void ABaseMonster::EndProbeStartle()
{
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->ClearFocus(EAIFocusPriority::Gameplay);
	}

	ProbeStartleTarget = nullptr;
}

// Background "open eyes in sight = attack" rule — see PendingChaseAttackTarget's own header comment for
// the full design. Called from CheckForPlayer()'s CatchDistance branch once light-protection/Moonshine
// i-frames have already been ruled out there.
void ABaseMonster::TryStartChaseAttack(ABaseCharacter* Player)
{
	// PendingEyePeekAttackTarget.IsValid(): belt-and-braces mutual exclusion with Eye Peek Risk — in
	// practice this can't actually happen, since IsPlayerVisible() (what got CheckForPlayer() into this
	// branch in the first place) requires eyes OPEN, while Eye Peek Risk's own swing only ever plays while
	// the player is still nominally eyes-CLOSED (see TryTriggerEyePeekEncounter()'s own entry gate) — but
	// checked explicitly anyway, the same "never resolve each other's swing" guarantee OnAttackHitNotify()
	// below also enforces, rather than relying purely on that invariant holding forever.
	if (!IsValid(Player) || PendingEyePeekAttackTarget.IsValid() || PendingChaseAttackTarget.IsValid())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: caught %s open-eyed at CatchDistance — swinging"), *GetName(), *Player->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(330, 3.0f, FColor::Red,
			FString::Printf(TEXT("%s: CAUGHT — swinging!"), *GetName()));
	}

	// Second Group Aggro call, right before the swing starts (the first already fired when Chase began,
	// above in CheckForPlayer()) — so monsters recruited by THIS call are already moving in by the time
	// the swing actually lands, on top of whichever recruits the first call already sent this way.
	BroadcastAlert(Player->GetActorLocation());

	// Stop navigating before playing the swing — same reasoning as ResolveEyePeekReaction()'s "swing
	// blind" branch and OnKillPlayer()'s own StopMovement()/StopMovementImmediately() call.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
	}
	if (UCharacterMovementComponent* MonsterMoveComp = GetCharacterMovement())
	{
		MonsterMoveComp->StopMovementImmediately();
	}

	// TODO (Phase 1.3 — Redneck Moonshine hook, not wired up yet): a future Moonshine-buffed counter/
	// escape option (beyond the plain crouch-dodge DoesEyePeekAttackConnect() already checks) would slot
	// in right here, before PendingChaseAttackTarget commits this swing to the ordinary dodge/hit
	// resolution below — e.g. consuming the buff to no-sell this specific hit, parallel to how
	// ABaseCharacter::IsImmuneToMonsterCatch() already no-sells the Rage Dash's own physical ram contact.

	bChaseAttackTargetWasCrouchedAtSwingStart = Player->IsCrouched();
	PendingChaseAttackTarget = Player;
	PlayAttackAnimation();

	// FALLBACK — same idempotent role as EyePeekAttackHitFallbackTimerHandle (see its own comment):
	// guarantees OnAttackHitNotify() still fires even if the real "Attack_Hit" AnimNotify never does.
	// Separate timer/delay from Eye Peek Risk's own so the two attack sources never clear or re-arm each
	// other's fallback.
	GetWorldTimerManager().SetTimer(ChaseAttackHitFallbackTimerHandle, this, &ABaseMonster::OnAttackHitNotify,
		ChaseAttackHitFallbackDelay, false);
}

// "Piling on" — see this function's own header comment for the full design. Called from CheckForPlayer()'s
// own standalone panic-strike check (deliberately NOT inside the bHasDetectedPlayer block — see that
// check's own comment).
void ABaseMonster::TryStartPanicStrike(ABaseCharacter* Player)
{
	if (!IsValid(Player) || PendingPanicStrikeTarget.IsValid())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: reached already-panicking %s — swinging (one attempt this episode)"),
		*GetName(), *Player->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(346, 3.0f, FColor::Red,
			FString::Printf(TEXT("%s: PILING ON — swinging!"), *GetName()));
	}

	// Stop navigating before playing the swing — same reasoning as TryStartChaseAttack()'s/
	// TryStartNaskokAttack()'s own equivalent.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
	}
	if (UCharacterMovementComponent* MonsterMoveComp = GetCharacterMovement())
	{
		MonsterMoveComp->StopMovementImmediately();
	}

	// Deliberately does NOT BroadcastAlert() here — same reasoning as TryStartNaskokAttack()'s own swing
	// start: don't cascade a fresh alert from every monster that piles on, on top of whatever alert
	// already brought them here.
	PendingPanicStrikeTarget = Player;
	PlayAttackAnimation();

	// FALLBACK — same idempotent role as the other three sources' own fallback timers (see their
	// comments). Reuses ChaseAttackHitFallbackDelay, same as NaskokAttackHitFallbackTimerHandle already
	// does — no new tunable needed.
	GetWorldTimerManager().SetTimer(PanicStrikeHitFallbackTimerHandle, this, &ABaseMonster::OnAttackHitNotify,
		ChaseAttackHitFallbackDelay, false);
}

// Timer callback for PanicStrikeRearmTimerHandle — see PanicStrikeRearmDelay's own comment for why the
// re-swing after a confirmed hit needed delaying instead of firing immediately from OnAttackHitNotify().
void ABaseMonster::TryStartPanicStrikeIfStillEligible(TWeakObjectPtr<ABaseCharacter> WeakPlayer)
{
	ABaseCharacter* Player = WeakPlayer.Get();
	if (!IsValid(Player) || Player->bIsDead)
	{
		return;
	}
	TryStartPanicStrike(Player);
}

// Called by ABaseCharacter::EnterPanicState() on the monster that just delivered the FIRST hit of a panic
// episode — see this function's own header comment for why one small hook here covers all five separate
// call sites that can start a panic episode instead of teaching each of them individually.
void ABaseMonster::ArmSelfPilingOnCooldown()
{
	PanicStrikeEligibleTime = GetWorld() ? GetWorld()->GetTimeSeconds() + PanicStrikeRearmDelay : 0.0f;
}

void ABaseMonster::TryTriggerEyePeekEncounter(ABaseCharacter* Player)
{
	if (bHasActiveEyePeekEncounter || !IsValid(Player))
	{
		return;
	}
	bHasActiveEyePeekEncounter = true;

	UE_LOG(LogTemp, Warning, TEXT("%s: caught %s peeking (RMB) in sight cone — closing in (need within %.0f units to swing)"),
		*GetName(), *Player->GetName(), EyePeekAttackRange);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(314, EyePeekMaxChaseDuration, FColor::Orange,
			FString::Printf(TEXT("%s: EYE PEEK RISK — spotted while peeking!"), *GetName()));
	}

	// "Group Aggro" restored: every other living ABaseMonster within AlertRadius gets ReceiveAlert()'d
	// and converges on the spot Player was actually spotted at (same Call Pack mechanism the physical
	// blind-touch encounter and a mouth-gasp already use — see AlertRadius's own comment) — not just this
	// one monster chasing alone. See this function's own header comment for why no separate "recruited
	// monsters attack too" system is needed on top of this: a recruit that gets close enough will
	// naturally re-detect the still-peeking player through its OWN CheckForPlayer() and run its own
	// independent encounter from there.
	BroadcastAlert(Player->GetActorLocation());

	// "Подбегает к игроку" — runs toward the player for as long as ResolveEyePeekReaction() below keeps
	// polling (up to EyePeekMaxChaseDuration), same MoveToActor() call the ordinary open-eyes Chase state
	// uses. MoveToActor() targets the actor itself (not a snapshotted point), so this single call keeps
	// self-updating toward Player's CURRENT location every tick on its own — no need to reissue it from
	// the poll.
	EyePeekChaseStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		RequestMoveToActor(AICon, Player, EyePeekAttackRange * 0.5f);
		AICon->SetFocus(Player);
	}

	// REPEATING (not one-shot) — see ResolveEyePeekReaction()'s own comment on why the swing must wait
	// for actual proximity, checked again every EyePeekReactionDelay seconds, rather than firing after a
	// single fixed windup regardless of how far the chase still has to go.
	TWeakObjectPtr<ABaseCharacter> WeakPlayer = Player;
	GetWorldTimerManager().SetTimer(EyePeekReactionTimerHandle,
		FTimerDelegate::CreateUObject(this, &ABaseMonster::ResolveEyePeekReaction, WeakPlayer),
		EyePeekReactionDelay, true);
}

void ABaseMonster::ResolveEyePeekReaction(TWeakObjectPtr<ABaseCharacter> WeakPlayer)
{
	ABaseCharacter* Player = WeakPlayer.Get();
	if (!IsValid(Player) || Player->bIsDead || Health <= 0.0f)
	{
		bHasActiveEyePeekEncounter = false;
		GetWorldTimerManager().ClearTimer(EyePeekReactionTimerHandle);
		return;
	}

	// Distance gate: the swing must not launch until this monster is actually within melee range — the
	// bug this fixes is a fixed windup timer letting the swing fire from wherever the chase happened to
	// be, including well outside EyePeekAttackRange. While still out of range, do nothing and let THIS
	// SAME repeating timer poll again next interval, UNLESS the chase has dragged on past
	// EyePeekMaxChaseDuration — in which case give up entirely rather than pursuing forever.
	const float DistanceToPlayer = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
	if (DistanceToPlayer > EyePeekAttackRange)
	{
		const float ElapsedChaseTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f) - EyePeekChaseStartTime;
		if (ElapsedChaseTime <= EyePeekMaxChaseDuration)
		{
			// Still closing in — no-op this poll, keep waiting for the next one.
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("%s: gave up chasing %s — never got within %.0f units after %.1fs"),
			*GetName(), *Player->GetName(), EyePeekAttackRange, ElapsedChaseTime);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(319, 3.0f, FColor::Silver, TEXT("Lost the peeking player — gave up the chase"));
		}

		bHasActiveEyePeekEncounter = false;
		GetWorldTimerManager().ClearTimer(EyePeekReactionTimerHandle);
		if (AAIController* AICon = Cast<AAIController>(GetController()))
		{
			AICon->StopMovement();
		}
		return;
	}

	// Actually within range now — stop polling and resolve the reaction matrix below.
	bHasActiveEyePeekEncounter = false;
	GetWorldTimerManager().ClearTimer(EyePeekReactionTimerHandle);

	if (Player->GetIsAiming())
	{
		// (a) Still peeking — "eyes open" at the moment the blind swing lands: Fatality. Same
		// PlayAttackAnimation() + OnKillPlayer() path CheckForPlayer()'s own catch-range kill uses.
		// Warning (not Error) severity, even though this is the "kill" branch — this is an intentional,
		// expected gameplay outcome, not a bug condition; UE's automation framework treats any Error-
		// severity log emitted during a test as an automatic failure, which would otherwise fail
		// FEyePeekRiskReactionTest's own Fatality scenario even though the outcome itself is correct.
		UE_LOG(LogTemp, Warning, TEXT("%s: %s never broke eye contact — FATALITY"), *GetName(), *Player->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(315, 3.0f, FColor::Red, TEXT("EYES OPEN — CAUGHT PEEKING!"));
		}
		StartEyePeekAttackAnimDebug();
		PlayAttackAnimation();
		OnKillPlayer(Player);
		return;
	}

	// (b) Released RMB in time — eyes closed, swings blind regardless of whether the player is standing
	// or already crouched RIGHT NOW. This function's ONLY job is to stop navigating and start the
	// Anim_Whisper_Attack1 swing (PlayAttackAnimation(), same animation/slot the Fatality branch above
	// and CheckForPlayer()'s ordinary catch-range kill both use) — it deliberately applies NO
	// connect/miss decision of its own (no crouch check, no range check, no
	// Player->EnterPanicState()/BroadcastAlert() call). That used to be a bug: checking IsCrouched()
	// and/or calling EnterPanicState() synchronously the instant this function ran meant the outcome was
	// decided the moment the sight-cone check caught the player — potentially SightDistance units away,
	// and before the player had even finished reacting — with no check of where the player actually was,
	// or whether they'd crouched in the meantime, once the swing would have reached them. The ENTIRE
	// connect/miss decision (crouch AND proximity, both re-checked at the swing's actual point of
	// contact) now happens in OnAttackHitNotify() (fired by the "Attack_Hit" AnimNotify on the animation
	// itself) — see PendingEyePeekAttackTarget's own comment and DoesEyePeekAttackConnect().
	UE_LOG(LogTemp, Warning, TEXT("%s: %s closed their eyes — swinging blind"), *GetName(), *Player->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(316, 3.0f, FColor::Yellow, TEXT("SWINGING BLIND — hold still or drop low!"));
	}

	// Stop navigating BEFORE playing the swing — same reasoning as OnKillPlayer()'s own StopMovement()/
	// StopMovementImmediately() call: PlayAttackAnimation() plays through an additive Slot node layered
	// OVER the locomotion state machine (see its own header comment), so it would keep playing to
	// completion regardless either way, but a monster visibly sliding mid-stride while its swing anim
	// plays reads as broken — this is purely about making the swing look like a deliberate stop-and-hit,
	// not a fix for the animation itself being cut off.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
	}
	if (UCharacterMovementComponent* MonsterMoveComp = GetCharacterMovement())
	{
		MonsterMoveComp->StopMovementImmediately();
	}

	PendingEyePeekAttackTarget = Player;
	StartEyePeekAttackAnimDebug();
	PlayAttackAnimation();

	// FALLBACK — see EyePeekAttackHitFallbackDelay's own comment: guarantees OnAttackHitNotify() (and so
	// the crouch/proximity connect check + Player->EnterPanicState()/BroadcastAlert()) still fires even
	// if the real "Attack_Hit" AnimNotify never does. Harmless if the real notify DOES fire first —
	// OnAttackHitNotify() clears PendingEyePeekAttackTarget on its first call, so whichever of the two
	// arrives second just finds it already null.
	GetWorldTimerManager().SetTimer(EyePeekAttackHitFallbackTimerHandle, this, &ABaseMonster::OnAttackHitNotify,
		EyePeekAttackHitFallbackDelay, false);
}

void ABaseMonster::StartEyePeekAttackAnimDebug()
{
	bIsPlayingEyePeekAttackAnim = true;
	GetWorldTimerManager().SetTimer(EyePeekAttackDebugTimerHandle, this, &ABaseMonster::ClearEyePeekAttackAnimDebug,
		EyePeekAttackDebugDuration, false);
}

void ABaseMonster::ClearEyePeekAttackAnimDebug()
{
	bIsPlayingEyePeekAttackAnim = false;
}

bool ABaseMonster::DoesEyePeekAttackConnect(const ABaseCharacter* Player) const
{
	if (!IsValid(Player))
	{
		return false;
	}

	// Слепое уклонение — эта функция вызывается только для веток, где сама суть в том, что игрок НЕ
	// видит замах (Eye Peek Risk "swing blind"). Присел = увернулся, независимо от дистанции — см.
	// DoesOpenEyesAttackConnect() для варианта с открытыми глазами, где это правило другое.
	if (Player->IsCrouched())
	{
		return false;
	}

	return IsPlayerWithinAttackContactRange(Player);
}

bool ABaseMonster::DoesOpenEyesAttackConnect(const ABaseCharacter* Player, bool bTargetWasCrouchedAtSwingStart) const
{
	if (!IsValid(Player))
	{
		return false;
	}

	if (Player->IsEyesClosed())
	{
		// Глаза уже закрыты к моменту проверки (закрыл во время замаха) — откат на обычное слепое
		// правило вместо привязки к высоте.
		if (Player->IsCrouched())
		{
			return false;
		}
	}
	else
	{
		// Глаза открыты: присед сам по себе не спасает — игрок одинаково может получить удар что стоя,
		// что сидя, ПОКА его текущая стойка совпадает с той, на которую замах был нацелен в момент
		// TryStartChaseAttack()/TryStartNaskokAttack(). Смена стойки МЕЖДУ началом замаха и этой
		// проверкой (присел во время замаха, или наоборот встал) уводит именно этот замах мимо — см.
		// собственный комментарий этой функции про следующий замах.
		if (Player->IsCrouched() != bTargetWasCrouchedAtSwingStart)
		{
			return false;
		}
	}

	return IsPlayerWithinAttackContactRange(Player);
}

bool ABaseMonster::IsPlayerWithinAttackContactRange(const ABaseCharacter* Player) const
{
	if (!IsValid(Player))
	{
		return false;
	}

	// "Перед лапой/грудью", не по центру капсулы монстра — EyePeekAttackForwardOffset выносит точку
	// проверки вперёд, представляя достижимость замаха, а не сферу, включающую собственные ноги монстра.
	FVector HitCenter = GetActorLocation() + GetActorForwardVector() * EyePeekAttackForwardOffset;
	// Раньше высота бралась от самого монстра — это ломало проверку, как только цель оказывалась заметно
	// выше/ниже него (наездница на маунте, со временем возможно и другие случаи с разницей высот). Теперь
	// высота проверки всегда подстраивается под фактическую высоту игрока, а не под рост монстра.
	HitCenter.Z = Player->GetActorLocation().Z;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EyePeekAttackHit), false);
	QueryParams.AddIgnoredActor(this);

	const bool bAnyOverlap = GetWorld()->OverlapMultiByObjectType(Overlaps, HitCenter, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(EyePeekAttackRange), QueryParams);

	bool bHitPlayer = false;
	if (bAnyOverlap)
	{
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (Overlap.GetActor() == Player)
			{
				bHitPlayer = true;
				break;
			}
		}
	}

	return bHitPlayer;
}

void ABaseMonster::OnAttackHitNotify()
{
	// Cleared here (not just left to expire) so whichever of the real AnimNotify or either pending
	// source's own fallback timer fires SECOND is guaranteed not to also fire — belt-and-braces on top of
	// the PendingEyePeekAttackTarget/PendingChaseAttackTarget .Reset() calls below already making a
	// second call a no-op.
	GetWorldTimerManager().ClearTimer(EyePeekAttackHitFallbackTimerHandle);
	GetWorldTimerManager().ClearTimer(ChaseAttackHitFallbackTimerHandle);
	GetWorldTimerManager().ClearTimer(NaskokAttackHitFallbackTimerHandle);
	GetWorldTimerManager().ClearTimer(PanicStrikeHitFallbackTimerHandle);
	GetWorldTimerManager().ClearTimer(PanicStrikeRearmTimerHandle);

	// Eye Peek Risk's own swing — checked and resolved FIRST, exactly as before this function also learned
	// to handle PendingChaseAttackTarget below; nothing in this branch changed. See
	// PendingEyePeekAttackTarget's own comment.
	if (PendingEyePeekAttackTarget.IsValid())
	{
		ABaseCharacter* Player = PendingEyePeekAttackTarget.Get();
		PendingEyePeekAttackTarget.Reset();

		// No-ops if the player went invalid/died in the meantime, or Health hit 0 (this monster died mid-
		// swing) — same guard as before.
		if (!IsValid(Player) || Player->bIsDead || Health <= 0.0f)
		{
			return;
		}

		if (!DoesEyePeekAttackConnect(Player))
		{
			// Either the player crouched in time, or they moved out of range between
			// ResolveEyePeekReaction() starting the swing and the animation's actual point of contact — this
			// is the fix in action: no more telepathic stun, and no more ignoring a last-second crouch.
			UE_LOG(LogTemp, Log, TEXT("%s: blind swing connected on empty air — %s dodged or was out of range at contact"),
				*GetName(), *Player->GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(318, 3.0f, FColor::Green, TEXT("Swing missed — dodged or out of range"));
			}
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("%s: blind swing connects on %s at the moment of contact (PANIC)"),
			*GetName(), *Player->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(316, 3.0f, FColor::Red, TEXT("HIT BLIND — knocked down, stay calm!"));
		}

		// Captured BEFORE ApplyDamage(): if the player is mounted right now, ApplyDamage() ->
		// ABaseCharacter::TakeDamage() -> OnKnockedOffMount() -> Dismount() all run SYNCHRONOUSLY inside
		// this call, so Player->IsMounted() would already read false by the time we'd check it afterward —
		// this is the only way to know "was this hit what knocked them off" versus "were they already on
		// foot".
		const bool bWasMountedBeforeHit = Player->IsMounted();

		// Health damage — see ChaseAttackDamage's own comment for why this is purely additive to
		// EnterPanicState() below: if this hit alone drops Health to 0, TakeDamage() -> HandleDeath()
		// already sets bIsDead, so the EnterPanicState() call right after this safely no-ops via its own
		// bIsDead guard instead of starting a knockdown mini-game for a player who's already dead.
		UGameplayStatics::ApplyDamage(Player, ChaseAttackDamage, GetInstigatorController(), this, UDamageType::StaticClass());

		// A hit that knocks the player off a mount already carries its own knockdown+knockback
		// (OnKnockedOffMount() -> UMountRiderComponent::HandleKnockedOffMount(), triggered synchronously by
		// ApplyDamage() above) — piling the full panic mini-game on top of that reads as a double
		// punishment for the same hit. Not mounted (or wasn't mounted before this hit) — unchanged, panic
		// fires exactly as before.
		if (!bWasMountedBeforeHit)
		{
			Player->EnterPanicState(this);
		}
		BroadcastAlert(GetActorLocation());
		return;
	}

	// Background "open eyes in sight = attack" rule's own swing (TryStartChaseAttack()) — reuses the exact
	// same crouch+overlap connect check as Eye Peek Risk above, but routes its OWN outcomes: a dodge here
	// sends this monster into Scanning (EnterScanningState()) instead of falling straight back to
	// Patrol/Chase, rather than just missing silently — see PendingChaseAttackTarget's own comment.
	if (PendingChaseAttackTarget.IsValid())
	{
		ABaseCharacter* Player = PendingChaseAttackTarget.Get();
		PendingChaseAttackTarget.Reset();

		if (!IsValid(Player) || Player->bIsDead || Health <= 0.0f)
		{
			return;
		}

		if (!DoesOpenEyesAttackConnect(Player, bChaseAttackTargetWasCrouchedAtSwingStart))
		{
			UE_LOG(LogTemp, Log, TEXT("%s: open-eyes swing connected on empty air — %s dodged or was out of range at contact"),
				*GetName(), *Player->GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(331, 3.0f, FColor::Green, TEXT("Dodged! (crouched in time)"));
			}

			// Miss doesn't drop straight back to Patrol/Chase — stays riled up/searching the area for
			// ScanDuration first, same treatment a Blind Encounter dodge/miss already gets.
			EnterScanningState(Player);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("%s: open-eyes swing connects on %s at the moment of contact (PANIC)"),
			*GetName(), *Player->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(332, 3.0f, FColor::Red, TEXT("CAUGHT — knocked down!"));
		}

		// Captured BEFORE ApplyDamage() — see the matching comment in the Eye Peek hit branch above for why
		// (mount dismount runs synchronously inside ApplyDamage(), so IsMounted() would already read false
		// afterward).
		const bool bWasMountedBeforeHit = Player->IsMounted();

		// Health damage — see ChaseAttackDamage's own comment (same reasoning as the Eye Peek hit branch
		// above: purely additive to EnterPanicState() below).
		UGameplayStatics::ApplyDamage(Player, ChaseAttackDamage, GetInstigatorController(), this, UDamageType::StaticClass());

		// Same "don't double-punish a mount knockdown with a full panic mini-game too" reasoning as the Eye
		// Peek hit branch above.
		if (!bWasMountedBeforeHit)
		{
			Player->EnterPanicState(this);
		}
		return;
	}

	// Naskok's own swing (TryStartNaskokAttack()'s "first contact" branch) — same crouch+overlap connect
	// check as the two branches above, but its own outcomes: a dodge here routes into EnterScanningState()
	// with NO alert (a miss never calls the pack), a hit applies damage + panic AND ONLY THEN
	// BroadcastAlert() — deliberately different from TryStartChaseAttack(), which alerts unconditionally
	// the instant the swing starts. See PendingNaskokAttackTarget's own comment.
	if (PendingNaskokAttackTarget.IsValid())
	{
		ABaseCharacter* Player = PendingNaskokAttackTarget.Get();
		PendingNaskokAttackTarget.Reset();

		if (!IsValid(Player) || Player->bIsDead || Health <= 0.0f)
		{
			return;
		}

		if (!DoesOpenEyesAttackConnect(Player, bNaskokAttackTargetWasCrouchedAtSwingStart))
		{
			UE_LOG(LogTemp, Log, TEXT("%s: Naskok swing connected on empty air — %s dodged or was out of range at contact"),
				*GetName(), *Player->GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(343, 3.0f, FColor::Green, TEXT("Dodged the ram! (crouched in time)"));
			}

			// Miss doesn't drop straight back to Patrol/Chase — stays riled up/searching the area for
			// ScanDuration first, same treatment the Blind Encounter and background Chase-attack dodges
			// already get. No BroadcastAlert() here — see this branch's own comment above.
			EnterScanningState(Player);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("%s: Naskok swing connects on %s at the moment of contact (PANIC)"),
			*GetName(), *Player->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(344, 3.0f, FColor::Red, TEXT("RAMMED — knocked down!"));
		}

		// Same "captured before ApplyDamage()" mount-dismount reasoning as the two branches above.
		const bool bWasMountedBeforeHit = Player->IsMounted();

		UGameplayStatics::ApplyDamage(Player, ChaseAttackDamage, GetInstigatorController(), this, UDamageType::StaticClass());

		if (!bWasMountedBeforeHit)
		{
			Player->EnterPanicState(this);
		}

		// Alert fires HERE, on a CONFIRMED hit only — deliberately NOT on swing start the way
		// TryStartNaskokAttack() itself never alerts either. See this branch's own header comment.
		BroadcastAlert(GetActorLocation());
		return;
	}

	// "Piling on" (TryStartPanicStrike()'s swing) — a monster already investigating this alert or already
	// Chasing this player, reaching an already-panicking player at CatchDistance. Different connect rule
	// from the three branches above: eyes CLOSED and a genuine dodge (crouched or moved out of range) is
	// the ONLY way to avoid this — eyes open connects regardless of the body (a panicking player can't
	// meaningfully dodge with their eyes open either way), and eyes closed but NOT dodged (no crouch, still
	// in range) also connects. A HIT does NOT retire this monster from the episode — it calls
	// BroadcastAlert() (a hit is meant to cascade a fresh wave, same as every other confirmed-hit branch)
	// and immediately re-arms TryStartPanicStrike() on itself to swing again, for as long as the player
	// stays down and in range; only a genuine DODGE sets bHasMissedPanicStrikeThisEpisode and retires this
	// monster for the rest of the episode.
	if (PendingPanicStrikeTarget.IsValid())
	{
		ABaseCharacter* Player = PendingPanicStrikeTarget.Get();
		PendingPanicStrikeTarget.Reset();

		if (!IsValid(Player) || Player->bIsDead || Health <= 0.0f)
		{
			return;
		}

		// Combat-stealth rules DRAFT, section 6 (final): the eyes-only dodge applies for as long as the
		// player is ACTUALLY still physically down — either bIsPanicking (the Nose-Breathing mini-game is
		// genuinely running, PlayKnockdownAnimation() still has them on the ground) OR bIsAwaitingGetUp
		// (the mini-game was already WON, but RecoverFromKnockdown() hasn't run yet — the player is still
		// lying there waiting on a forward press; see that field's own comment on ABaseCharacter). There's
		// no "standing, could crouch out of the way" body to check in either state, so
		// DoesEyePeekAttackConnect() is skipped for both. But the standalone "piling on" eligibility check
		// in CheckForPlayer() deliberately keeps this episode reachable even AFTER the player has actually
		// stood back up (RecoverFromKnockdown() has run, both flags false) — a player caught by this branch
		// while genuinely standing is exactly the freshly-caught, STANDING case DoesEyePeekAttackConnect()
		// exists for everywhere else (ChaseAttack/Naskok/Eye Peek), so the ordinary crouch-dodge table
		// applies to them too. That function itself is untouched either way — only which branch calls it
		// differs.
		const bool bIsPhysicallyDown = Player->bIsPanicking || Player->bIsAwaitingGetUp;
		const bool bDodged = bIsPhysicallyDown
			? Player->IsEyesClosed()
			: (Player->IsEyesClosed() && !DoesEyePeekAttackConnect(Player));
		if (bDodged)
		{
			UE_LOG(LogTemp, Log, TEXT("%s: piling-on swing connected on empty air — %s closed their eyes in time"),
				*GetName(), *Player->GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(347, 3.0f, FColor::Green, TEXT("Dodged the pile-on! (eyes closed in time)"));
			}

			// ONLY a dodge retires this monster from the episode — see bHasMissedPanicStrikeThisEpisode's
			// own comment.
			bHasMissedPanicStrikeThisEpisode = true;

			// Longer, dedicated search window (PanicStrikeScanDuration) rather than the ordinary
			// ScanDuration every other dodge gets — see that property's own comment.
			EnterScanningState(Player, PanicStrikeScanDuration);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("%s: piling-on swing connects on %s at the moment of contact"),
			*GetName(), *Player->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(348, 3.0f, FColor::Red, TEXT("PILED ON — hit!"));
		}

		// Combat-stealth rules DRAFT, section 6 (final): a piling-on swing landing on OPEN eyes is the
		// actual kill trigger now — the player is being caught wide awake, mid-panic, by a second (or
		// third...) monster, not just taking another tick of Health. Player->bIsPanicking is what actually
		// distinguishes "the Nose-Breathing mini-game is genuinely still running right now" from the
		// (practically unreachable today, kept as a future-1.2f safety net) case of this branch somehow
		// still resolving against a player whose panic already ended some other way.
		if (Player->bIsPanicking)
		{
			// PlayAttackAnimation() already played when TryStartPanicStrike() started this swing — same
			// "already started via PlayAttackAnimation() at the call site" reasoning OnKillPlayer()'s own
			// comment gives for the ordinary catch-range kill and the Eye Peek Fatality branch, both of
			// which route through this exact same function.
			OnKillPlayer(Player);
			return;
		}

		UGameplayStatics::ApplyDamage(Player, ChaseAttackDamage, GetInstigatorController(), this, UDamageType::StaticClass());

		// Panic already ended some other way but this branch still resolved against the player anyway (see
		// this block's own comment) — treat it like a fresh catch: damage, then a brand new panic episode.
		// EnterPanicState() itself calls ArmSelfPilingOnCooldown() on `this`, so a stale
		// PanicStrikeRearmTimerHandle re-arm from here would be redundant — that whole mechanism is what
		// paces this monster's NEXT attempt now, not a re-arm from this specific branch.
		Player->EnterPanicState(this);
		return;
	}

	// No pending target of any kind — this playback belongs to some OTHER caller (the Eye Peek Fatality
	// branch and any other still-synchronous OnKillPlayer() path, none of which ever set any Pending*
	// field) — no-op, same as before.
}

void ABaseMonster::ResolveBlindEncounterReaction(TWeakObjectPtr<ABaseCharacter> WeakPlayer)
{
	ABaseCharacter* Player = WeakPlayer.Get();
	if (!IsValid(Player) || Player->bIsDead || !Player->IsEyesClosed())
	{
		// Player died, opened their eyes (already resolved via OpenEyes()'s own instant-kill check), or is
		// otherwise gone — Scanning already started in OnCapsuleHit() above, so tear it down the same way
		// its own timeout would (EndScanningState() clears bHasActiveBlindEncounter too).
		GetWorldTimerManager().ClearTimer(ScanTimerHandle);
		EndScanningState();
		return;
	}

	const bool bStillPushing = Player->GetVelocity().SizeSquared() > FMath::Square(BlindTouchPushSpeedThreshold);

	UE_LOG(LogTemp, Warning, TEXT("%s: blind encounter reaction — %s"), *GetName(), bStillPushing ? TEXT("Scenario B (panic)") : TEXT("Scenario A (scan continues)"));

	if (bStillPushing)
	{
		// Scenario B: Scanning (already active since OnCapsuleHit()) stays active for the monster's own
		// visual "locked on, searching" pose, but its own ScanDuration timeout must NOT fire mid-panic and
		// tear down BlindEncounterMonster out from under a mini-game that can easily outlast ScanDuration —
		// OnBlindEncounterResolved() (called by EndPanicSafely()/ForceEyesOpenFromPanic()'s survival
		// branch) does that same EndScanningState() cleanup once the panic mini-game itself actually ends.
		GetWorldTimerManager().ClearTimer(ScanTimerHandle);
		Player->EnterPanicState(this);
	}
	// Scenario A: nothing further to do here — Scanning is already running and resolves itself normally
	// via ScanTimerHandle -> EndScanningState() (or ResolveBlindEncounterKill() if eyes reopen first).
}

void ABaseMonster::EnterScanningState(ABaseCharacter* Player, float OverrideScanDuration)
{
	if (!IsValid(Player))
	{
		bHasActiveBlindEncounter = false;
		return;
	}

	bIsScanning = true;
	CurrentScanTarget = Player;
	Player->BlindEncounterMonster = this;

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
		AICon->SetFocus(Player);
	}
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	const float ActualScanDuration = (OverrideScanDuration > 0.0f) ? OverrideScanDuration : ScanDuration;
	GetWorldTimerManager().SetTimer(ScanTimerHandle, this, &ABaseMonster::EndScanningState, ActualScanDuration, false);
}

void ABaseMonster::EndScanningState()
{
	bIsScanning = false;
	bHasActiveBlindEncounter = false;

	if (ABaseCharacter* Player = CurrentScanTarget.Get())
	{
		if (Player->BlindEncounterMonster.Get() == this)
		{
			Player->BlindEncounterMonster.Reset();
		}
	}
	CurrentScanTarget.Reset();

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->ClearFocus(EAIFocusPriority::Gameplay);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: lost interest — eyes stayed closed through the whole scan"), *GetName());
}

void ABaseMonster::ResolveBlindEncounterKill(ABaseCharacter* Player)
{
	bIsScanning = false;
	bHasActiveBlindEncounter = false;
	CurrentScanTarget.Reset();
	GetWorldTimerManager().ClearTimer(ScanTimerHandle);

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->ClearFocus(EAIFocusPriority::Gameplay);
	}

	PlayAttackAnimation();
	OnKillPlayer(Player);
}

void ABaseMonster::BroadcastAlert(const FVector& AlertLocation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> AllMonsters;
	UGameplayStatics::GetAllActorsOfClass(World, ABaseMonster::StaticClass(), AllMonsters);

	UE_LOG(LogTemp, Warning, TEXT("%s: BroadcastAlert() — Total monsters on map: %d"), *GetName(), AllMonsters.Num());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
			FString::Printf(TEXT("[PACK CALL] %s — Total monsters on map: %d"), *GetName(), AllMonsters.Num()));
	}

	// Gather in-radius candidates first — dead/invalid ones are KEPT here (only self and non-monster
	// actors are dropped) so the per-monster debug readout below can report "Dead" as an explicit
	// rejection reason instead of silently omitting them from the count entirely.
	struct FAlertCandidate
	{
		ABaseMonster* Monster;
		float DistanceCm;
	};
	TArray<FAlertCandidate> Candidates;
	const float AlertRadiusSq = FMath::Square(AlertRadius);
	for (AActor* Actor : AllMonsters)
	{
		ABaseMonster* OtherMonster = Cast<ABaseMonster>(Actor);
		if (!IsValid(OtherMonster) || OtherMonster == this)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(OtherMonster->GetActorLocation(), AlertLocation);
		if (DistSq > AlertRadiusSq)
		{
			continue;
		}
		Candidates.Add({OtherMonster, FMath::Sqrt(DistSq)});
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Monsters in radius (%.0f units): %d"), *GetName(), AlertRadius, Candidates.Num());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
			FString::Printf(TEXT("[PACK CALL] Monsters in radius (%.0fm): %d"), AlertRadius / 100.0f, Candidates.Num()));
	}

	// Pack Formation on Arrival: only LIVING candidates get a ring slot, sorted by their current bearing
	// around AlertLocation so the evenly-spaced slots ReceiveAlert() assigns below roughly match each
	// monster's own approach direction — see BroadcastAlert()'s own header comment for the full
	// reasoning/caveats.
	TArray<ABaseMonster*> Recipients;
	for (const FAlertCandidate& Candidate : Candidates)
	{
		if (Candidate.Monster->Health > 0.0f && !Candidate.Monster->bIsDead)
		{
			Recipients.Add(Candidate.Monster);
		}
	}
	Recipients.Sort([&AlertLocation](const ABaseMonster& A, const ABaseMonster& B)
	{
		const FVector2D OffsetA(A.GetActorLocation() - AlertLocation);
		const FVector2D OffsetB(B.GetActorLocation() - AlertLocation);
		return FMath::Atan2(OffsetA.Y, OffsetA.X) < FMath::Atan2(OffsetB.Y, OffsetB.X);
	});

	for (const FAlertCandidate& Candidate : Candidates)
	{
		ABaseMonster* OtherMonster = Candidate.Monster;
		const float DistanceMeters = Candidate.DistanceCm / 100.0f;

		FString Reason;
		bool bAccepted = false;
		if (OtherMonster->Health <= 0.0f || OtherMonster->bIsDead)
		{
			Reason = TEXT("Dead");
		}
		else
		{
			const int32 SlotIndex = Recipients.IndexOfByKey(OtherMonster);
			bAccepted = OtherMonster->ReceiveAlert(AlertLocation, SlotIndex, Recipients.Num(), Reason);
		}

		if (bAccepted)
		{
			UE_LOG(LogTemp, Warning, TEXT("  -> %s: Distance %.1f m — ReceiveAlert: ACCEPTED"), *OtherMonster->GetName(), DistanceMeters);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
					FString::Printf(TEXT("  %s: %.1fm — ACCEPTED"), *OtherMonster->GetName(), DistanceMeters));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  -> %s: Distance %.1f m — ReceiveAlert: REJECTED (%s)"), *OtherMonster->GetName(), DistanceMeters, *Reason);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Orange,
					FString::Printf(TEXT("  %s: %.1fm — REJECTED (%s)"), *OtherMonster->GetName(), DistanceMeters, *Reason));
			}
		}
	}
}

EPathFollowingRequestResult::Type ABaseMonster::RequestMoveToActor(AAIController* AICon, AActor* Goal, float AcceptanceRadius, bool bStopOnOverlap,
	bool bUsePathfinding, bool bCanStrafe, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPath) const
{
	// See this function's own header comment: bIsBeingRidden is checked BEFORE AICon is touched at all,
	// so a ridden monster never issues a nav-move request in the first place.
	if (bIsBeingRidden || !IsValid(AICon))
	{
		return EPathFollowingRequestResult::Failed;
	}
	return AICon->MoveToActor(Goal, AcceptanceRadius, bStopOnOverlap, bUsePathfinding, bCanStrafe, FilterClass, bAllowPartialPath);
}

EPathFollowingRequestResult::Type ABaseMonster::RequestMoveToLocation(AAIController* AICon, const FVector& Dest, float AcceptanceRadius, bool bStopOnOverlap,
	bool bUsePathfinding, bool bProjectDestinationToNavigation, bool bCanStrafe, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPath) const
{
	if (bIsBeingRidden || !IsValid(AICon))
	{
		return EPathFollowingRequestResult::Failed;
	}
	return AICon->MoveToLocation(Dest, AcceptanceRadius, bStopOnOverlap, bUsePathfinding, bProjectDestinationToNavigation, bCanStrafe, FilterClass, bAllowPartialPath);
}

bool ABaseMonster::ReceiveAlert(const FVector& AlertLocation, int32 SlotIndex, int32 SlotCount, FString& OutReason)
{
	// Don't interrupt anything more urgent already happening — only an idle Patrol-state monster diverts
	// to go investigate. Checked individually (rather than one combined condition) so BroadcastAlert()'s
	// debug readout can report exactly WHICH one applied.
	if (bIsStunned) { OutReason = TEXT("Stunned"); return false; }
	if (bIsStaggered) { OutReason = TEXT("Staggered"); return false; }
	if (bIsBeingRidden) { OutReason = TEXT("Being ridden"); return false; }
	// bIsFleeingToShadow alone covers BOTH "still actively running toward cover" AND "arrived, standing
	// safely in shadow" (see its own header comment) — only the former should actually block a pack call.
	// A monster that's already reached safety (bIsIdleInShadow) has nothing left to flee from and should
	// accept the alert and head to its ring position like any other Patrol-state monster.
	if (bIsFleeingToShadow && !bIsIdleInShadow) { OutReason = TEXT("Fleeing to shadow (sun)"); return false; }
	if (bIsScanning) { OutReason = TEXT("Already scanning"); return false; }
	if (bIsInvestigatingAlert) { OutReason = TEXT("Already investigating"); return false; }
	if (State == EMonsterState::Chase) { OutReason = TEXT("Already chasing"); return false; }
	if (State == EMonsterState::Stalk) { OutReason = TEXT("Already stalking"); return false; }
	if (State != EMonsterState::Patrol) { OutReason = TEXT("Not patrolling"); return false; }

	AAIController* AICon = Cast<AAIController>(GetController());
	if (!AICon)
	{
		OutReason = TEXT("No AIController");
		return false;
	}

	AICon->StopMovement();

	// Pack Formation on Arrival: an evenly-spaced point on the InvestigateAcceptanceRadius ring around
	// AlertLocation, not AlertLocation itself — see that property's own comment for why this replaced the
	// old "path to AlertLocation with a big blob-shaped acceptance radius" approach (which let multiple
	// arrivals clump on whichever side got there first instead of surrounding the player).
	const float SlotAngle = (SlotCount > 0) ? (2.0f * PI * SlotIndex) / static_cast<float>(SlotCount) : 0.0f;
	const FVector RingOffset = FVector(FMath::Cos(SlotAngle), FMath::Sin(SlotAngle), 0.0f) * InvestigateAcceptanceRadius;
	const FVector RingTarget = AlertLocation + RingOffset;

	const EPathFollowingRequestResult::Type MoveResult =
		RequestMoveToLocation(AICon, RingTarget, PackFormationTolerance, /*bStopOnOverlap=*/true, /*bUsePathfinding=*/true);
	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		OutReason = TEXT("No NavMesh path");
		return false;
	}

	bIsInvestigatingAlert = true;
	CurrentInvestigateRingTarget = RingTarget;
	InvestigateStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// InvestigateDuration used to start counting down the instant the alert was accepted, giving this
	// monster only InvestigateDuration seconds for the ENTIRE trip across the map — nowhere near enough at
	// PatrolSpeed on real level-sized distances. Instead, poll for actual arrival at the ring point (same
	// repeating-timer style ResolveEyePeekReaction()/EyePeekReactionTimerHandle already uses) and only
	// start the ordinary "hold position, then give up" InvestigateDuration countdown once genuinely there —
	// see CheckInvestigateArrival().
	GetWorldTimerManager().SetTimer(InvestigateArrivalCheckTimerHandle, this, &ABaseMonster::CheckInvestigateArrival,
		InvestigateArrivalPollInterval, true);
	return true;
}

void ABaseMonster::CheckInvestigateArrival()
{
	if (!bIsInvestigatingAlert)
	{
		GetWorldTimerManager().ClearTimer(InvestigateArrivalCheckTimerHandle);
		return;
	}

	if (FVector::Dist(GetActorLocation(), CurrentInvestigateRingTarget) <= PackFormationTolerance)
	{
		GetWorldTimerManager().ClearTimer(InvestigateArrivalCheckTimerHandle);
		GetWorldTimerManager().SetTimer(InvestigateTimerHandle, this, &ABaseMonster::EndInvestigateAlert, InvestigateDuration, false);
		return;
	}

	// Still not there — cap the TRAVEL attempt itself at InvestigateDuration (reused, not a new tunable) so
	// an unreachable ring point (blocked path, NavMesh gap, etc.) can't leave this monster polling forever.
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : InvestigateStartTime;
	if (Now - InvestigateStartTime >= InvestigateDuration)
	{
		GetWorldTimerManager().ClearTimer(InvestigateArrivalCheckTimerHandle);
		EndInvestigateAlert();
	}
}

void ABaseMonster::EndInvestigateAlert()
{
	bIsInvestigatingAlert = false;
	GetWorldTimerManager().ClearTimer(InvestigateArrivalCheckTimerHandle);
}

void ABaseMonster::OnMounted(ACharacter* Rider)
{
	// bIsBeingRidden = true is, on its own, already what pauses BOTH shadow-restricted wander AND
	// combat AI for the whole ride: Tick() checks it first (before ever reaching the Patrol/shadow-wander
	// branch) and returns immediately, and CheckForPlayer() checks it before any Stalk/Chase decision —
	// so a monster that was mid-shadow-wander (or mid-Chase) the instant this fires simply stops deciding
	// anything new next tick, without needing bIsFleeingToShadow/bIsIdleInShadow/State touched here at
	// all. State is still force-reset to Patrol below regardless, same as always, purely to clear any
	// stale Chase/Stalk bookkeeping (see the comment further down) — not because Day/Night state needs it.
	bIsBeingRidden = true;
	CurrentRider = Rider;

	// Clears any active stun from before the mount (e.g. the rider's own held item stunned this monster
	// moments before mounting it) so residual stun state can't linger and interfere with the rider's
	// WASD-driven movement once bIsBeingRidden's own Tick()/CheckForPlayer() gating takes over.
	if (bIsStunned)
	{
		bIsStunned = false;
		GetWorldTimerManager().ClearTimer(StunTimerHandle);
	}

	// Same for a residual stagger (e.g. mounted right after being knocked back) — belt-and-braces so
	// nothing about the launch's suppression window can linger into the ride either.
	if (bIsStaggered)
	{
		bIsStaggered = false;
		GetWorldTimerManager().ClearTimer(StaggerTimerHandle);
	}

	// THE actual fix for state persisting across mount: if this monster was Stalking (State ==
	// EMonsterState::Stalk) at any point — even from well before this specific mount, e.g. a light
	// encounter on a previous ride — MoveToLightBorder() re-arms FlankRescanTimerHandle itself on its
	// own schedule (0.5-1s) and is NOT gated by Tick()/CheckForPlayer() at all, since it's a
	// self-perpetuating timer chain, not something driven from Tick(). MoveToLightBorder() now also
	// checks bIsBeingRidden itself (belt-and-braces), but resetting State here and clearing the timer
	// outright is what stops a fully independent AI move from ever being issued in the first place,
	// rather than relying on that check catching it after the fact.
	State = EMonsterState::Patrol;
	bIsWaiting = false;
	GetWorldTimerManager().ClearTimer(FlankRescanTimerHandle);
	GetWorldTimerManager().ClearTimer(WaitTimerHandle);

	// Stops any AI move already in progress immediately — without this, a Chase/Stalk monster mid-path
	// would keep sliding toward its old destination for a frame or two before Tick()/CheckForPlayer()'s
	// bIsBeingRidden gate (checked at the top of each) gets a chance to matter. ClearFocus() too: a
	// Chase/Stalk episode leaves SetFocus(Player) active, which keeps driving
	// bUseControllerDesiredRotation-based turning toward that stale focus target even after StopMovement()
	// — exactly what was fighting the rider's movement-driven input and causing the rapid on-the-spot
	// spin while mounted.
	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
		AICon->ClearFocus(EAIFocusPriority::Gameplay);
	}

	// AI normally drives this monster's rotation via bUseControllerDesiredRotation (turning to face
	// wherever the AIController's ControlRotation/Focus points, completely independent of movement) — an
	// entirely different, CONFLICTING rotation source from the rider's own movement-direction input, and
	// having both active at once is what caused the rapid spinning loop. Replaced here with ordinary
	// movement-facing rotation (bOrientRotationToMovement, same as how the player's own character turns)
	// so the monster visibly turns to face wherever ABaseCharacter::AddMovementInput() is actually
	// pushing it, with only one rotation source in play. Restored exactly by OnDismounted().
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		bPreMountUseControllerDesiredRotation = MoveComp->bUseControllerDesiredRotation;
		bPreMountOrientRotationToMovement = MoveComp->bOrientRotationToMovement;
		PreMountRotationRate = MoveComp->RotationRate;

		MoveComp->bUseControllerDesiredRotation = false;
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, MountRotationRateYaw, 0.0f);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: OnMounted by %s"), *GetName(), IsValid(Rider) ? *Rider->GetName() : TEXT("None"));
}

void ABaseMonster::OnDismounted()
{
	bIsBeingRidden = false;
	CurrentRider = nullptr;

	// Restores exactly what OnMounted() changed — back to this monster's own normal AI-driven rotation.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bUseControllerDesiredRotation = bPreMountUseControllerDesiredRotation;
		MoveComp->bOrientRotationToMovement = bPreMountOrientRotationToMovement;
		MoveComp->RotationRate = PreMountRotationRate;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: OnDismounted"), *GetName());

	// Resume AI movement immediately rather than waiting for Tick()'s own idle-detection to notice
	// (which would otherwise impose a fresh WanderWaitTimeMin-Max pause before the monster starts moving
	// again, since OnMounted() already StopMovement()'d it and Tick() returned early the whole time
	// bIsBeingRidden was true). Chase/Stalk need no equivalent kickstart — they pick back up on their own
	// via the next CheckForPlayer() tick / already-armed FlankRescanTimerHandle now that bIsBeingRidden no
	// longer gates them — but OnMounted() always forces State back to Patrol, so that's the only case
	// that can actually be sitting here at dismount time.
	//
	// Which Patrol behavior to resume depends on whether this is a normal dismount or one that happened
	// mid-Dawn/Day: mounting/dismounting never touches bIsFleeingToShadow/bIsIdleInShadow at all (Tick()'s
	// own bIsBeingRidden checks already fully suppress both normal Patrol AND shadow-wander while ridden,
	// so there's nothing to pause/resume there beyond re-kicking movement here) — so a monster that was
	// idle-in-shadow when mounted is STILL idle-in-shadow now, and needs the shadow-restricted wander
	// path specifically (MoveToRandomWanderPoint() directly, not MoveToNextPatrolPoint() — a monster
	// configured with real, fixed PatrolPoints has no shade guarantee on any of them, same reasoning as
	// Tick()'s and CheckForPlayer()'s identical branches). Otherwise, ordinary patrol/wander resumes via
	// MoveToNextPatrolPoint(), ending back in normal Patrol/Chase-at-Night behavior as usual.
	if (State == EMonsterState::Patrol && !bIsWaiting)
	{
		if (bIsFleeingToShadow && bIsIdleInShadow)
		{
			MoveToRandomWanderPoint();
		}
		else
		{
			MoveToNextPatrolPoint();
		}
	}
}

FName ABaseMonster::GetMountSocketName() const
{
	if (!IsValid(GetMesh()))
	{
		return NAME_None;
	}
	if (GetMesh()->DoesSocketExist(MountSocketName))
	{
		return MountSocketName;
	}
	if (GetMesh()->DoesSocketExist(TEXT("Spine_03")))
	{
		return TEXT("Spine_03");
	}
	if (GetMesh()->DoesSocketExist(TEXT("head")))
	{
		return TEXT("head");
	}
	return NAME_None;
}

void ABaseMonster::ResetStun()
{
	if (!IsValid(this) || IsPendingKillPending() || Health <= 0.0f)
	{
		return;
	}
	bIsStunned = false;

	// Starts counting now, not from when the stun began — a fresh Candle stun is suppressed for this
	// long after recovery so a second near-immediate re-trigger can't cancel this monster's movement
	// again right as it's trying to recover.
	ShieldBounceCooldownUntil = GetWorld()->GetTimeSeconds() + ShieldBounceCooldown;

	if (State == EMonsterState::Patrol)
	{
		MoveToNextPatrolPoint();
	}
	else if (State == EMonsterState::Stalk)
	{
		// THE fix for "stalls forever on the exact spot it got stunned": MoveToLightBorder() re-arms its
		// own FlankRescanTimerHandle at its end, but only reaches that line when it doesn't bail out
		// early — and its very first check is "return immediately while bIsStunned". A stun that fires
		// while already Stalking (e.g. Chase-state momentum carrying this monster a little into the
		// item's actual, smaller HolyAuraSphere radius right as it arrives at the boundary) therefore
		// kills that timer with nothing left to ever restart it: CheckForPlayer() keeps State at Stalk
		// the whole time (correctly — light protection still applies), but EnterStalkState() only calls
		// StartStalking() when TRANSITIONING into Stalk, not when already there. Explicitly resuming
		// here closes that gap.
		MoveToLightBorder();
	}
}

void ABaseMonster::HandleDawnStarted()
{
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}

	// Cancel any in-flight Night wander/patrol move THE INSTANT Dawn breaks, before evaluating shadow
	// fleeing at all — done first, ahead of even the combat-interrupt below, so nothing later in this
	// function is fighting a Night-issued MoveToActor/MoveToLocation still in progress. ClearFocus()
	// drops SetFocus(Player) left over from Chase/Stalk (same teardown OnMounted() already does — see
	// its comment) so this monster stops turning to face whatever it was looking at.
	AAIController* AICon = Cast<AAIController>(GetController());
	if (IsValid(AICon))
	{
		AICon->StopMovement();
		AICon->ClearFocus(EAIFocusPriority::Gameplay);
	}

	// Interrupt combat — fleeing takes absolute priority over whatever this monster was doing the instant
	// Dawn breaks. Montage_Stop(0.0f, nullptr) stops EVERY montage currently playing on this AnimInstance
	// (this class has no C++-side attack-montage system of its own to target a specific one; this is
	// defensive, for whatever a Blueprint AnimBP/attack graph might have playing). State reset back to a
	// clean Patrol baseline mirrors OnMounted()'s own combat-state teardown for the same underlying
	// problem (stale Chase/Stalk state must not persist across a hard mode switch) — see its comment.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
		{
			AnimInst->Montage_Stop(0.0f, nullptr);
		}
	}

	State = EMonsterState::Patrol;
	bIsWaiting = false;
	GetWorldTimerManager().ClearTimer(FlankRescanTimerHandle);
	GetWorldTimerManager().ClearTimer(WaitTimerHandle);

	bIsFleeingToShadow = true;
	bIsIdleInShadow = false;

	// Fresh flee episode: nothing issued yet, so CheckSunExposure()'s retry-target comparison always
	// issues its first move rather than comparing against a stale target from a previous episode.
	CurrentShadowTarget = FVector::ZeroVector;

	// Armed from DAWN now (not just Day — HandleDayStarted() still (re)arms this too, defensively, for a
	// monster that spawns straight into Day with no Dawn transition ever observed) so CheckSunExposure()'s
	// periodic retry-toward-shadow logic runs for the WHOLE grace period, not just the one-shot check
	// below. ApplySunDamage() itself is what actually withholds damage during Dawn (see its own comment)
	// — this just makes sure the monster keeps actively trying to reach real cover throughout the grace
	// window instead of standing wherever its first, possibly-wrong destination left it until Day's
	// damage suddenly starts.
	GetWorldTimerManager().SetTimer(SunCheckTimerHandle, this, &ABaseMonster::CheckSunExposure, SunCheckInterval, true, 0.0f);

	// Same "don't fight the rider's own movement passthrough" reasoning already documented on
	// bIsStunned/bIsStaggered/bIsBeingRidden elsewhere in this class — only actually search for/move
	// toward shadow if nobody's riding.
	if (!bIsBeingRidden)
	{
		// Evaluate exposure at THIS monster's current (pre-move) location right now, synchronously,
		// rather than waiting for the timer above to fire on the next Tick — same trace-and-react logic
		// CheckSunExposure() runs every SunCheckInterval for the rest of this flee episode, reused here
		// unchanged rather than duplicated. Crucially, a monster that's ALREADY standing in shadow the
		// instant Dawn strikes (e.g. spawned/parked indoors) hits CheckSunExposure()'s SafeInShadow
		// branch immediately: since StopMovement() above already left it not-Moving, that branch settles
		// it straight into bIsIdleInShadow=true instead of leaving it "fleeing" with nothing to actually
		// do — no separate FindShadowLocation()/MoveToLocation() call needed (or wanted) here at all.
		CheckSunExposure();

		// See this event's header comment: purely a supplementary notification now — native movement
		// above no longer depends on anything being bound to this.
		OnStartFleeingToShadow();
	}
}

void ABaseMonster::HandleDayStarted()
{
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}

	// Re-arms (harmless no-op timing-wise if HandleDawnStarted() already armed this for the grace period —
	// SetTimer() just restarts the same repeating call) rather than assuming it's already running: a
	// monster can spawn (or have BeginPlay() seed it) straight into Day with no Dawn transition ever
	// observed, and this is the only other place that starts sun checks at all. FirstDelay=0.0f: check
	// immediately rather than waiting a full SunCheckInterval before the first result, so a monster caught
	// out at the exact moment Day starts reacts right away.
	GetWorldTimerManager().SetTimer(SunCheckTimerHandle, this, &ABaseMonster::CheckSunExposure, SunCheckInterval, true, 0.0f);

	UE_LOG(LogTemp, Warning, TEXT("%s: day started — sun checks active every %.2fs"), *GetName(), SunCheckInterval);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(304, 3.0f, FColor::Yellow, FString::Printf(TEXT("%s: DAY — sun checks active"), *GetName()));
	}
}

void ABaseMonster::HandleNightStarted()
{
	GetWorldTimerManager().ClearTimer(SunCheckTimerHandle);
	SunState = EMonsterSunState::SafeInShadow;
	bIsFleeingToShadow = false;
	bIsIdleInShadow = false;
	CurrentShadowTarget = FVector::ZeroVector;
	RestoreNormalMaxWalkSpeed();

	// Explicit kickstart: a flee move that was still Moving (stuck against geometry, or simply
	// still in flight) the instant Night breaks would otherwise leave Tick()'s Patrol handling
	// waiting forever for GetMoveStatus() != Moving before it ever issues a fresh command — this
	// forces a new patrol/wander move right now regardless of whatever AI state Dawn/Day left
	// behind.
	bIsWaiting = false;
	State = EMonsterState::Patrol;
	MoveToNextPatrolPoint();

	UE_LOG(LogTemp, Warning, TEXT("%s: night started — sun checks stopped, resuming normal behavior"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(305, 3.0f, FColor::Cyan, FString::Printf(TEXT("%s: NIGHT — hunting resumed"), *GetName()));
	}
}

void ABaseMonster::CheckSunExposure()
{
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}

	if (!IsValid(DayNightManager) || !IsValid(DayNightManager->DirectionalLight))
	{
		return;
	}

	// A directional light's forward vector is the direction its RAYS travel (sun -> ground) — the
	// direction TOWARD the sun itself, which this trace needs to walk, is the opposite.
	const FVector DirectionToSun = -DayNightManager->DirectionalLight->GetActorForwardVector();

	const float HeadHeight = IsValid(GetCapsuleComponent()) ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, HeadHeight);
	const FVector TraceEnd = TraceStart + DirectionToSun * SunTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	const bool bBlockedFromSun = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, SunTraceChannel, QueryParams);

	SunState = bBlockedFromSun ? EMonsterSunState::SafeInShadow : EMonsterSunState::SunlitBurning;

	UE_LOG(LogTemp, Warning, TEXT("%s::CheckSunExposure: %s (%s)"), *GetName(),
		bBlockedFromSun ? TEXT("SafeInShadow") : TEXT("SunlitBurning"),
		(bBlockedFromSun && IsValid(Hit.GetActor())) ? *Hit.GetActor()->GetName() : TEXT("no obstruction"));

	if (bDebugSunTrace)
	{
		// Stops exactly at the obstruction on a hit (not the full TraceEnd) so the drawn line shows what
		// actually blocked the sun, not just "something, somewhere along this ray". Duration=
		// SunCheckInterval (not a short flicker-per-frame value like HolyItem's Tick()-driven debug
		// spheres) since this only runs once per SunCheckInterval via SunCheckTimerHandle, not every Tick —
		// the line needs to persist until the NEXT check redraws it, or it'd flicker in and out between
		// checks.
		const FVector DebugLineEnd = bBlockedFromSun ? Hit.ImpactPoint : TraceEnd;
		DrawDebugLine(GetWorld(), TraceStart, DebugLineEnd, bBlockedFromSun ? FColor::Green : FColor::Red, false, SunCheckInterval, 0, 3.0f);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(306, SunCheckInterval, bBlockedFromSun ? FColor::Green : FColor::Red,
				FString::Printf(TEXT("%s: %s"), *GetName(), bBlockedFromSun ? TEXT("Safe in shadow") : TEXT("SUNLIT - BURNING")));
		}
	}

	if (SunState == EMonsterSunState::SunlitBurning)
	{
		// Definitely not safely idle if the trace just found it sunlit — whether that's the very first
		// check of a fresh episode or the sun having moved off a spot that used to be shaded.
		bIsIdleInShadow = false;

		ApplySunDamage();

		// Still burning at wherever HandleDawnStarted() (or the last retry here) sent it — the sun moves
		// across the sky over the course of a Day, so a spot shaded at Dawn (or a FindShadowLocation()
		// fallback point that was never actually verified shaded) can stop being safe later. Retry the
		// search rather than leaving this monster to keep burning at a now-wrong destination for the rest
		// of the day. Gated on bIsFleeingToShadow (only retry while actually in flee mode — HandleDeath()
		// may have just fired above this frame, but ApplySunDamage() itself already stopped movement/AI
		// for a dead monster, so IsValid()/bIsDead re-checked here too) and !bIsBeingRidden (same
		// passthrough reasoning as HandleDawnStarted()).
		if (IsValid(this) && !IsPendingKillPending() && !bIsDead && bIsFleeingToShadow && !bIsBeingRidden)
		{
			AAIController* AICon = Cast<AAIController>(GetController());
			UCharacterMovementComponent* MoveComp = GetCharacterMovement();

			// Path-flapping guard: this runs every SunCheckInterval (as often as every 0.25s) for the
			// entire time this monster stays SunlitBurning, and FindShadowLocation() samples a FRESH
			// random NavMesh point every call — re-issuing MoveToLocation() unconditionally on every one
			// of those ticks was cancelling and restarting the path follower before it ever got a chance
			// to actually cover ground, which is exactly what read as "frozen" despite genuinely being
			// SunlitBurning (and taking damage) the whole time. Only skip re-issuing while a move toward
			// the CURRENT target is still genuinely in flight.
			const bool bAlreadyMovingToShadow = AICon
				&& AICon->GetMoveStatus() == EPathFollowingStatus::Moving
				&& IsValid(MoveComp) && MoveComp->IsActive()
				&& !CurrentShadowTarget.IsNearlyZero();

			if (!bAlreadyMovingToShadow)
			{
				const FVector RetryShadowLoc = FindShadowLocation();
				if (!RetryShadowLoc.IsNearlyZero())
				{
					// Only actually re-issue if the freshly-sampled candidate is a meaningfully
					// different destination than what's already been sent — otherwise two nearly-
					// identical random samples in a row (or a repeat of the exact same fallback
					// "closest point") would still flap the path follower for no real gain.
					const bool bTargetChangedSignificantly = CurrentShadowTarget.IsNearlyZero()
						|| FVector::DistSquared(RetryShadowLoc, CurrentShadowTarget) > FMath::Square(ShadowAcceptanceRadius);

					if (bTargetChangedSignificantly)
					{
						IssueShadowFleeMove(RetryShadowLoc);
					}
				}
			}
		}
	}
	else if (bIsFleeingToShadow)
	{
		// Reached cover (trace says SafeInShadow) — but don't cut an in-flight move short to declare
		// that: StopMovement() the instant the trace flips would abort the path wherever this monster
		// happens to be mid-stride (possibly an awkward spot relative to obstacles, or short of
		// CurrentShadowTarget entirely if a stray bit of overhead cover shaded this one trace early),
		// which is what used to read as an abrupt freeze rather than a clean arrival. Let the path
		// follower finish covering the remaining distance to CurrentShadowTarget on its own — only
		// finalize the idle-in-shadow state once it has genuinely stopped moving (arrived, or never had
		// anywhere to go in the first place), which is a smooth, natural stop rather than a forced one.
		AAIController* AICon = Cast<AAIController>(GetController());
		const bool bStillMoving = AICon && AICon->GetMoveStatus() == EPathFollowingStatus::Moving;

		if (!bStillMoving)
		{
			RestoreNormalMaxWalkSpeed();
			CurrentShadowTarget = FVector::ZeroVector;
			bIsIdleInShadow = true;
		}
	}
}

void ABaseMonster::ApplySunDamage()
{
	if (!IsValid(this) || IsPendingKillPending() || bIsDead)
	{
		return;
	}

	// Dawn is a grace period: CheckSunExposure() (and its retry-toward-shadow logic) still runs and
	// SunState can still read SunlitBurning while Dawn is in progress — this monster just doesn't take
	// damage FOR it yet, giving it the whole Dawn window to actually reach cover before Day's full damage
	// kicks in. Only Day applies real damage; Night never reaches here at all (HandleNightStarted() stops
	// SunCheckTimerHandle entirely). No DayNightManager at all (shouldn't happen if this got called, since
	// CheckSunExposure() already bails without one, but checked explicitly rather than assumed) also skips
	// damage rather than defaulting to "apply it anyway".
	if (!IsValid(DayNightManager) || DayNightManager->SunPhase != ESunPhase::Day)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: sunlit during Dawn grace period — no damage yet"), *GetName());
		if (bDebugSunTrace && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(307, SunCheckInterval, FColor::Yellow,
				FString::Printf(TEXT("%s: DAWN grace period - sunlit, no damage yet"), *GetName()));
		}
		return;
	}

	// Scaled by SunCheckInterval (not applied as a flat SunDamagePerSecond every tick) since this only
	// actually runs once per SunCheckInterval — this is what makes SunDamagePerSecond mean what its name
	// says regardless of how the interval is tuned.
	const float DamageThisTick = SunDamagePerSecond * SunCheckInterval;
	Health -= DamageThisTick;

	UE_LOG(LogTemp, Warning, TEXT("%s: burning in direct sunlight for %.1f damage (HP left: %.1f)"), *GetName(), DamageThisTick, Health);
	if (bDebugSunTrace && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(307, SunCheckInterval, FColor::Red,
			FString::Printf(TEXT("%s: sun damage %.1f (HP %.1f)"), *GetName(), DamageThisTick, Health));
	}

	// No external DamageCauser for an ambient environmental source — passed as `this`, same as Die()'s
	// own instant-kill path does, purely for HandleDeath()'s logging/attribution parameter.
	if (Health <= 0.0f)
	{
		HandleDeath(this);
	}
}

FVector ABaseMonster::FindShadowLocation() const
{
	if (!IsValid(DayNightManager) || !IsValid(DayNightManager->DirectionalLight))
	{
		return FVector::ZeroVector;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return FVector::ZeroVector;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!IsValid(NavSys))
	{
		return FVector::ZeroVector;
	}

	const FVector Origin = GetActorLocation();

	// Tracked across every attempt, at either radius (not just the ones that fail to find shade), so the
	// fallback below has something useful to offer even if none actually lands in full shadow. Never the
	// monster's OWN current (by definition sunlit) location — only ever a genuine NavMesh sample.
	bool bFoundAnyNavPoint = false;
	float ClosestDistSq = TNumericLimits<float>::Max();
	FVector ClosestPoint = FVector::ZeroVector;

	// Two-pass search: the normal ShadowSearchRadius first, then a doubled radius if nothing in that
	// first pass actually landed in full shadow. Real covered terrain (a building, an overhang) sitting
	// just outside the normal radius otherwise never gets a chance, and this monster would settle for
	// "closest sunlit point" — which is barely distinguishable from standing still — before ever looking
	// further out.
	const float SearchRadii[] = { ShadowSearchRadius, ShadowSearchRadius * 2.0f };
	for (const float Radius : SearchRadii)
	{
		for (int32 Attempt = 0; Attempt < MaxShadowSearchAttempts; ++Attempt)
		{
			FNavLocation RandomNavLocation;
			if (!NavSys->GetRandomReachablePointInRadius(Origin, Radius, RandomNavLocation))
			{
				continue;
			}

			bFoundAnyNavPoint = true;
			const float DistSq = FVector::DistSquared(Origin, RandomNavLocation.Location);
			if (DistSq < ClosestDistSq)
			{
				ClosestDistSq = DistSq;
				ClosestPoint = RandomNavLocation.Location;
			}

			if (IsLocationInShadow(RandomNavLocation.Location))
			{
				UE_LOG(LogTemp, Warning, TEXT("%s::FindShadowLocation: found shadow point %s after %d attempt(s) at radius %.0f"),
					*GetName(), *RandomNavLocation.Location.ToCompactString(), Attempt + 1, Radius);
				return RandomNavLocation.Location;
			}
		}
	}

	// No attempt at either radius found a fully-shadowed point. The closest reachable point actually
	// tested is still a better destination than standing still, so callers treat it as usable — only
	// FVector::ZeroVector (returned when the NavMesh search itself never produced a single reachable
	// point at either radius) means "nothing usable at all" and should NOT be passed to MoveToLocation().
	if (bFoundAnyNavPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::FindShadowLocation: no fully-shadowed point in %d attempts at either radius — falling back to closest tested point %s"),
			*GetName(), MaxShadowSearchAttempts, *ClosestPoint.ToCompactString());
		return ClosestPoint;
	}

	UE_LOG(LogTemp, Error, TEXT("%s::FindShadowLocation: NO SHADOW FOUND ON NAVMESH!"), *GetName());
	if (bDebugSunTrace && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(312, 5.0f, FColor::Red,
			FString::Printf(TEXT("%s NO SHADOW FOUND ON NAVMESH!"), *GetName()));
	}
	return FVector::ZeroVector;
}

bool ABaseMonster::IsLocationInShadow(const FVector& Location) const
{
	if (!IsValid(DayNightManager) || !IsValid(DayNightManager->DirectionalLight))
	{
		// Nothing to check exposure against — don't let a missing DayNightManager block movement that
		// would otherwise be perfectly fine.
		return true;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return true;
	}

	// Same direction-toward-the-sun convention as CheckSunExposure(): a directional light's forward
	// vector is the direction its RAYS travel (sun -> ground), so the direction TOWARD the sun is the
	// opposite.
	const FVector DirectionToSun = -DayNightManager->DirectionalLight->GetActorForwardVector();

	// +100 Z: approximates head height above Location (which, for every current caller, sits at
	// ground/floor level, e.g. a sampled NavMesh point) — tracing from ground level itself would
	// false-negative on flat, open ground that has genuine overhead cover a few units up.
	const FVector TraceStart = Location + FVector(0.0f, 0.0f, 100.0f);
	const FVector TraceEnd = TraceStart + DirectionToSun * SunTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, SunTraceChannel, QueryParams);
}

void ABaseMonster::IssueShadowFleeMove(const FVector& TargetLocation)
{
	AAIController* AICon = Cast<AAIController>(GetController());
	if (!IsValid(AICon))
	{
		return;
	}

	// Boosted only once per flee episode (guarded by bMaxWalkSpeedBoosted) — a monster already fleeing
	// that lands back here via another retry shouldn't overwrite PreFleeMaxWalkSpeed with its OWN
	// already-boosted FleeMaxWalkSpeed value.
	if (!bMaxWalkSpeedBoosted && IsValid(GetCharacterMovement()))
	{
		PreFleeMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
		GetCharacterMovement()->MaxWalkSpeed = FleeMaxWalkSpeed;
		bMaxWalkSpeedBoosted = true;
	}

	RequestMoveToLocation(AICon, TargetLocation, ShadowAcceptanceRadius);
	CurrentShadowTarget = TargetLocation;
	bIsIdleInShadow = false;

	const float DistanceToShadow = FVector::Dist(GetActorLocation(), TargetLocation);

	UE_LOG(LogTemp, Warning, TEXT("%s: fleeing to shadow point %s at speed %.0f (distance %.0f)"),
		*GetName(), *TargetLocation.ToCompactString(), FleeMaxWalkSpeed, DistanceToShadow);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(303, 3.0f, FColor::Orange,
			FString::Printf(TEXT("%s: fleeing to shadow at %s"), *GetName(), *TargetLocation.ToCompactString()));
	}

	// Gated behind bDebugSunTrace (same "tuning visualization" toggle CheckSunExposure()'s own
	// DrawDebugLine/on-screen readout already use), not a separate always-on flag — this is the same
	// category of debug output.
	if (bDebugSunTrace)
	{
		DrawDebugSphere(GetWorld(), TargetLocation, 50.0f, 12, FColor::Cyan, false, 5.0f, 0, 2.0f);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(311, 3.0f, FColor::Cyan,
				FString::Printf(TEXT("%s Fleeing to shadow at speed %.0f! Distance: %.0f units"),
					*GetName(), FleeMaxWalkSpeed, DistanceToShadow));
		}
	}
}

void ABaseMonster::RestoreNormalMaxWalkSpeed()
{
	if (!bMaxWalkSpeedBoosted)
	{
		return;
	}

	if (IsValid(GetCharacterMovement()))
	{
		GetCharacterMovement()->MaxWalkSpeed = PreFleeMaxWalkSpeed;
	}
	bMaxWalkSpeedBoosted = false;

	UE_LOG(LogTemp, Warning, TEXT("%s: restored MaxWalkSpeed to %.0f"), *GetName(), PreFleeMaxWalkSpeed);
}
