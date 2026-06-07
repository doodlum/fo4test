// Narrow vanilla-equivalent FO4 PreNG DFComposite descriptor 0x88 shader.
// This is the first visible-safe DFComposite bind proof candidate.

#include "LightLimitFix/LightLimitFix.hlsli"

cbuffer DFCompositeFrameData : register(b2)
{
	float4 DFCompositeCB2[1];
}

Texture2D<float4> DFCompositeTexture0 : register(t0);
Texture2D<float4> DFCompositeTexture3 : register(t3);
Texture2D<float4> DFCompositeTexture4 : register(t4);
Texture2D<float4> DFCompositeTexture5 : register(t5);
Texture2D<float4> DFCompositeTexture6 : register(t6);

SamplerState DFCompositeSampler0 : register(s0);
SamplerState DFCompositeSampler3 : register(s3);
SamplerState DFCompositeSampler4 : register(s4);
SamplerState DFCompositeSampler5 : register(s5);
SamplerState DFCompositeSampler6 : register(s6);

struct DFCompositePSInput
{
	float4 position : SV_Position;
};

struct DFCompositePSOutput
{
	float4 target0 : SV_Target0;
};

float2 GetDFCompositeUV(float4 position)
{
	return position.xy * DFCompositeCB2[0].xy;
}

float3 ConsumeLLFKeepalive(float2 uv)
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	LightLimitFix::TryGetCluster(
		uv,
		1.0f,
		uint3(8, 8, 16),
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

DFCompositePSOutput main(DFCompositePSInput input)
{
	const float2 uv = GetDFCompositeUV(input.position);

	const float4 baseColor = DFCompositeTexture0.SampleLevel(DFCompositeSampler0, uv, 0.0f);
	const float materialClass = DFCompositeTexture3.SampleLevel(DFCompositeSampler3, uv, 0.0f).w;
	const float3 litBase = baseColor.xyz * DFCompositeTexture5.SampleLevel(DFCompositeSampler5, uv, 0.0f).xyz;
	const float3 bloomColor = DFCompositeTexture4.Sample(DFCompositeSampler4, uv).xyz;

	float3 color = litBase * 3.0f;
	if (abs((materialClass * 255.0f) - 5.0f) >= 0.25f) {
		color += bloomColor;
		color += DFCompositeTexture6.SampleLevel(DFCompositeSampler6, uv, 0.0f).xyz;
	}

	if (input.position.x < -0.5f) {
		color += ConsumeLLFKeepalive(uv) * 1.0e-4f;
	}

	DFCompositePSOutput output;
	output.target0 = float4(color, baseColor.w);
	return output;
}
