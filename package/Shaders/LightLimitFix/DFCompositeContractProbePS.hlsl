// Compile-only probe for FO4 PreNG DFComposite shader-side integration.
// The runtime must not bind this shader as a visible replacement; it exists to
// validate descriptor-owned compilation and the DFComposite + LLF resource contract.

#include "LightLimitFix/LightLimitFix.hlsli"

cbuffer DFCompositeRuntimeData : register(b0)
{
	float4 DFCompositeCB0[3];
}

cbuffer DFCompositeFrameData : register(b2)
{
	float4 DFCompositeCB2[3];
}

cbuffer DFCompositeEffectData : register(b12)
{
	float4 DFCompositeCB12[47];
}

Texture2D<float4> DFCompositeTexture0 : register(t0);
Texture2D<float4> DFCompositeTexture1 : register(t1);
Texture2D<float4> DFCompositeTexture2 : register(t2);
Texture2D<float4> DFCompositeTexture3 : register(t3);
Texture2D<float4> DFCompositeTexture4 : register(t4);
Texture2D<float4> DFCompositeTexture5 : register(t5);
Texture2D<float4> DFCompositeTexture6 : register(t6);
Texture2D<float4> DFCompositeTexture7 : register(t7);
TextureCubeArray<float4> DFCompositeCubeArray8 : register(t8);
Texture2D<float4> DFCompositeTexture9 : register(t9);
Texture2D<float4> DFCompositeTexture10 : register(t10);
Texture2D<float4> DFCompositeTexture11 : register(t11);
Texture2D<float4> DFCompositeTexture12 : register(t12);
Texture2D<float4> DFCompositeTexture14 : register(t14);
Texture2D<float4> DFCompositeTexture15 : register(t15);

SamplerState DFCompositeSampler0 : register(s0);
SamplerState DFCompositeSampler1 : register(s1);
SamplerState DFCompositeSampler2 : register(s2);
SamplerState DFCompositeSampler3 : register(s3);
SamplerState DFCompositeSampler4 : register(s4);
SamplerState DFCompositeSampler5 : register(s5);
SamplerState DFCompositeSampler6 : register(s6);
SamplerState DFCompositeSampler7 : register(s7);
SamplerState DFCompositeSampler8 : register(s8);
SamplerState DFCompositeSampler9 : register(s9);
SamplerState DFCompositeSampler10 : register(s10);
SamplerState DFCompositeSampler11 : register(s11);
SamplerState DFCompositeSampler12 : register(s12);
SamplerState DFCompositeSampler14 : register(s14);
SamplerState DFCompositeSampler15 : register(s15);

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
	return saturate(position.xy * max(abs(DFCompositeCB2[0].xy), float2(1.0e-6f, 1.0e-6f)));
}

float3 AccumulateDFCompositeLLFProbe(float2 uv)
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	const uint3 probeClusterSize = uint3(8, 8, 16);
	LightLimitFix::TryGetCluster(
		uv,
		1.0f,
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

float3 ConsumeDFCompositeVanillaProbe(float2 uv)
{
	float3 result = 0.0f;
	result += DFCompositeTexture0.SampleLevel(DFCompositeSampler0, uv, 0.0f).xyz;
	result += DFCompositeTexture1.SampleLevel(DFCompositeSampler1, uv, 0.0f).xyz;
	result += DFCompositeTexture2.SampleLevel(DFCompositeSampler2, uv, 0.0f).xyz;
	result += DFCompositeTexture3.SampleLevel(DFCompositeSampler3, uv, 0.0f).xyz;
	result += DFCompositeTexture4.Sample(DFCompositeSampler4, uv).xyz;
	result += DFCompositeTexture5.SampleLevel(DFCompositeSampler5, uv, 0.0f).xyz;
	result += DFCompositeTexture6.SampleLevel(DFCompositeSampler6, uv, 0.0f).xyz;
	result += DFCompositeTexture7.SampleLevel(DFCompositeSampler7, uv, 0.0f).xyz;
	result += DFCompositeCubeArray8.SampleLevel(DFCompositeSampler8, float4(0.0f, 0.0f, 1.0f, 0.0f), 0.0f).xyz;
	result += DFCompositeTexture9.Sample(DFCompositeSampler9, uv).xyz;
	result += DFCompositeTexture10.SampleLevel(DFCompositeSampler10, uv, 0.0f).xyz;
	result += DFCompositeTexture11.SampleLevel(DFCompositeSampler11, uv, 0.0f).xyz;
	result += DFCompositeTexture12.SampleLevel(DFCompositeSampler12, uv, 0.0f).xyz;
	result += DFCompositeTexture14.Sample(DFCompositeSampler14, uv).xyz;
	result += DFCompositeTexture15.SampleLevel(DFCompositeSampler15, uv, 0.0f).xyz;
	result += DFCompositeCB0[0].xyz + DFCompositeCB2[0].xyz + DFCompositeCB12[30].xyz;
	return result * 1.0e-7f;
}

DFCompositePSOutput main(DFCompositePSInput input)
{
	DFCompositePSOutput output;
	output.target0 = 0.0f;

	if (input.position.x < -0.5f) {
		const float2 uv = GetDFCompositeUV(input.position);
		const float3 vanillaProbe = ConsumeDFCompositeVanillaProbe(uv);
		const float3 llfProbe = AccumulateDFCompositeLLFProbe(uv);
		output.target0 = float4(vanillaProbe + (llfProbe * 1.0e-4f), 0.0f);
	}

	return output;
}
