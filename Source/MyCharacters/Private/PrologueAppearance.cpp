// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueAppearance.h"

namespace
{
	// Limits read defensively: a DataAsset can hold Min > Max or a preferred value outside the limits.
	struct FSaneLimits
	{
		float MinDistance;
		float MaxDistance;
		float PreferredDistance;
		float MaxViewAngle;
		float PreferredViewAngle;
	};

	FSaneLimits MakeSane(const FPrologueAppearancePlacement& Placement)
	{
		FSaneLimits L;
		L.MinDistance = FMath::Min(Placement.MinDistance, Placement.MaxDistance);
		L.MaxDistance = FMath::Max(Placement.MinDistance, Placement.MaxDistance);
		L.PreferredDistance = FMath::Clamp(Placement.PreferredDistance, L.MinDistance, L.MaxDistance);
		L.MaxViewAngle = FMath::Clamp(Placement.MaxViewAngle, 0.0f, 180.0f);
		L.PreferredViewAngle = FMath::Clamp(Placement.PreferredViewAngle, 0.0f, L.MaxViewAngle);
		return L;
	}

	FVector FlatDir(const FVector& Dir)
	{
		const FVector Flat(Dir.X, Dir.Y, 0.0f);
		return Flat.IsNearlyZero() ? FVector::ForwardVector : Flat.GetSafeNormal();
	}
}

float PrologueAppearance::ViewAngleTo(const FVector& ViewLocation, const FVector& ViewDir, const FVector& Point)
{
	const FVector ToPoint(Point.X - ViewLocation.X, Point.Y - ViewLocation.Y, 0.0f);
	if (ToPoint.IsNearlyZero())
	{
		return 0.0f;
	}
	const float Cos = FMath::Clamp(FVector::DotProduct(FlatDir(ViewDir), ToPoint.GetSafeNormal()), -1.0f, 1.0f);
	return FMath::RadiansToDegrees(FMath::Acos(Cos));
}

bool PrologueAppearance::IsWithinLimits(const FVector& ViewLocation, const FVector& ViewDir, const FVector& Point,
	const FPrologueAppearancePlacement& Placement)
{
	const FSaneLimits L = MakeSane(Placement);
	const float Distance = FVector::Dist2D(ViewLocation, Point);
	const float Angle = ViewAngleTo(ViewLocation, ViewDir, Point);
	// Tiny tolerance so points generated exactly on the limits (MakeFallbackCandidates) are not lost to rounding.
	return Distance >= L.MinDistance - 1.0f && Distance <= L.MaxDistance + 1.0f && Angle <= L.MaxViewAngle + 0.01f;
}

int32 PrologueAppearance::ChooseBestPoint(const TArray<FVector>& Candidates, const FVector& ViewLocation, const FVector& ViewDir,
	const FPrologueAppearancePlacement& Placement, TFunctionRef<bool(const FVector&)> IsVisible)
{
	const FSaneLimits L = MakeSane(Placement);
	const float DistanceRange = FMath::Max(L.MaxDistance - L.MinDistance, 1.0f);
	const float AngleRange = FMath::Max(L.MaxViewAngle, 1.0f);

	struct FScored
	{
		int32 Index;
		float Score;
	};
	TArray<FScored> Allowed;

	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (!IsWithinLimits(ViewLocation, ViewDir, Candidates[i], Placement))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(ViewLocation, Candidates[i]);
		const float Angle = ViewAngleTo(ViewLocation, ViewDir, Candidates[i]);
		const float Score = FMath::Abs(Distance - L.PreferredDistance) / DistanceRange
			+ FMath::Abs(Angle - L.PreferredViewAngle) / AngleRange;
		Allowed.Add({ i, Score });
	}

	// Stable: equal scores keep the candidates' own order.
	Allowed.StableSort([](const FScored& A, const FScored& B) { return A.Score < B.Score; });

	for (const FScored& Entry : Allowed)
	{
		if (IsVisible(Candidates[Entry.Index]))
		{
			return Entry.Index;
		}
	}
	return INDEX_NONE;
}

TArray<FVector> PrologueAppearance::MakeFallbackCandidates(const FVector& ViewLocation, const FVector& ViewDir,
	const FPrologueAppearancePlacement& Placement)
{
	const FSaneLimits L = MakeSane(Placement);
	const FVector Forward = FlatDir(ViewDir);

	// Distances: the preferred one first, then the limits and the halfway marks (duplicates dropped).
	TArray<float> Distances;
	for (const float D : { L.PreferredDistance, (L.MinDistance + L.PreferredDistance) * 0.5f, (L.PreferredDistance + L.MaxDistance) * 0.5f,
		L.MinDistance, L.MaxDistance })
	{
		if (!Distances.ContainsByPredicate([D](float Other) { return FMath::IsNearlyEqual(D, Other, 1.0f); }))
		{
			Distances.Add(D);
		}
	}

	// Angles: the preferred one on both sides, then a fan across the whole allowed width.
	TArray<float> Angles;
	auto AddAngle = [&Angles](float A)
	{
		if (!Angles.ContainsByPredicate([A](float Other) { return FMath::IsNearlyEqual(A, Other, 0.5f); }))
		{
			Angles.Add(A);
		}
	};
	AddAngle(L.PreferredViewAngle);
	AddAngle(-L.PreferredViewAngle);
	constexpr int32 FanSteps = 4;
	for (int32 Step = -FanSteps; Step <= FanSteps; ++Step)
	{
		AddAngle(L.MaxViewAngle * Step / FanSteps);
	}

	TArray<FVector> Points;
	Points.Reserve(Distances.Num() * Angles.Num());
	for (const float D : Distances)
	{
		for (const float A : Angles)
		{
			const FVector Dir = Forward.RotateAngleAxis(A, FVector::UpVector);
			Points.Add(FVector(ViewLocation.X, ViewLocation.Y, ViewLocation.Z) + Dir * D);
		}
	}
	return Points;
}
