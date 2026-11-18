// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IntroSceneDirector.generated.h"

class ABaseCharacter;
class ACameraActor;
class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIntroSceneFinished);

// Phases of the opening scene, in order. Done is terminal (the scene never replays in the same session).
UENUM(BlueprintType)
enum class EIntroPhase : uint8
{
	Idle,       // not started yet
	Walking,    // the hero walks out of the forest; the camera flies towards him from far and high, over the hills
	Holding,    // optional pause after the hero has arrived (only when OrbitLeadTime is 0)
	Orbiting,   // the camera sweeps around the hero to the gameplay position behind his back
	            // (starts OrbitLeadTime before he arrives, so he may still be taking the last steps)
	Handover,   // control is given back, the view blends from the scene camera to the player's own camera
	Done
};

// Opening scene, played in the engine on the real character (no pre-rendered video, no Sequencer asset):
// the Redneck walks out of the forest onto the start slab while the camera flies towards him from far away and
// high above the hills, the two meet on the slab (camera in front of his face), then the camera flies around
// him to the usual over-the-shoulder position and the player gets control.
//
// A plain AActor placed in the level (ABinocularsPickup/ABearTrap precedent: a world-side scripted moment,
// not a character ability). It drives the existing player pawn only through public API (AddMovementInput,
// SetActorLocation, DisableInput/EnableInput, SetAimSuppressed, SetViewTargetWithBlend, the public camera
// getters), so the walk animation is the character's own locomotion animation.
// Level data (the waypoints) is set by rtd_build_level.py from rtd_layout.json -> "intro".
UCLASS()
class MYCHARACTERS_API AIntroSceneDirector : public AActor
{
	GENERATED_BODY()

public:
	AIntroSceneDirector();

	virtual void Tick(float DeltaSeconds) override;

	// World-space positions (cm) ON THE GROUND the hero walks through, in order. The first one is where he
	// appears, the last one is where he stops (the start spot). The last segment defines the direction he faces.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro")
	TArray<FVector> Waypoints;

	// Untick to disable the scene (e.g. while testing other mechanics): the level then starts as before.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro")
	bool bPlayOnBeginPlay = true;

	// Route between waypoints through the NavMesh, so the hero walks AROUND rocks and other obstacles instead of
	// pushing into them. If there is no NavMesh path for a segment, that segment is walked in a straight line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Walk")
	bool bUseNavMesh = true;

	// Walking speed during the scene (cm/s). The character's own MaxWalkSpeed is restored afterwards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Walk")
	float WalkSpeed = 250.0f;

	// Safety net: if the hero has not reached the last waypoint within this time (stuck on something),
	// he is placed on it and the scene continues, so the level can never be soft-locked.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Walk")
	float WalkTimeout = 40.0f;

	// The camera starts turning around the hero this many seconds BEFORE he arrives, so the fly-in flows into the
	// orbit without a stop (the camera spirals in). 0 = the orbit waits until he has stopped (+ HoldDuration).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Timing")
	float OrbitLeadTime = 1.5f;

	// Pause (s) between the hero's arrival and the orbit. Only used when OrbitLeadTime is 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Timing")
	float HoldDuration = 0.0f;

	// Seconds the camera takes to fly from the front of the hero to the position behind him.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Timing")
	float OrbitDuration = 5.0f;

	// Seconds the view blends from the scene camera to the player's own camera at the very end.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Timing")
	float HandoverBlendTime = 0.8f;

	// Where the camera STARTS: this far (cm, horizontally) from the final standing point, in front of the hero.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera Approach")
	float ApproachStartDistance = 5000.0f;

	// Where the camera STARTS: this high (cm) above the final standing point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera Approach")
	float ApproachStartHeight = 2500.0f;

	// Turns the starting point around the final standing point (degrees, + = to the hero's left as he arrives).
	// 0 = straight ahead of him. Use it to pick the side the camera comes from (e.g. a clearer view over the trees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera Approach")
	float ApproachYawOffset = 0.0f;

