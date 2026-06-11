// Compile-only probe for FO4 PreNG BSLighting shader-side LLF integration.
// This shader is not vanilla-equivalent and must not be bound as a visible
// replacement. It only validates that BSLighting descriptors can compile an
// owned pixel shader declaring vanilla resources plus LLF b3/t35-t37.

#include "LightLimitFix/LightLimitFix.hlsli"

cbuffer BSLightingRuntimeData : register(b1)
{
	float4 BSLightingCB1[8];
}

cbuffer BSLightingLightData : register(b2)
{
	float4 BSLightingCB2[38];
}

Texture2D<float4> BSLightingTexture0 : register(t0);
Texture2D<float4> BSLightingTexture1 : register(t1);
Texture2D<float4> BSLightingTexture2 : register(t2);
TextureCube<float4> BSLightingTexture4 : register(t4);
Texture2D<float4> BSLightingTexture6 : register(t6);
Texture2DArray<float4> BSLightingTexture14 : register(t14);
Texture2D<float4> BSLightingTexture15 : register(t15);

SamplerState BSLightingSampler0 : register(s0);
SamplerState BSLightingSampler1 : register(s1);
SamplerState BSLightingSampler2 : register(s2);
SamplerState BSLightingSampler4 : register(s4);
SamplerState BSLightingSampler6 : register(s6);
SamplerState BSLightingSampler14 : register(s14);
SamplerState BSLightingSampler15 : register(s15);

struct BSLightingPSInput
{
	float4 position : SV_Position;
	float2 texCoord0 : TEXCOORD0;
	float4 texCoord4 : TEXCOORD4;
	float3 tangentX : TEXCOORD1;
	float3 tangentY : TEXCOORD2;
	float3 tangentZ : TEXCOORD3;
	float3 viewVector : TEXCOORD5;
	float4 color : COLOR0;
	bool isFrontFace : SV_IsFrontFace;
};

struct BSLightingPSOutput
{
	float4 target0 : SV_Target0;
};

float3 GetBSLightingProbeDirection(BSLightingPSInput input)
{
	float3 direction = input.viewVector;
	if (dot(direction, direction) < 1.0e-4f) {
		direction = input.tangentZ;
	}

	if (dot(direction, direction) < 1.0e-4f) {
		direction = float3(0.0f, 0.0f, 1.0f);
	}

	return normalize(direction);
}

float3 ConsumeBSLightingVanillaProbe(BSLightingPSInput input)
{
	const float2 uv = saturate(input.texCoord0);
	const float3 direction = GetBSLightingProbeDirection(input);
	float3 result = 0.0f;

	result += BSLightingTexture0.Sample(BSLightingSampler0, uv).xyz;
	result += BSLightingTexture1.Sample(BSLightingSampler1, uv).xyz;
	result += BSLightingTexture2.Sample(BSLightingSampler2, uv).xyz;
	result += BSLightingTexture4.Sample(BSLightingSampler4, direction).xyz;
	result += BSLightingTexture6.SampleLevel(BSLightingSampler6, uv, 0.0f).xyz;
	result += BSLightingTexture14.Sample(BSLightingSampler14, float3(uv, 0.0f)).xyz;
	result += BSLightingTexture15.SampleLevel(BSLightingSampler15, uv, 0.0f).xyz;
	result += BSLightingCB1[7].xyz + BSLightingCB2[0].xyz + BSLightingCB2[19].www + BSLightingCB2[37].www;

	return result * 1.0e-7f;
}

float3 AccumulateBSLightingLLFProbe(BSLightingPSInput input)
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	const uint3 probeClusterSize = uint3(8, 8, 16);
	const float2 uv = saturate(input.texCoord0);
	const float viewZ = max(abs(input.position.z), 0.001f);

	LightLimitFix::TryGetCluster(
		uv,
		viewZ,
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

BSLightingPSOutput main(BSLightingPSInput input)
{
	BSLightingPSOutput output;
	output.target0 = 0.0f;

	if (input.position.x < -0.5f) {
		const float3 vanillaProbe = ConsumeBSLightingVanillaProbe(input);
		const float3 llfProbe = AccumulateBSLightingLLFProbe(input);
		output.target0 = float4(vanillaProbe + (llfProbe * 1.0e-4f), 0.0f);
	}

	return output;
}
