// Compile-only probe for FO4 PreNG DFLight shader-side integration.
// This file is not bound by the runtime. It mirrors the observed active
// DFLight fullscreen/deferred PS shape while forcing LLF b3/t35-t37 usage.

#include "LightLimitFix/LightLimitFix.hlsli"

cbuffer DFLightFrameData : register(b2)
{
	float4 DFLightCB2[28];
}

cbuffer DFLightEffectData : register(b12)
{
	float4 DFLightCB12[31];
}

Texture2D<float4> DFLightGBuffer0 : register(t0);
Texture2D<float4> DFLightGBuffer1 : register(t1);
Texture2D<float4> DFLightGBuffer2 : register(t2);
Texture2D<float4> DFLightGBuffer3 : register(t3);
Texture2DArray<float> DFLightShadowMap : register(t5);

SamplerState DFLightSampler0 : register(s0);
SamplerState DFLightSampler1 : register(s1);
SamplerState DFLightSampler2 : register(s2);
SamplerState DFLightSampler3 : register(s3);
SamplerComparisonState DFLightShadowSampler : register(s5);

struct DFLightPSInput
{
	float4 position : SV_Position;
};

struct DFLightPSOutput
{
	float4 target0 : SV_Target0;
	float4 target1 : SV_Target1;
};

float3 AccumulateLLFProbe(float2 uv, float viewZ)
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	const uint3 probeClusterSize = uint3(16, 16, 32);

	LightLimitFix::TryGetCluster(
		uv,
		max(viewZ, 0.001f),
		probeClusterSize,
		0.1f,
		10000.0f,
		clusterLightOffset,
		clusteredLightCount);

	const uint lightCount = min(LightLimitFix::GetStrictLightCount() + clusteredLightCount, 4u);
	float3 result = 0.0f;
	[loop] for (uint i = 0; i < lightCount; ++i) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!LightLimitFix::GetStrictOrClusteredLight(i, clusterLightOffset, light)) {
			continue;
		}

		result += light.color * light.fade * saturate(light.invRadius);
	}

	return result;
}

float SampleDFLightShadowProbe(float2 uv)
{
	const float2 shadowTexel = 1.0f / max(abs(DFLightCB2[27].zw), float2(1.0f, 1.0f));
	float shadow = 0.0f;

	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv), 0.0f), 0.5f);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel * float2(1.0f, 0.0f)), 0.0f), 0.5f);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel * float2(-1.0f, 0.0f)), 0.0f), 0.5f);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel * float2(0.0f, 1.0f)), 0.0f), 0.5f);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel * float2(0.0f, -1.0f)), 0.0f), 0.5f);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel * float2(1.0f, 1.0f)), 0.0f), 0.5f);

	return shadow * (1.0f / 6.0f);
}

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;

	const float2 viewportSize = max(abs(DFLightCB2[0].xy), float2(1.0f, 1.0f));
	const float2 uv = saturate(input.position.xy / viewportSize);
	const float viewZ = max(abs(input.position.z), 0.001f);

	const float4 g0 = DFLightGBuffer0.Sample(DFLightSampler0, uv);
	const float4 g1 = DFLightGBuffer1.Sample(DFLightSampler1, uv);
	const float4 g2 = DFLightGBuffer2.Sample(DFLightSampler2, uv);
	const float4 g3 = DFLightGBuffer3.Sample(DFLightSampler3, uv);
	const float shadowProbe = SampleDFLightShadowProbe(uv);

	const float3 llfProbe = AccumulateLLFProbe(uv, viewZ);
	const float3 vanillaProbe = ((g0.xyz + g1.xyz + g2.xyz + g3.xyz + DFLightCB12[0].xyz) * 1.0e-7f) + ((DFLightCB2[27].xyz + DFLightCB12[30].xyz) * 1.0e-8f);
	const float keepAlive = shadowProbe * 1.0e-7f;

	output.target0 = float4(vanillaProbe + (llfProbe * 0.0001f) + keepAlive, 0.0f);
	output.target1 = float4(llfProbe * 0.0001f, 0.0f);
	return output;
}