	// Field of view at the start (narrow = the far-away hero looks bigger); widens to the player's FOV on arrival.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera Approach")
	float ApproachStartFOV = 55.0f;

	// The camera never goes lower than this above the ground or tree tops under it (cm) while flying in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera Approach")
	float MinGroundClearance = 100.0f;

	// Where the camera's approach ENDS (they meet): this far in front of the final standing point (cm),
	// along the hero's heading.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float FrontCameraDistance = 450.0f;

	// Height of the meeting point above the ground of the final standing point (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float FrontCameraHeight = 150.0f;

	// The camera looks at this height above the hero's capsule centre (cm) — roughly his face.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float LookAtHeight = 50.0f;

	// Pitch of the player's camera when control is given back (negative = looking slightly down).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float GameplayCameraPitch = -12.0f;

	// On-screen state line for testing in PIE (keys 500/501): phase, route point, distance left, hero speed,
	// walk/camera progress. Untick for a clean look at the scene.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Debug")
	bool bShowDebug = true;

	// Broadcast when the player has control again (the view may still be finishing its short blend).
	UPROPERTY(BlueprintAssignable, Category = "Intro")
	FOnIntroSceneFinished OnIntroFinished;

	UFUNCTION(BlueprintPure, Category = "Intro")
	EIntroPhase GetPhase() const { return Phase; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Intro")
	EIntroPhase Phase = EIntroPhase::Idle;

	UPROPERTY(Transient)
	TObjectPtr<ABaseCharacter> Player;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> PC;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> SceneCamera;

	FTimerHandle StartRetryHandle;
	FTimerHandle CameraCleanupHandle;
	int32 StartAttempts = 0;

	// The walked route: the waypoints with NavMesh corner points inserted between them, and the distance
	// along the route (2D, cm) at each point — used for the walk progress the camera is synced to.
	TArray<FVector> Route;
	TArray<float> RouteDist;
	int32 RouteIndex = 0;

	float PhaseTime = 0.0f;
	float WalkTime = 0.0f;          // time since the walk started (the timeout counts this, not the phase time)
	bool bHeroArrived = false;      // the walk is over; the camera phases run independently of it
	float SavedMaxWalkSpeed = 0.0f;
	FVector HeadingDir = FVector::ForwardVector;
	FRotator CameraLookRotation = FRotator::ZeroRotator;

	// Camera approach: from ApproachStart (far, high) to ApproachEnd (in front of the hero's face), driven by
	// how much of the route the hero has walked, so they arrive together.
	FVector ApproachStart = FVector::ZeroVector;
	FVector ApproachEnd = FVector::ZeroVector;
	float WalkProgress = 0.0f;     // 0..1, never goes back
	float CameraProgress = 0.0f;   // follows WalkProgress smoothly (glides instead of jumping after a teleport)
	float ClearanceLift = 0.0f;    // extra height (cm) to stay above hills/trees, smoothed
	float PlayerFOV = 90.0f;

	// Orbit: polar coordinates around the spot where the hero stops (his capsule centre there). The fly-in keeps
	// running underneath and is turned around this pivot, blending into the player's camera position.
	FVector OrbitPivot = FVector::ZeroVector;
	bool bOrbitFullTurn = false;   // degenerate set-up (camera already behind him): make a full extra turn
	FRotator GameplayRotation = FRotator::ZeroRotator;

	void TryStartScene();
	void StartScene();
	void BuildRoute();
	float ComputeWalkProgress() const;
	float RemainingWalkDistance() const;
	void BeginWalking();
	void TickWalking(float DeltaSeconds);
	void OnHeroArrived();
	FVector ComputeApproachPos(float DeltaSeconds, bool bSnap);
	void UpdateApproachCamera(float DeltaSeconds, bool bSnap);
	FVector ComputeOrbitEndPos() const;
	void BeginHolding();
	void BeginOrbit();
	void TickOrbit(float DeltaSeconds);
	void BeginHandover();
	void FinishScene();
	void DestroySceneCamera();
	void UpdateCameraLook(float DeltaSeconds, bool bSnap);
	void ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const;
};
