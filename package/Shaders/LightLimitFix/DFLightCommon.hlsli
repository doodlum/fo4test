#ifndef LLF_DFLIGHT_COMMON_HLSLI
#define LLF_DFLIGHT_COMMON_HLSLI

#include "LightLimitFix/LightLimitFix.hlsli"

#ifndef LLF_DFLIGHT_HAS_SHADOW_MAP
#define LLF_DFLIGHT_HAS_SHADOW_MAP 1
#endif

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
Texture2D<float4> DFLightDepth : register(t3);
#if LLF_DFLIGHT_HAS_SHADOW_MAP
Texture2DArray<float> DFLightShadowMap : register(t5);
#endif

SamplerState DFLightSampler0 : register(s0);
SamplerState DFLightSampler1 : register(s1);
SamplerState DFLightSampler2 : register(s2);
SamplerState DFLightSampler3 : register(s3);
#if LLF_DFLIGHT_HAS_SHADOW_MAP
SamplerComparisonState DFLightShadowSampler : register(s5);
#endif

struct DFLightPSInput
{
	float4 position : SV_Position;
};

struct DFLightPSOutput
{
	float4 target0 : SV_Target0;
	float4 target1 : SV_Target1;
};

float2 GetDFLightViewportUV(float4 position)
{
	const float2 viewportScale = max(abs(DFLightCB2[0].xy), float2(1.0f, 1.0f));
	return saturate(position.xy * DFLightCB2[27].xy * viewportScale);
}

float2 GetDFLightDepthSizeUV(float4 position)
{
	uint width = 1;
	uint height = 1;
	DFLightDepth.GetDimensions(width, height);
	return saturate(position.xy / max(float2(width, height), float2(1.0f, 1.0f)));
}

float SampleDFLightDepth(float2 uv)
{
	const float2 gradX = ddx(uv.x).xx;
	const float2 gradY = ddy(uv.y).xx;
	return DFLightDepth.SampleGrad(DFLightSampler3, uv, gradX, gradY).y;
}

float3 ReconstructDFLightPosition(float2 uv, float depth)
{
	const bool nearDepth = depth <= 0.01f;
	const float4 clip = float4((uv.x * 2.0f) - 1.0f, 1.0f - (uv.y * 2.0f), nearDepth ? depth * 100.0f : mad(depth, 1.01f, -0.01f), 1.0f);

	const float4 row0 = nearDepth ? DFLightCB12[24] : DFLightCB12[20];
	const float4 row1 = nearDepth ? DFLightCB12[25] : DFLightCB12[21];
	const float4 row2 = nearDepth ? DFLightCB12[26] : DFLightCB12[22];
	const float4 row3 = nearDepth ? DFLightCB12[27] : DFLightCB12[23];
	const float4 view = float4(dot(row0, clip), dot(row1, clip), dot(row2, clip), dot(row3, clip));

	return view.xyz / max(abs(view.w), 1.0e-4f);
}

float3 DecodeDFLightNormal(float2 encodedNormal)
{
	const float2 normalXY = mad(encodedNormal, 4.0f, -2.0f);
	const float xyLengthSq = dot(normalXY, normalXY);
	const float normalScale = sqrt(1.0f - (xyLengthSq * 0.25f));
	return float3(normalXY * normalScale, (xyLengthSq * 0.5f) - 1.0f);
}

#if LLF_DFLIGHT_HAS_SHADOW_MAP
float SampleDFLightShadow(float2 uv, float viewZ)
{
	const float shadowScale = max(abs(DFLightCB2[20].z), 1.0f) * 3.0f;
	const float2 shadowTexel = 1.0f / max(abs(DFLightCB2[27].zw), float2(1.0f, 1.0f));
	const float compareValue = saturate((viewZ - DFLightCB2[21].z) / max(DFLightCB2[21].w - DFLightCB2[21].z, 1.0e-4f));
	const float2 baseUV = saturate(uv * shadowScale);

	float shadow = 0.0f;
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(baseUV, 0.0f), compareValue);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(baseUV + shadowTexel * float2(1.0f, 0.0f)), 0.0f), compareValue);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(baseUV + shadowTexel * float2(-1.0f, 0.0f)), 1.0f), compareValue);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(baseUV + shadowTexel * float2(0.0f, 1.0f)), 1.0f), compareValue);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(baseUV + shadowTexel * float2(0.0f, -1.0f)), 2.0f), compareValue);
	shadow += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(baseUV + shadowTexel * float2(1.0f, 1.0f)), 2.0f), compareValue);
	return shadow * (1.0f / 6.0f);
}
#endif

