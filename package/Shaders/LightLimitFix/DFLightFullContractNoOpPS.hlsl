// Runtime proof shader for FO4 PreNG full-shadowed DFLight resource contracts.
// Normal visible pixels output zero; the offscreen branch keeps vanilla DFLight
// resources plus LLF b3/t35-t37 declared and consumed by compiled bytecode.

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

struct DFLightPSInput
{
	float4 position : SV_Position;
};

struct DFLightPSOutput
{
	float4 target0 : SV_Target0;
	float4 target1 : SV_Target1;
};

float ConsumeFullDFLightContract(float4 position)
{
	const float2 viewportSize = max(abs(DFLightCB2[0].xy), float2(1.0f, 1.0f));
	const float2 uv = saturate(position.xy / viewportSize);

	float diagnostic = 0.0f;
	diagnostic += dot(DFLightCB2[27], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightCB12[30], float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));

	diagnostic += dot(DFLightGBuffer0.Sample(DFLightSampler0, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer1.Sample(DFLightSampler1, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightGBuffer2.Sample(DFLightSampler2, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));
	diagnostic += dot(DFLightDepth.Sample(DFLightSampler3, uv), float4(1.0e-8f, 2.0e-8f, 3.0e-8f, 4.0e-8f));

	const float2 shadowTexel = 1.0f / max(abs(DFLightCB2[27].zw), float2(1.0f, 1.0f));
	diagnostic += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv), 0.0f), 0.5f) * 1.0e-8f;
	diagnostic += DFLightShadowMap.SampleCmpLevelZero(DFLightShadowSampler, float3(saturate(uv + shadowTexel), 1.0f), 0.5f) * 1.0e-8f;

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

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;
	output.target0 = 0.0f;
	output.target1 = 0.0f;

	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeFullDFLightContract(input.position);
		output.target0 = diagnostic.xxxx;
		output.target1 = diagnostic.xxxx;
	}

	return output;
}
