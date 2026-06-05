// Runtime proof shader for FO4 PreNG LLF-only additive DFLight contribution.
// It leaves vanilla DFLight intact, then adds a low-scale LLF contribution.

#include "LightLimitFix/DFLightCommon.hlsli"

cbuffer LLFAdditiveControls : register(b13)
{
	float LLFAdditiveScale;
	uint LLFAdditiveMaxLights;
	uint LLFAdditiveFlags;
	uint LLFAdditivePad0;
}

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;
	output.target0 = 0.0f;
	output.target1 = 0.0f;

	const float2 uv = GetDFLightDepthSizeUV(input.position);
	const float depth = SampleDFLightDepth(uv);
	const float3 positionWS = ReconstructDFLightPosition(uv, depth);
	const float viewZ = max(abs(positionWS.z), 0.001f);
	const float3 normalWS = DecodeDFLightNormal(DFLightGBuffer1.Sample(DFLightSampler1, uv).xy);

	const float3 llfLighting = AccumulateLLFDFLight(uv, viewZ, positionWS, normalWS, LLFAdditiveMaxLights) * max(LLFAdditiveScale, 0.0f);
	output.target0 = float4(llfLighting, 0.0f);

	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeDFLightVanillaKeepAlive(uv);
		output.target0 += diagnostic.xxxx;
		output.target1 += diagnostic.xxxx;
	}

	return output;
}
