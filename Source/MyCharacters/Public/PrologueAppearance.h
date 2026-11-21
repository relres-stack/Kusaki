// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "PrologueAppearance.generated.h"

// Where a hero shows up when the prologue brings him in (task 04, step 2.1): somewhere the player can see him,
// at a comfortable distance and angle from where the player is looking. Used by APrologueDirector::AppearHero().
//
// Points the level designer (or rtd_build_level.py) placed in advance come first: actors with CandidateTag,
// usually ATargetPoint standing on the ground. If the level has none, or none of them fits right now, points are
// generated around the player's view and snapped to the navmesh.
USTRUCT(BlueprintType)
struct FPrologueAppearancePlacement
{
	GENERATED_BODY()

	// Actors with this tag are the candidate points (their location = the hero's feet, their rotation = where he
	// faces). None, or no such actor in the level = points around the player's view, the hero facing the player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FName CandidateTag;

	// Horizontal distance from the player's camera, cm. Points outside [Min, Max] are never used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.0"))
	float MinDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.0"))
	float MaxDistance = 4000.0f;

	// Among the allowed points, the one closest to this distance and to PreferredViewAngle wins.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.0"))
	float PreferredDistance = 1500.0f;

	// Horizontal angle between where the player looks and the point, degrees (0 = straight ahead). Points wider
	// than MaxViewAngle are never used (the player would not see the hero appear).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxViewAngle = 50.0f;

	// "Out of the corner of the eye" is about 20-35; 0 = right in front of the player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float PreferredViewAngle = 20.0f;

	// The visibility line goes from the camera to this height above the point (the hero's chest), cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.0"))
	float VisibilityTraceHeight = 120.0f;
};

// The math of choosing a point, kept free of the world so the automation test (PrologueAppearanceTest) can run it
// with made-up points. World queries (line traces, navmesh) are passed in or done by the caller.
namespace PrologueAppearance
{
	// Horizontal (yaw-only) angle in degrees between ViewDir and the direction from ViewLocation to Point, 0..180.
	MYCHARACTERS_API float ViewAngleTo(const FVector& ViewLocation, const FVector& ViewDir, const FVector& Point);

	// Whether Point is within the placement's distance and angle limits (visibility not checked).
	MYCHARACTERS_API bool IsWithinLimits(const FVector& ViewLocation, const FVector& ViewDir, const FVector& Point,
		const FPrologueAppearancePlacement& Placement);

	// Index of the best point in Candidates, or INDEX_NONE. A point is allowed if its horizontal distance from
	// ViewLocation is within [MinDistance, MaxDistance] and its ViewAngleTo() is within MaxViewAngle. Among the
	// allowed points the lowest score wins: distance off PreferredDistance (as a share of the distance range) plus
	// angle off PreferredViewAngle (as a share of MaxViewAngle). IsVisible is asked only for allowed points, best
	// score first, and the first visible one is returned (fewer line traces).
	MYCHARACTERS_API int32 ChooseBestPoint(const TArray<FVector>& Candidates, const FVector& ViewLocation, const FVector& ViewDir,
		const FPrologueAppearancePlacement& Placement, TFunctionRef<bool(const FVector&)> IsVisible);

	// Points inside the placement's distance and angle limits around the viewer, at the viewer's height (the caller
	// snaps them to the ground/navmesh). Used when the level has no candidate points.
	MYCHARACTERS_API TArray<FVector> MakeFallbackCandidates(const FVector& ViewLocation, const FVector& ViewDir,
		const FPrologueAppearancePlacement& Placement);
}
