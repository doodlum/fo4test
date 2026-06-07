// Runtime candidate for the FO4 PreNG full-shadowed DFLight family.
// Visible output intentionally stays vanilla-approx while the offscreen branch
// keeps the full DFLight + LLF contract declared for focused diagnostics.

#include "LightLimitFix/DFLightCommon.hlsli"

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;

	const float2 uv = GetDFLightViewportUV(input.position);
	const float4 gbuffer2 = DFLightGBuffer2.Sample(DFLightSampler2, uv);
	const float4 gbuffer0 = DFLightGBuffer0.Sample(DFLightSampler0, uv);
	const float2 normalEncoded = DFLightGBuffer1.Sample(DFLightSampler1, uv).xy;
	const float3 normalWS = DecodeDFLightNormal(normalEncoded);

	const float3 vanillaDiffuse = DFLightCB2[2].xyz * (gbuffer0.w + saturate(dot(normalWS, DFLightCB2[1].xyz)));
	const float3 vanillaSpecular = gbuffer2.www * DFLightCB12[30].yyy * DFLightCB2[2].xyz;

	output.target0 = float4(vanillaDiffuse / 3.0f, 0.0f);
	output.target1 = float4(vanillaSpecular, 1.0f);

	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeDFLightFullContract(input.position);
		output.target0 += diagnostic.xxxx;
		output.target1 += diagnostic.xxxx;
	}

	return output;
}
