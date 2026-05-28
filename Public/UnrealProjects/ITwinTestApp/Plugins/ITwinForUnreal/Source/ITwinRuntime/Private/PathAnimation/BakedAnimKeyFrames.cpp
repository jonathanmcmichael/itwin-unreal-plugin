/*--------------------------------------------------------------------------------------+
|
|     $Source: BakedAnimKeyFrames.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <PathAnimation/BakedAnimKeyFrames.h>
#include <Spline/ITwinSplineHelper.h>
#include <Components/SplineComponent.h>
#include <Math/UEMathConversion.h>
#include <EngineUtils.h> // for TActorIterator<>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	extern bool FindHeight(UWorld* World, const FVector& InPos, float& OutHeight, FVector& OutNormal, float maxHeight);
}


void UBakedAnimKeyFrames::MarkForUpdate()
{
	Status = EBakedKeyFramesStatus::Invalid;
}

bool UBakedAnimKeyFrames::NeedsUpdate() const
{
	return Status == EBakedKeyFramesStatus::Invalid;
}

bool UBakedAnimKeyFrames::IsReady() const
{
	return Status == EBakedKeyFramesStatus::Ready && transforms.Num() > 0;
}

float UBakedAnimKeyFrames::GetTotalTime() const
{
	return TotalTime;
}

void UBakedAnimKeyFrames::SetSpeed(float InSpeed)
{
	if (InSpeed > 0.f)
		TotalTime = TotalLength / InSpeed;
}

void UBakedAnimKeyFrames::BakeSpline(UWorld* World, const AdvViz::SDK::RefID& SplineId, float InSpeed)
{
	Status = EBakedKeyFramesStatus::InProgress;
	transforms.Empty();

	auto AnimSpline = [World, SplineId]() -> AITwinSplineHelper*
		{
			for (TActorIterator<AITwinSplineHelper> It(World); It; ++It)
			{
				auto splineInst = It->GetAVizSpline()->GetRAutoLock();
				if (splineInst->GetId() == SplineId) return *It;
			}
			return nullptr;
		}();

	if (!AnimSpline || InSpeed <= 0.0f)
		return;

	auto UESpline = AnimSpline->GetSplineComponent();

	TotalLength = UESpline->GetSplineLength(); // in cm
	BE_LOGI("App", "Processing animation spline of length " << TotalLength);
	if (TotalLength < 0.01f)
		return;
	TotalTime = TotalLength / InSpeed;

	float CurrentDistance = 0.0f;

	while (CurrentDistance <= TotalLength)
	{
		// Position along spline at given distance
		FVector SplineLocation = UESpline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
		FVector SplineTangent = UESpline->GetTangentAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World).GetSafeNormal();

		// Ground height and normal
		float GroundZ = 0.0f;
		FVector GroundNormal = FVector::UpVector;

		//FVector TracePosition = SplineLocation + FVector(0, 0, 500); // trace from above
		if (ITwin::FindHeight(World, SplineLocation/*TracePosition*/, GroundZ, GroundNormal, 200)) // limit to 2m to avoid snapping to bridges
		{
			SplineLocation.Z = GroundZ;
		}

		// Build orientation
		FVector Forward = SplineTangent;
		FVector Up = GroundNormal;
		FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		FVector AlignedForward = FVector::CrossProduct(Right, Up).GetSafeNormal();

		// World orientation based on spline and surface
		FMatrix Basis(AlignedForward, Right, Up, FVector::ZeroVector);
		FQuat WorldRotation = FQuat(Basis);

		// Apply alignment fix if needed (Y+ to X+ correction)
		FQuat AlignmentFix = FQuat(FVector::UpVector, 3 * PI / 2);
		FQuat FinalRotation = WorldRotation * AlignmentFix;

		FTransform Keyframe(FinalRotation, SplineLocation);
		transforms.Add(Keyframe);

		CurrentDistance += DistanceStep;
	}

	Status = transforms.Num() > 0 ? EBakedKeyFramesStatus::Ready : EBakedKeyFramesStatus::Invalid;
}

int32 UBakedAnimKeyFrames::GetKeyframeIndex(float Time)
{
	if (!IsReady())
		return -1;

	int32 Index = FMath::FloorToInt((transforms.Num() - 1) * Time / TotalTime);
	return FMath::Clamp(Index, 0, transforms.Num() - 2); // -2 to allow interpolation with next frame
}

FTransform UBakedAnimKeyFrames::GetTransform(float Time, bool bReverse/* = false*/)
{
	auto idx = GetKeyframeIndex(bReverse ? TotalTime - Time : Time);
	if (idx < 0)
		return FTransform();

	auto outTransform = transforms[idx];
	if (bReverse)
	{
		const FQuat Rot = outTransform.GetRotation();
		const FVector Up = Rot.GetUpVector();
		const FQuat FlipQuat(Up, PI); // 180° rotation around local up
		FQuat NewRot = Rot * FlipQuat;
		NewRot.Normalize();
		outTransform = FTransform(NewRot, outTransform.GetLocation(), outTransform.GetScale3D());
	}
	return outTransform;
}

