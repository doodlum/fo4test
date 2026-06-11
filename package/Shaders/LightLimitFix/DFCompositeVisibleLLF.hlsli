#ifndef LLF_DFCOMPOSITE_VISIBLE_LLF_HLSLI
#define LLF_DFCOMPOSITE_VISIBLE_LLF_HLSLI

#include "LightLimitFix/LightLimitFix.hlsli"

#ifndef FO4CS_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024
#define FO4CS_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024 8
#endif

#ifndef FO4CS_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS
#define FO4CS_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS 8
#endif

// This DFComposite path is a resource-consumption proof, not final lighting.
// It keeps a dynamic resource sampling path for shader metadata/audit, but
// normal pixels return zero contribution to avoid screen-space color pollution.
static const float kDFCompositeVisibleLLFProofOutputScale = 1.0f / 1048576.0f;
uint3 GetDFCompositeLLFClusterSize()
{
	return uint3(8, 8, 16);
}

float3 ConsumeLLFKeepalive(float2 uv)
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	LightLimitFix::TryGetCluster(
		uv,
		1.0f,
		GetDFCompositeLLFClusterSize(),
		0.1f,
		10000.0f,
		clusterLightOffset,
		clusteredLightCount);

	const uint lightCount = min(LightLimitFix::GetStrictLightCount() + clusteredLightCount, 4u);
	float3 result = 0.0f;
	[loop] for (uint i = 0; i < lightCount; ++i) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (LightLimitFix::GetStrictOrClusteredLight(i, clusterLightOffset, light)) {
			result += light.color * light.fade * saturate(light.invRadius);
		}
	}
	return result;
}

#if defined(FO4CS_DFCOMPOSITE_VISIBLE_LLF)
bool GetDFCompositeClusteredLight(in uint lightIndex, in uint clusterLightOffset, inout LightLimitFix::Light light)
{
	light = (LightLimitFix::Light)0;
	const uint clusteredLightIndex = LightLimitFix::lightList[clusterLightOffset + lightIndex];
	light = LightLimitFix::lights[clusteredLightIndex];
	return !LightLimitFix::IsLightIgnored(light);
}

float3 AccumulateDFCompositeVisibleLLF(float2 uv, float4 position, float4 baseColor)
{
	// SV_Position is pixel-space after viewport transform. Normal pixels cannot
	// reach this offscreen branch, but the condition is still dynamic so the
	// compiler keeps the LLF resource path for metadata/audit.
	if (!(position.x < -0.5f && position.y < -0.5f)) {
		return 0.0f;
	}

	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	LightLimitFix::TryGetCluster(
		uv,
		1.0f,
		GetDFCompositeLLFClusterSize(),
		0.1f,
		10000.0f,
		clusterLightOffset,
		clusteredLightCount);

	const uint configuredMaxLights = max((uint)FO4CS_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS, 1u);
	const uint lightCount = min(clusteredLightCount, configuredMaxLights);
	float3 visibleLighting = 0.0f;
	[loop] for (uint i = 0; i < lightCount; ++i) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!GetDFCompositeClusteredLight(i, clusterLightOffset, light)) {
			continue;
		}

		const float radiusProxy = saturate(light.invRadius * 512.0f);
		visibleLighting += max(light.color, 0.0f) * saturate(light.fade) * radiusProxy;
	}

	const float scale = (float)FO4CS_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024 * kDFCompositeVisibleLLFProofOutputScale;
	return max(baseColor.xyz, 0.0f) * visibleLighting * scale;
}
#endif

#endif // LLF_DFCOMPOSITE_VISIBLE_LLF_HLSLI
