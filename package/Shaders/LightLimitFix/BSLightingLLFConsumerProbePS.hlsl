// Descriptor-specific compile probe for the FO4 PreNG BSLighting LLF consumer.
// This shader is intentionally not a visible replacement. It matches the
// observed BSLighting descriptor resource family and keeps the LLF b3/t35-t37
// contract alive behind an offscreen branch for metadata/runtime validation.

#include "LightLimitFix/LightLimitFix.hlsli"

#ifndef FO4CS_SHADER_DESCRIPTOR
#define FO4CS_SHADER_DESCRIPTOR 0
#endif

#define BSLIGHTING_DESC_1   1
#define BSLIGHTING_DESC_101 257
#define BSLIGHTING_DESC_111 273
#define BSLIGHTING_DESC_141 321
#define BSLIGHTING_DESC_201 513

#if FO4CS_SHADER_DESCRIPTOR == BSLIGHTING_DESC_101 || \
	FO4CS_SHADER_DESCRIPTOR == BSLIGHTING_DESC_111 || \
	FO4CS_SHADER_DESCRIPTOR == BSLIGHTING_DESC_141
#define BSLIGHTING_HAS_CUBE 1
#else
#define BSLIGHTING_HAS_CUBE 0
#endif

#if FO4CS_SHADER_DESCRIPTOR == BSLIGHTING_DESC_201
#define BSLIGHTING_HAS_AUX_6 1
#else
#define BSLIGHTING_HAS_AUX_6 0
#endif

#if FO4CS_SHADER_DESCRIPTOR == BSLIGHTING_DESC_141
#define BSLIGHTING_HAS_AUX_15 1
#else
#define BSLIGHTING_HAS_AUX_15 0
#endif

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
#if BSLIGHTING_HAS_CUBE
TextureCube<float4> BSLightingTexture4 : register(t4);
#endif
#if BSLIGHTING_HAS_AUX_6
Texture2D<float4> BSLightingTexture6 : register(t6);
#endif
Texture2DArray<float4> BSLightingTexture14 : register(t14);
#if BSLIGHTING_HAS_AUX_15
Texture2D<float4> BSLightingTexture15 : register(t15);
#endif

SamplerState BSLightingSampler0 : register(s0);
SamplerState BSLightingSampler1 : register(s1);
SamplerState BSLightingSampler2 : register(s2);
#if BSLIGHTING_HAS_CUBE
SamplerState BSLightingSampler4 : register(s4);
#endif
#if BSLIGHTING_HAS_AUX_6
SamplerState BSLightingSampler6 : register(s6);
#endif
SamplerState BSLightingSampler14 : register(s14);
#if BSLIGHTING_HAS_AUX_15
SamplerState BSLightingSampler15 : register(s15);
#endif

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
	float3 direction = input.viewVector + (input.tangentZ * 0.25f);
	if (dot(direction, direction) < 1.0e-4f) {
		direction = float3(0.0f, 0.0f, 1.0f);
	}

	return normalize(direction);
}

float3 ConsumeBSLightingVanillaResources(BSLightingPSInput input)
{
	const float2 uv = saturate(input.texCoord0);
	const float3 direction = GetBSLightingProbeDirection(input);
	float3 result = 0.0f;

	result += BSLightingTexture0.Sample(BSLightingSampler0, uv).xyz;
	result += BSLightingTexture1.Sample(BSLightingSampler1, uv).xyz;
	result += BSLightingTexture2.Sample(BSLightingSampler2, uv).xyz;
#if BSLIGHTING_HAS_CUBE
	result += BSLightingTexture4.Sample(BSLightingSampler4, direction).xyz;
#endif
#if BSLIGHTING_HAS_AUX_6
	result += BSLightingTexture6.Sample(BSLightingSampler6, uv).xyz;
#endif
	result += BSLightingTexture14.Sample(BSLightingSampler14, float3(uv, 0.0f)).xyz;
#if BSLIGHTING_HAS_AUX_15
	result += BSLightingTexture15.SampleLevel(BSLightingSampler15, uv, 0.0f).xyz;
#endif

	result += BSLightingCB1[7].xyz;
	result += BSLightingCB2[0].xyz + BSLightingCB2[3].yzw + BSLightingCB2[37].www;
	return result;
}

float3 ConsumeBSLightingLLFResources(BSLightingPSInput input)
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

		result += max(light.color, 0.0f) * saturate(light.fade) * saturate(light.invRadius);
	}

	return result;
}

BSLightingPSOutput main(BSLightingPSInput input)
{
	BSLightingPSOutput output;
	output.target0 = 0.0f;

	if (input.position.x < -0.5f && input.position.y < -0.5f) {
		const float3 vanillaProbe = ConsumeBSLightingVanillaResources(input);
		const float3 llfProbe = ConsumeBSLightingLLFResources(input);
		output.target0 = float4((vanillaProbe * 1.0e-7f) + (llfProbe * 1.0e-7f), 0.0f);
	}

	return output;
}
