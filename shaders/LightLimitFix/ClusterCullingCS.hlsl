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

// NOTE: the C++ side still binds a 1-element lightIndexCounter UAV at slot 0 and
// clears it each frame, but the shader no longer reads or writes it. Slot indices
// are kept stable (list=u1, grid=u2) so the host bindings remain correct.
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
	// Out-of-bounds threads skip (the host over-dispatches with a ceil grid).
	if (any(dispatchThreadId >= uint3(ClusterSize.x, ClusterSize.y, ClusterSize.z)))
		return;

	uint clusterIndex = dispatchThreadId.x
	                  + dispatchThreadId.y * ClusterSize.x
	                  + dispatchThreadId.z * (ClusterSize.x * ClusterSize.y);

	ClusterAABB cluster = clusters[clusterIndex];

	// Static per-cluster partition. The light index list buffer is allocated as
	// clusterCount * MAX_CLUSTER_LIGHTS entries (see SetupResources), so cluster N
	// owns slots [N * MAX_CLUSTER_LIGHTS, (N+1) * MAX_CLUSTER_LIGHTS). This removes
	// the atomic counter and the 256-element per-thread array that exceeded the
	// thread-group local-data budget of the original implementation.
	uint base = clusterIndex * MAX_CLUSTER_LIGHTS;
	uint visibleLightCount = 0;

	for (uint i = 0; i < LightCount; i++) {
		Light light = lights[i];

		float3 positionVS = mul(CameraView, float4(light.positionWS[0].xyz, 1.0f)).xyz;
		float radiusSq = light.radius * light.radius;

		if (LightIntersectsCluster(positionVS, radiusSq, cluster)) {
			if (visibleLightCount < MAX_CLUSTER_LIGHTS) {
				lightIndexList[base + visibleLightCount] = i;
			}
			visibleLightCount++;
		}
	}

	LightGrid grid;
	grid.offset = base;
	grid.lightCount = min(visibleLightCount, MAX_CLUSTER_LIGHTS);
	grid.pad0 = uint2(0u, 0u);
	lightGrid[clusterIndex] = grid;
}
