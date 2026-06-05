// Runtime candidate for the FO4 PreNG full-shadowed DFLight family.
// This is a proof shader, not a vanilla-equivalent replacement.

#include "LightLimitFix/DFLightCommon.hlsli"

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;

	const float2 uv = GetDFLightViewportUV(input.position);
	const float depth = SampleDFLightDepth(uv);
	const float3 positionWS = ReconstructDFLightPosition(uv, depth);
	const float viewZ = max(abs(positionWS.z), 0.001f);

	const float4 gbuffer2 = DFLightGBuffer2.Sample(DFLightSampler2, uv);
	const float4 gbuffer0 = DFLightGBuffer0.Sample(DFLightSampler0, uv);
	const float2 normalEncoded = DFLightGBuffer1.Sample(DFLightSampler1, uv).xy;
	const float3 normalWS = DecodeDFLightNormal(normalEncoded);

	const float shadow = SampleDFLightShadow(uv, viewZ);
	const float3 llfLighting = AccumulateLLFDFLight(uv, viewZ, positionWS, normalWS) * shadow;

	const float3 vanillaDiffuse = DFLightCB2[2].xyz * (gbuffer0.w + saturate(dot(normalWS, DFLightCB2[1].xyz)));
	const float3 vanillaSpecular = gbuffer2.www * DFLightCB12[30].yyy * DFLightCB2[2].xyz;

	output.target0 = float4((vanillaDiffuse + llfLighting) / 3.0f, 0.0f);
	output.target1 = float4(vanillaSpecular + (llfLighting * 0.25f), 1.0f);
	return output;
}
