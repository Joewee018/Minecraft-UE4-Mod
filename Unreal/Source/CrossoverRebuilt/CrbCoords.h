// Minecraft <-> Unreal coordinate conversion. MC (x east, y up, z south), right-handed, metres(blocks).
// UE = (100*(z-az), -100*(x-ax), 100*(y-ay)). MC yaw == UE yaw; MC pitch == -UE pitch.
#pragma once
#include "CoreMinimal.h"

// ProceduralMeshComponent stores vertex UVs as half floats, which snap to 1/2048 between 0.5 and 1: four texels of the
// 8192-wide block atlas that resource packs like Guns++ produce (held items and particles sampled the wrong texels).
// Atlas UVs are therefore sent as a coarse part on UV0 (multiples of 1/256, exact in half precision) plus a small
// remainder on UV2 (precise in half), and the materials sample at TexCoord0 + TexCoord2.
inline void CrbSplitUV(const FVector2D& UV, FVector2D& Coarse, FVector2D& Fine)
{
	Coarse = FVector2D(FMath::RoundToFloat(UV.X * 256.f) / 256.f, FMath::RoundToFloat(UV.Y * 256.f) / 256.f);
	Fine = UV - Coarse;
}

struct FCrbCoords
{
	// Anchor in Minecraft block coordinates (double precision, section aligned).
	double AX = 0, AY = 0, AZ = 0;

	FVector ToUE(double X, double Y, double Z) const
	{
		return FVector(float((Z - AZ) * 100.0), float(-(X - AX) * 100.0), float((Y - AY) * 100.0));
	}
	// Direction / local offset (no anchor).
	static FVector DirToUE(const FVector& Mc) { return FVector(Mc.Z * 100.f, -Mc.X * 100.f, Mc.Y * 100.f); }
	static FVector NormalToUE(const FVector& Mc) { return FVector(Mc.Z, -Mc.X, Mc.Y); }
	void ToMC(const FVector& UE, double& X, double& Y, double& Z) const
	{
		X = AX - UE.Y / 100.0; Y = AY + UE.Z / 100.0; Z = AZ + UE.X / 100.0;
	}
	static FRotator RotToUE(float McYaw, float McPitch) { return FRotator(-McPitch, McYaw, 0.f); }
	static FVector DirectionVector(int32 Dir)
	{
		static const FVector D[6] = { FVector(0,-1,0), FVector(0,1,0), FVector(0,0,-1), FVector(0,0,1), FVector(-1,0,0), FVector(1,0,0) };
		return (Dir >= 0 && Dir < 6) ? D[Dir] : FVector::ZeroVector;
	}
	static FIntVector DirectionInt(int32 Dir)
	{
		static const FIntVector D[6] = { FIntVector(0,-1,0), FIntVector(0,1,0), FIntVector(0,0,-1), FIntVector(0,0,1), FIntVector(-1,0,0), FIntVector(1,0,0) };
		return (Dir >= 0 && Dir < 6) ? D[Dir] : FIntVector::ZeroValue;
	}
	static int32 FloorDiv16(int32 V) { return V >> 4; }
};
