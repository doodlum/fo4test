#include "LightLimitFix/Common.hlsli"

// Per-frame constant buffer — written by LightLimitFix::Prepass()
// CPU struct: LightCullingCB + ViewMatrix appended
cbuffer PerFrame : register(b0)
{
	uint LightCount;
	uint3 padCB0;               // → 16 bytes

	uint4 ClusterSize;          // grid dimensions (x, y, z, pad)
	                             // → 32 bytes

	float4x4 CameraView;         // world → view-space transform (row-major)
	                             // → 96 bytes
}

StructuredBuffer<ClusterAABB> clusters  : register(t0);
StructuredBuffer<Light>        lights    : register(t1);

RWStructuredBuffer<uint>      lightIndexCounter : register(u0);
RWStructuredBuffer<uint>      lightIndexList    : register(u1);
RWStructuredBuffer<LightGrid> lightGrid         : register(u2);

// AABB vs sphere intersection test (view-space)
bool LightIntersectsCluster(float3 position, float radiusSq, ClusterAABB cluster)
{
	float3 closest = max(cluster.minPoint.xyz, min(position, cluster.maxPoint.xyz));
	float3 delta = closest - position;
	return dot(delta, delta) <= radiusSq;
}

[numthreads(NUMTHREAD_X, NUMTHREAD_Y, NUMTHREAD_Z)]
void main(
	uint3 groupId : SV_GroupID,
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	// Out-of-bounds threads skip
	if (any(dispatchThreadId >= uint3(ClusterSize.x, ClusterSize.y, ClusterSize.z)))
		return;

	uint visibleLightCount = 0;
	uint visibleLightIndices[MAX_CLUSTER_LIGHTS];

	uint clusterIndex = dispatchThreadId.x
	                  + dispatchThreadId.y * ClusterSize.x
	                  + dispatchThreadId.z * (ClusterSize.x * ClusterSize.y);

	ClusterAABB cluster = clusters[clusterIndex];

	// Test each light against this cluster's AABB
	for (uint i = 0; i < LightCount; i++) {
		Light light = lights[i];

		float3 positionVS = mul(CameraView, float4(light.positionWS[0].xyz, 1.0f)).xyz;
		float radiusSq = light.radius * light.radius;

		if (LightIntersectsCluster(positionVS, radiusSq, cluster)) {
			visibleLightIndices[visibleLightCount] = i;
			visibleLightCount++;
			if (visibleLightCount >= MAX_CLUSTER_LIGHTS)
				break;
		}
	}

	// Atomically append visible light indices to the global list
	uint offset = 0;
	InterlockedAdd(lightIndexCounter[0], visibleLightCount, offset);

	for (uint j = 0; j < visibleLightCount; j++) {
		lightIndexList[offset + j] = visibleLightIndices[j];
	}

	LightGrid grid;
	grid.offset = offset;
	grid.lightCount = visibleLightCount;
	grid.pad0 = uint2(0u, 0u);
	lightGrid[clusterIndex] = grid;
}
