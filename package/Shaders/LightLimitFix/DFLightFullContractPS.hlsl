// Runtime consumer candidate for the FO4 PreNG DFLight full-contract descriptor family.
// Default output mirrors the dumped psDesc=0x20201 vanilla shader; visible LLF is opt-in.

#define LLF_DFLIGHT_HAS_SHADOW_MAP 0
#ifndef FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF
#define FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF 0
#endif
#ifndef FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS
#define FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS 16
#endif
#ifndef FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS
#define FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS LightLimitFix::MaxStrictLights
#endif
#ifndef FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS
#define FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS MAX_CLUSTER_LIGHTS
#endif
#include "LightLimitFix/DFLightCommon.hlsli"

struct DFLightFullContractEvaluation
{
	float2 uv;
	float depth;
	float3 positionWS;
	float3 normalWS;
	DFLightPSOutput output;
};

float2 GetDFLightFullContractUV(float4 position)
{
	return position.xy * DFLightCB2[27].xy * DFLightCB2[0].xy;
}

float SampleDFLightFullContractDepth(float2 uv)
{
	const float2 gradX = ddx(uv.x).xx;
	const float2 gradY = ddy(uv.y).xx;
	return DFLightDepth.SampleGrad(DFLightSampler3, uv, gradX, gradY).y;
}

float3 ReconstructDFLightFullContractPosition(float4 position, float depth)
{
	const bool nearDepth = depth <= 0.01f;
	const float2 screen = position.xy * DFLightCB2[0].xy;
	const float4 clip = float4(
		(screen.x * 2.0f) - 1.0f,
		((1.0f - screen.y) * 2.0f) - 1.0f,
		nearDepth ? (depth * 100.0f) : mad(depth, 1.01f, -0.01f),
		1.0f);

	const float4 row0 = nearDepth ? DFLightCB12[24] : DFLightCB12[20];
	const float4 row1 = nearDepth ? DFLightCB12[25] : DFLightCB12[21];
	const float4 row2 = nearDepth ? DFLightCB12[26] : DFLightCB12[22];
	const float4 row3 = nearDepth ? DFLightCB12[27] : DFLightCB12[23];
	const float4 world = float4(dot(row0, clip), dot(row1, clip), dot(row2, clip), dot(row3, clip));

	return world.xyz / world.w;
}

float3 DecodeDFLightFullContractNormal(float2 encodedNormal)
{
	const float2 normalXY = mad(encodedNormal, 4.0f, -2.0f);
	const float normalXYLengthSq = dot(normalXY, normalXY);
	const float normalScale = sqrt(1.0f - (normalXYLengthSq * 0.25f));
	const float normalZ = (normalXYLengthSq * 0.5f) - 1.0f;
	return float3(normalXY * normalScale, normalZ);
}

float3 DFLightFullContractPow3(float3 value, float exponent)
{
	return exp(log(value) * exponent);
}

float DFLightFullContractPow(float value, float exponent)
{
	return exp(log(value) * exponent);
}

float3 TransformDFLightFullContractColor(float3 direction)
{
	const float4 value = float4(direction, 1.0f);
	return DFLightFullContractPow3(
		float3(
			dot(DFLightCB2[6], value),
			dot(DFLightCB2[7], value),
			dot(DFLightCB2[8], value)),
		2.2f);
}

