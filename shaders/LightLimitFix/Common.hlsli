#ifndef LLF_COMMON_HLSLI
#define LLF_COMMON_HLSLI

// Thread group dimensions for the culling compute shader
#define NUMTHREAD_X 16
#define NUMTHREAD_Y 16
#define NUMTHREAD_Z 4
#define GROUP_SIZE (NUMTHREAD_X * NUMTHREAD_Y * NUMTHREAD_Z)

// Maximum visible lights per cluster (matches kClusterMaxLights in LightLimitFix.cpp)
#define MAX_CLUSTER_LIGHTS 128

namespace LightFlags
{
	static const uint PortalStrict = (1u << 0);
	static const uint Shadow       = (1u << 1);
	static const uint Simple       = (1u << 2);

	static const uint Initialised   = (1u << 8);
	static const uint Disabled      = (1u << 9);
	static const uint InverseSquare = (1u << 10);
	static const uint Linear        = (1u << 11);
}

// GPU-side layout matching LightLimitFix::ClusterAABB in C++
struct ClusterAABB
{
	float4 minPoint;
	float4 maxPoint;
};

// GPU-side layout matching LightLimitFix::LightGrid in C++
struct LightGrid
{
	uint offset;
	uint lightCount;
	uint2 pad0;
};

// GPU-side layout matching LightLimitFix::LightData in C++ (sizeof == 96,
// static_asserted in LightLimitFix.h). Field offsets MUST stay in lock-step:
//   color/fade            0..16
//   radius..sizeBias     16..32
//   positionWS[2]        32..64 (float3 data + uint pad per slot)
//   roomFlags[4]         64..80
//   lightFlags           80
//   shadowLightIndex     84
//   pad0 / pad1          88 / 92
struct Light
{
	float3 color;           // offset 0
	float fade;             // offset 12
	float radius;           // offset 16
	float invRadius;        // offset 20
	float fadeZone;         // offset 24
	float sizeBias;         // offset 28
	float4 positionWS[2];   // offset 32..64 (float3 data + uint pad per slot)
	uint4 roomFlags;        // offset 64 (maps uint32_t roomFlags[4])
	uint lightFlags;        // offset 80
	uint shadowLightIndex;  // offset 84 (C++ shadowLightIndex)
	uint pad0;              // offset 88
	uint pad1;              // offset 92
};

#endif // LLF_COMMON_HLSLI
