#include "LightLimitFix/LightLimitFix.hlsli"

cbuffer DFCompositeFrameData : register(b2)
{
	float4 DFCompositeCB2[3];
}

cbuffer DFCompositeAtmosphereData : register(b12)
{
	float4 DFCompositeCB12[47];
}

Texture2D<float4> DFCompositeTexture0 : register(t0);
Texture2D<float4> DFCompositeTexture3 : register(t3);
Texture2D<float4> DFCompositeTexture4 : register(t4);
Texture2D<float4> DFCompositeTexture5 : register(t5);
Texture2D<float4> DFCompositeTexture7 : register(t7);
#if defined(FO4CS_DFCOMPOSITE_VANILLA_10040)
Texture2D<float4> DFCompositeTexture11 : register(t11);
#endif

SamplerState DFCompositeSampler0 : register(s0);
SamplerState DFCompositeSampler3 : register(s3);
SamplerState DFCompositeSampler4 : register(s4);
SamplerState DFCompositeSampler5 : register(s5);
SamplerState DFCompositeSampler7 : register(s7);
#if defined(FO4CS_DFCOMPOSITE_VANILLA_10040)
SamplerState DFCompositeSampler11 : register(s11);
#endif

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

float4 ReconstructDFCompositeWorldPosition(float2 uv, float screenY, float depth)
{
	float depthSlice = 0.0f;
	float4 row0;
	float4 row1;
	float4 row2;
	float4 row3;
	if (0.01f >= depth) {
		depthSlice = depth * 100.0f;
		row0 = DFCompositeCB12[24];
		row1 = DFCompositeCB12[25];
		row2 = DFCompositeCB12[26];
		row3 = DFCompositeCB12[27];
	} else {
		depthSlice = depth * 1.01f - 0.01f;
		row0 = DFCompositeCB12[20];
		row1 = DFCompositeCB12[21];
		row2 = DFCompositeCB12[22];
		row3 = DFCompositeCB12[23];
	}

	const float2 clipXY = float2(uv.x, 1.0f - screenY * DFCompositeCB2[0].y) * 2.0f - 1.0f;
	const float4 clipPosition = float4(clipXY, depthSlice, 1.0f);
	float4 worldPosition;
	worldPosition.x = dot(row0, clipPosition);
	worldPosition.y = dot(row1, clipPosition);
	worldPosition.z = dot(row2, clipPosition);
	worldPosition.w = dot(row3, clipPosition);
	worldPosition.xyz /= worldPosition.w;
	worldPosition.w = 1.0f;
	return worldPosition;
}

float4 ApplyDFCompositeAtmosphere(float3 litColor, float3 overlayColor, float4 worldPosition)
{
	const float fogProjection = dot(DFCompositeCB12[14], worldPosition) + DFCompositeCB12[35].z;
	const float distanceSq = dot(worldPosition.xyz, worldPosition.xyz);
	const float distance = sqrt(distanceSq);
	const float distanceTerm = distance * DFCompositeCB12[41].x - DFCompositeCB12[41].z;
	const float distanceFade = saturate(distanceTerm);

	const float heightFadeA = saturate(fogProjection * DFCompositeCB12[46].x - DFCompositeCB12[46].z);
	const float heightFadeB = saturate(fogProjection * DFCompositeCB12[46].y - DFCompositeCB12[46].w);
	const float heightFade = heightFadeA + distanceFade * (heightFadeB - heightFadeA);

	const bool farFog = 0.75f < distanceTerm;
	const float farRamp = min(((distanceFade - 0.75f) * 4.0f) * (1.0f - DFCompositeCB12[43].w) + DFCompositeCB12[43].w, 1.0f);
	const float fogLimit = farFog ? farRamp : DFCompositeCB12[43].w;

	const bool nearFog = distanceTerm < 0.015f;
	const float nearRamp = nearFog ? distanceFade * 66.666672f : 1.0f;
	const float fogPow = min(fogLimit, exp(log(distanceFade) * DFCompositeCB12[42].w));
	const float alphaScale = heightFade * DFCompositeCB12[44].w + (1.0f - heightFade);

	float3 fogColorA = DFCompositeCB12[42].xyz + fogPow * (DFCompositeCB12[44].xyz - DFCompositeCB12[42].xyz);
	float3 fogColorB = DFCompositeCB12[43].xyz + fogPow * (DFCompositeCB12[45].xyz - DFCompositeCB12[43].xyz);
	float3 fogColor = fogColorA + heightFade * (fogColorB - fogColorA);
	const float fogAlpha = nearRamp * alphaScale * fogPow;

	const float invDistance = rsqrt(distanceSq);
	const float3 viewDirection = worldPosition.xyz * invDistance;
	float directional = max(dot(viewDirection, DFCompositeCB2[1].xyz), 0.0f);
	directional = exp(log(directional) * DFCompositeCB2[2].w) * DFCompositeCB2[1].w;
	fogColor += directional * (DFCompositeCB2[2].xyz - fogColor);

	const float3 compositeColor = litColor * 3.0f + overlayColor;
	const float luminance = dot(compositeColor, float3(0.333333f, 0.333333f, 0.333333f));
	const float3 luminanceFogColor = fogColor + luminance * (luminance.xxx - fogColor);
	return float4(fogAlpha < DFCompositeCB12[43].w ? luminanceFogColor : fogColor, fogAlpha);
}

DFCompositePSOutput main(DFCompositePSInput input)
{
	const float2 uv = GetDFCompositeUV(input.position);
	const float materialClass = DFCompositeTexture3.SampleLevel(DFCompositeSampler3, uv, 0.0f).w;
	const float depth = DFCompositeTexture7.SampleLevel(DFCompositeSampler7, uv, 0.0f).x;
	const float3 overlayColor = DFCompositeTexture4.Sample(DFCompositeSampler4, uv).xyz;

	DFCompositePSOutput output;
	const bool heldMaterial =
		abs(materialClass * 255.0f - 2.0f) < 0.25f ||
		abs(materialClass * 255.0f - 3.0f) < 0.25f;
	if (heldMaterial) {
		output.target0 = 0.0f;
	} else {
		const float3 baseColor = DFCompositeTexture0.SampleLevel(DFCompositeSampler0, uv, 0.0f).xyz;
		float3 directLighting = DFCompositeTexture5.SampleLevel(DFCompositeSampler5, uv, 0.0f).xyz;
#if defined(FO4CS_DFCOMPOSITE_VANILLA_10040)
		directLighting += DFCompositeTexture11.SampleLevel(DFCompositeSampler11, uv, 0.0f).xyz;
#endif
		const float4 worldPosition = ReconstructDFCompositeWorldPosition(uv, input.position.y, depth);
		output.target0 = ApplyDFCompositeAtmosphere(baseColor * directLighting, overlayColor, worldPosition);
	}

	if (input.position.x < -0.5f) {
		output.target0.xyz += ConsumeLLFKeepalive(uv) * 1.0e-4f;
	}
	return output;
}