DFLightFullContractEvaluation EvaluateDFLightFullContractVanilla(float4 position)
{
	DFLightFullContractEvaluation evaluation;

	evaluation.uv = GetDFLightFullContractUV(position);
	evaluation.depth = SampleDFLightFullContractDepth(evaluation.uv);
	evaluation.positionWS = ReconstructDFLightFullContractPosition(position, evaluation.depth);

	const float4 gbuffer2 = DFLightGBuffer2.Sample(DFLightSampler2, evaluation.uv);
	const float4 gbuffer0 = DFLightGBuffer0.Sample(DFLightSampler0, evaluation.uv);
	const float2 normalEncoded = DFLightGBuffer1.Sample(DFLightSampler1, evaluation.uv).xy;
	evaluation.normalWS = DecodeDFLightFullContractNormal(normalEncoded);

	const float3 viewDir = normalize(-evaluation.positionWS);
	const float3 lightDir = DFLightCB2[1].xyz;
	const float3 lightColor = DFLightCB2[2].xyz;
	const float3 albedo = gbuffer0.xyz * gbuffer0.w;
	const float oneMinusRoughness = 1.0f - gbuffer2.x;
	const float ndotView = dot(evaluation.normalWS, viewDir);
	const float ndotViewSat = saturate(ndotView);
	const float logOneMinusNdotView = log(1.0f - ndotViewSat);
	const float ndotLight = dot(evaluation.normalWS, lightDir);
	const float ndotLightPositive = max(ndotLight, 0.0f);
	const float ndotLightSat = min(ndotLightPositive, 1.0f);
	const float cb12Rim = 1.0f - saturate(DFLightCB12[30].y);
	const float cb12RimSq = cb12Rim * cb12Rim;
	const float cb12RimPow5 = cb12RimSq * cb12RimSq * cb12Rim;
	const float fresnelTint = 1.0f - cb12RimPow5;
	const float3 normalTransformColor = TransformDFLightFullContractColor(evaluation.normalWS);

	float materialAlpha = gbuffer0.w;
	float directSpecularMask = 0.0f;
	float3 target1Direct = 0.0f;
	float3 target1Indirect = 0.0f;

	const bool tangentMaterial = abs((gbuffer2.w * 255.0f) - 1.0f) < 0.25f;
	if (tangentMaterial) {
		const float tangentDotLight = dot(gbuffer2.xyz, lightDir);
		const float tangentDotView = dot(gbuffer2.xyz, viewDir);
		const float tangentLightSin = sqrt(1.0f - min(tangentDotLight * tangentDotLight, 1.0f));
		const float tangentViewSin = sqrt(1.0f - min(tangentDotView * tangentDotView, 1.0f));

		float lobeSin;
		float lobeCos;
		sincos(DFLightCB12[29].y, lobeSin, lobeCos);
		float lobe = mad(-tangentDotLight, lobeCos, -(tangentLightSin * lobeSin));
		lobe = mad(lobe, tangentDotView, sqrt(1.0f - (lobe * lobe)) * tangentViewSin);
		lobe = max(lobe, 0.0f);
		lobe = DFLightFullContractPow(lobe, DFLightCB12[28].w);
		lobe = saturate(mad(DFLightCB12[28].z, lobe, ndotLightPositive));
		materialAlpha = min(materialAlpha, lobe);

		float highlightSin;
		float highlightCos;
		sincos(DFLightCB12[29].x, highlightSin, highlightCos);
		float highlight = mad(-tangentDotLight, highlightCos, -(tangentLightSin * highlightSin));
		highlight = mad(highlight, tangentDotView, sqrt(1.0f - (highlight * highlight)) * tangentViewSin);
		highlight = max(highlight, 0.0f);
		highlight = DFLightFullContractPow(highlight, DFLightCB12[28].y) * DFLightCB12[28].x;
		target1Direct = ndotLightSat * (highlight * lightColor);
		directSpecularMask = 0.0f;
	} else {
		directSpecularMask = gbuffer2.z * 100.0f;
		const float roughnessExp = exp(mad(gbuffer2.x, 10.0f, 1.0f));
		const float3 reflectionDir = mad(2.0f * ndotView, evaluation.normalWS, -viewDir);
		const float reflectionWeight = exp(logOneMinusNdotView * (3.0f - gbuffer2.x)) * 0.25f;
		target1Indirect = gbuffer2.y * (reflectionWeight * TransformDFLightFullContractColor(reflectionDir));

		const float specExponent = (1.0f - (0.98f * fresnelTint)) * roughnessExp;
		const float3 viewTangent = mad(-evaluation.normalWS, ndotView, viewDir);
		const float3 lightTangent = mad(-evaluation.normalWS, ndotLight, lightDir);
		const float tangentDot = max(dot(viewTangent, lightTangent), 0.0f);
		const float roughnessSq = oneMinusRoughness * oneMinusRoughness;
		const float geometryA = 1.0f - (0.5f * (roughnessSq / (roughnessSq + 0.57f)));
		const float geometryB = (roughnessSq / (roughnessSq + 0.09f)) * 0.45f;
		const float geometrySin = sqrt(saturate((1.0f - (ndotLight * ndotLight)) * (1.0f - (ndotView * ndotView))));
		const float geometryDenom = max(ndotView, ndotLight);
		const float geometry = mad(tangentDot * geometryB, geometrySin / geometryDenom, geometryA);
		materialAlpha = geometry * ndotLightPositive;

		const float3 halfDir = normalize(viewDir + lightDir);
		const float vdotH = saturate(dot(viewDir, halfDir));
		const float ndotH = saturate(dot(halfDir, evaluation.normalWS));
		const float normalization = mad(roughnessExp, specExponent, 2.0f) * 0.159155f;
		const float distribution = DFLightFullContractPow(ndotH, specExponent) * normalization;
		const float minNVNL = min(ndotViewSat, ndotLightSat);
		const bool useSmithDiv = vdotH >= ((ndotH + ndotH) * minNVNL);
		const float smithRatio = (ndotViewSat == minNVNL) ? 1.0f : (ndotLightSat / ndotViewSat);
		const float smithDivA = ((ndotH + ndotH) * smithRatio) / vdotH;
		const float smithDivB = 1.0f / ndotViewSat;
		const float smithDiv = useSmithDiv ? smithDivA : smithDivB;
		const float fresnelBase = 1.0f - vdotH;
		const float fresnelSq = fresnelBase * fresnelBase;
		const float fresnelPow4 = fresnelSq * fresnelSq;
		const float fresnelPow5 = fresnelPow4 * fresnelBase;
		const float fresnel = min(((1.0f - (fresnelBase * fresnelPow4)) * 0.2f) + fresnelPow5, 1.0f);
		float specular = smithDiv * fresnel * distribution * 0.25f;
		specular = min(specular, 15.0f);
		specular *= gbuffer2.y * 3.141593f;
		target1Direct = ndotLightSat * (specular * lightColor);
	}

	const float retroDiffuse =
		oneMinusRoughness *
		exp(logOneMinusNdotView * 0.01f) *
		saturate(dot(viewDir, -lightDir)) *
		ndotLightSat;
	float3 target0 = retroDiffuse * lightColor;
	target0 = mad(lightColor, materialAlpha, target0);
	target0 = mad(lightColor, albedo * saturate(-ndotLight), target0);

	const float diffuseRamp = saturate((directSpecularMask + ndotLight) / (directSpecularMask + 1.0f));
	const float extraDiffuse = max(diffuseRamp - ndotLightSat, 0.0f);
	target0 = mad(extraDiffuse * lightColor, gbuffer0.xyz, target0);

	const float target1Scale = 1.0f - (0.5f * fresnelTint);
	evaluation.output.target1 = float4(mad(target1Direct, target1Scale, target1Indirect), 1.0f);
	evaluation.output.target0 = float4((target0 + normalTransformColor) / 3.0f, 0.0f);

	return evaluation;
}

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightFullContractEvaluation evaluation = EvaluateDFLightFullContractVanilla(input.position);
	DFLightPSOutput output = evaluation.output;

#if FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF
	const float viewZ = max(abs(evaluation.positionWS.z), 0.001f);
	const float3 llfLighting = AccumulateLLFDFLight(
		evaluation.uv,
		viewZ,
		evaluation.positionWS,
		evaluation.normalWS,
		FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS,
		FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS,
		FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS);

	output.target0.rgb += llfLighting / 3.0f;
	output.target1.rgb += llfLighting * 0.25f;
#else
	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeDFLightFullContract(input.position);
		output.target0 += diagnostic.xxxx;
		output.target1 += diagnostic.xxxx;
	}
#endif

	return output;
}