float3 EvaluateLLFDFLight(LightLimitFix::Light light, float3 positionWS, float3 normalWS)
{
	const float3 toLight = light.positionWS[0].xyz - positionWS;
	const float distanceSq = max(dot(toLight, toLight), 1.0e-4f);
	const float invDistance = rsqrt(distanceSq);
	const float3 lightDir = toLight * invDistance;
	const float distance = distanceSq * invDistance;
	const float attenuation = saturate(1.0f - (distance * max(light.invRadius, 0.0f)));
	const float diffuse = saturate(dot(normalWS, lightDir));
	return light.color * light.fade * attenuation * attenuation * diffuse;
}

float3 AccumulateLLFDFLight(float2 uv, float viewZ, float3 positionWS, float3 normalWS, uint maxLights)
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	const uint3 clusterSize = uint3(8, 8, 16);
	LightLimitFix::TryGetCluster(
		uv,
		max(abs(viewZ), 0.001f),
		clusterSize,
		0.1f,
		10000.0f,
		clusterLightOffset,
		clusteredLightCount);

	const uint lightCount = min(LightLimitFix::GetStrictLightCount() + clusteredLightCount, max(maxLights, 1u));
	float3 result = 0.0f;
	[loop] for (uint i = 0; i < lightCount; ++i) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!LightLimitFix::GetStrictOrClusteredLight(i, clusterLightOffset, light)) {
			continue;
		}

		result += EvaluateLLFDFLight(light, positionWS, normalWS);
	}

	return result;
}

float3 AccumulateLLFDFLight(float2 uv, float viewZ, float3 positionWS, float3 normalWS)
{
	return AccumulateLLFDFLight(uv, viewZ, positionWS, normalWS, LightLimitFix::MaxStrictLights + MAX_CLUSTER_LIGHTS);
}

float ConsumeDFLightFullContract(float4 position)
{
	const float2 uv = GetDFLightViewportUV(position);
	float diagnostic = 0.0f;

	diagnostic += dot(DFLightCB2[27], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightCB12[30], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer0.Sample(DFLightSampler0, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer1.Sample(DFLightSampler1, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer2.Sample(DFLightSampler2, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightDepth.Sample(DFLightSampler3, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));

#if LLF_DFLIGHT_HAS_SHADOW_MAP
	const float2 shadowTexel = 1.0f / max(abs(DFLightCB2[27].zw), float2(1.0f, 1.0f));
	diagnostic += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv), 0.0f), 0.5f) * 1.0e-8f;
	diagnostic += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel), 1.0f), 0.5f) * 1.0e-8f;
#endif

	const uint strictCount = LightLimitFix::GetStrictLightCount();
	diagnostic += (float)strictCount * 1.0e-8f;
	diagnostic += (float)(LightLimitFix::ShadowBitMask & 0xFFFFu) * 1.0e-10f;

	if (strictCount > 0) {
		const LightLimitFix::Light strictLight = LightLimitFix::StrictLights[0];
		diagnostic += dot(strictLight.color, float3(1.0e-8f, 2.0e-8f, 3.0e-8f));
	}

	const LightLimitFix::LightGrid grid = LightLimitFix::lightGrid[0];
	const uint listHead = LightLimitFix::lightList[0];
	const LightLimitFix::Light clusteredLight = LightLimitFix::lights[0];
	diagnostic += (float)(grid.offset + grid.lightCount + listHead) * 1.0e-10f;
	diagnostic += dot(clusteredLight.color, float3(1.0e-8f, 2.0e-8f, 3.0e-8f));

	return diagnostic;
}

float ConsumeDFLightVanillaKeepAlive(float2 uv)
{
	float diagnostic = 0.0f;
	diagnostic += dot(DFLightCB2[27], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightCB12[30], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer0.Sample(DFLightSampler0, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer2.Sample(DFLightSampler2, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
#if LLF_DFLIGHT_HAS_SHADOW_MAP
	diagnostic += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv), 0.0f), 0.5f) * 1.0e-8f;
#endif
	return diagnostic;
}

#endif // LLF_DFLIGHT_COMMON_HLSLI
