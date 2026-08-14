#ifndef LLF_LIGHT_LIMIT_FIX_HLSLI
#define LLF_LIGHT_LIMIT_FIX_HLSLI

namespace LightLimitFix
{
	// FO4 forward clustered-lighting contract. FO4 BSLighting has NO b3
	// strict-light buffer (vanilla only declares CB1[8] + CB2[38]); the b3
	// strict-light path was Skyrim CS structure carried over and is removed.
	// Every forward point light is read from the clustered SRVs t35/t36/t37.
	// Shadow-casting point lights are owned by the deferred DFLight pass and
	// are skipped here so they are never double-lit.
#include "LightLimitFix/Common.hlsli"

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);
	StructuredBuffer<LightGrid> lightGrid : register(t37);

	bool GetClusterIndex(in float2 uv, in float viewZ, in uint3 clusterSize, in float cameraNear, in float cameraFar, inout uint clusterIndex)
	{
		clusterIndex = 0;

		if (clusterSize.x == 0 || clusterSize.y == 0 || clusterSize.z == 0)
			return false;

		if (uv.x < 0.0f || uv.y < 0.0f || uv.x >= 1.0f || uv.y >= 1.0f)
			return false;

		const float nearZ = max(cameraNear, 0.001f);
		const float farZ = max(cameraFar, nearZ + 0.001f);
		const float logRange = log(farZ / nearZ);
		if (logRange <= 0.0f)
			return false;

		const float z = max(viewZ, nearZ);
		const uint clusterZ = (uint)(log(z / nearZ) * float(clusterSize.z) / logRange);
		const uint3 cluster = uint3(uint2(uv * float2(clusterSize.xy)), clusterZ);

		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (clusterSize.x * cluster.y) + (clusterSize.x * clusterSize.y * cluster.z);
		return true;
	}

	bool GetClusterIndex(in float2 uv, in float viewZ, in uint4 clusterSize, in float cameraNear, in float cameraFar, inout uint clusterIndex)
	{
		return GetClusterIndex(uv, viewZ, clusterSize.xyz, cameraNear, cameraFar, clusterIndex);
	}

	bool TryGetCluster(in float2 uv, in float viewZ, in uint3 clusterSize, in float cameraNear, in float cameraFar, out uint lightOffset, out uint lightCount)
	{
		uint clusterIndex = 0;
		lightOffset = 0;
		lightCount = 0;

		if (!GetClusterIndex(uv, viewZ, clusterSize, cameraNear, cameraFar, clusterIndex))
			return false;

		LightGrid grid = lightGrid[clusterIndex];
		lightOffset = grid.offset;
		lightCount = min(grid.lightCount, (uint)MAX_CLUSTER_LIGHTS);
		return true;
	}

	bool TryGetCluster(in float2 uv, in float viewZ, in uint4 clusterSize, in float cameraNear, in float cameraFar, out uint lightOffset, out uint lightCount)
	{
		return TryGetCluster(uv, viewZ, clusterSize.xyz, cameraNear, cameraFar, lightOffset, lightCount);
	}

	bool IsLightIgnored(Light light)
	{
		// Shadow-casting point lights are deferred (DFLight) in FO4; skip them
		// so forward clustered lighting never double-lights them.
		return (light.lightFlags & LightFlags::Shadow) != 0;
	}

	bool GetClusteredLight(in uint lightIndex, in uint clusterLightOffset, inout Light light)
	{
		light = lights[lightList[clusterLightOffset + lightIndex]];
		return !IsLightIgnored(light);
	}
}

#endif // LLF_LIGHT_LIMIT_FIX_HLSLI
