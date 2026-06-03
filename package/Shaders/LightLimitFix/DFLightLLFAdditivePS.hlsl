// Runtime proof shader for FO4 PreNG LLF-only additive DFLight contribution.
// It leaves vanilla DFLight intact, then adds a low-scale LLF contribution.

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
Texture2D<float4> DFLightDepth : register(t3);
Texture2DArray<float> DFLightShadowMap : register(t5);

SamplerState DFLightSampler0 : register(s0);
SamplerState DFLightSampler1 : register(s1);
SamplerState DFLightSampler2 : register(s2);
SamplerState DFLightSampler3 : register(s3);
SamplerComparisonState DFLightShadowSampler : register(s5);

cbuffer LLFAdditiveControls : register(b13)
{
	float LLFAdditiveScale;
	uint LLFAdditiveMaxLights;
	uint LLFAdditiveFlags;
	uint LLFAdditivePad0;
}

struct DFLightPSInput
{
	float4 position : SV_Position;
};

struct DFLightPSOutput
{
	float4 target0 : SV_Target0;
	float4 target1 : SV_Target1;
};

float2 GetDFLightUV(float4 position)
{
	uint width = 1;
	uint height = 1;
	DFLightDepth.GetDimensions(width, height);
	return saturate(position.xy / max(float2(width, height), float2(1.0f, 1.0f)));
}

float SampleDFLightDepth(float2 uv)
{
	return DFLightDepth.SampleGrad(DFLightSampler3, uv, ddx(uv), ddy(uv)).z;
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
	const float xyLengthSq = saturate(dot(normalXY, normalXY));
	const float z = -sqrt(saturate(1.0f - (xyLengthSq * 0.25f)));
	return normalize(float3(normalXY * sqrt(saturate(1.0f - (xyLengthSq * 0.25f))), z));
}

float3 EvaluateLLFLight(LightLimitFix::Light light, float3 positionWS, float3 normalWS)
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

float3 AccumulateLLFDFLight(float2 uv, float viewZ, float3 positionWS, float3 normalWS)
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

	const uint lightCount = min(LightLimitFix::GetStrictLightCount() + clusteredLightCount, max(LLFAdditiveMaxLights, 1u));
	float3 result = 0.0f;
	[loop] for (uint i = 0; i < lightCount; ++i) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!LightLimitFix::GetStrictOrClusteredLight(i, clusterLightOffset, light)) {
			continue;
		}

		result += EvaluateLLFLight(light, positionWS, normalWS);
	}

	return result;
}

float ConsumeFullContractKeepAlive(float2 uv)
{
	float diagnostic = 0.0f;
	diagnostic += dot(DFLightCB2[27], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightCB12[30], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer0.Sample(DFLightSampler0, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer2.Sample(DFLightSampler2, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv), 0.0f), 0.5f) * 1.0e-8f;
	return diagnostic;
}

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;
	output.target0 = 0.0f;
	output.target1 = 0.0f;

	const float2 uv = GetDFLightUV(input.position);
	const float depth = SampleDFLightDepth(uv);
	const float3 positionWS = ReconstructDFLightPosition(uv, depth);
	const float viewZ = max(abs(positionWS.z), 0.001f);
	const float3 normalWS = DecodeDFLightNormal(DFLightGBuffer1.Sample(DFLightSampler1, uv).xy);

	const float3 llfLighting = AccumulateLLFDFLight(uv, viewZ, positionWS, normalWS) * max(LLFAdditiveScale, 0.0f);
	output.target0 = float4(llfLighting, 0.0f);

	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeFullContractKeepAlive(uv);
		output.target0 += diagnostic.xxxx;
		output.target1 += diagnostic.xxxx;
	}

	return output;
}
