#ifndef FO4CS_EXTENDED_MATERIALS_HLSLI
#define FO4CS_EXTENDED_MATERIALS_HLSLI

namespace ExtendedMaterials
{
	static const float ShadowIntensity = 2.0;

	struct DisplacementParams
	{
		float DisplacementScale;
		float DisplacementOffset;
		float HeightScale;
		float FlattenAmount;
	};

	float ScaleDisplacement(float displacement, float heightScale)
	{
		return (displacement - 0.5) * heightScale;
	}

	float ScaleDisplacement(float displacement, DisplacementParams params)
	{
		return ScaleDisplacement(displacement, params.HeightScale);
	}

	float AdjustDisplacementNormalized(float displacement, float scale, float offset)
	{
		return (displacement - 0.5) * scale + 0.5 + offset;
	}

	float AdjustDisplacementNormalized(float displacement, DisplacementParams params)
	{
		return AdjustDisplacementNormalized(displacement, params.DisplacementScale, params.DisplacementOffset);
	}

	float4 AdjustDisplacementNormalized(float4 displacement, float scale, float offset)
	{
		return float4(
			AdjustDisplacementNormalized(displacement.x, scale, offset),
			AdjustDisplacementNormalized(displacement.y, scale, offset),
			AdjustDisplacementNormalized(displacement.z, scale, offset),
			AdjustDisplacementNormalized(displacement.w, scale, offset));
	}

	float4 AdjustDisplacementNormalized(float4 displacement, DisplacementParams params)
	{
		return AdjustDisplacementNormalized(displacement, params.DisplacementScale, params.DisplacementOffset);
	}

	float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
	{
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);

#if !defined(PARALLAX)
		textureDims /= 2.0;
#endif

		float2 texCoordsPerSize = coords * textureDims;
		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);
		float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0.0);

#if !defined(PARALLAX)
		mipLevel += 1.0;
#endif

		return floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);
	}

	float2 GetParallaxOffset(
		Texture2D<float4> heightTex,
		SamplerState heightSamp,
		float2 texCoord,
		float3 viewDirTS,
		float heightScale,
		float screenNoise)
	{
		const float minSamples = 8.0;
		const float maxSamples = 32.0;

		float mipLevel = GetMipLevel(texCoord, heightTex, screenNoise);
		float3 view = normalize(viewDirTS);
		float viewDotNormal = abs(view.z);
		float2 parallaxDir = view.xy / max(viewDotNormal, 0.001);
		float sampleCount = lerp(maxSamples, minSamples, viewDotNormal);
		float layerHeight = 1.0 / sampleCount;
		float currentLayerHeight = 0.0;
		float2 currentTexCoord = texCoord;
		float2 deltaTexCoord = parallaxDir * heightScale * layerHeight;
		float currentHeight = heightTex.SampleLevel(heightSamp, currentTexCoord, mipLevel).r;
		float previousHeight = 1.0;

		[loop]
		for (float i = 0; i < maxSamples; i++) {
			if (currentHeight <= currentLayerHeight) {
				float afterWeight = currentLayerHeight - currentHeight;
				float beforeWeight = (previousHeight - currentLayerHeight) - afterWeight;
				float totalWeight = beforeWeight + afterWeight;
				float blendFactor = abs(totalWeight) > 0.0001 ? afterWeight / totalWeight : 0.5;
				return saturate(lerp(currentTexCoord, currentTexCoord - deltaTexCoord, blendFactor));
			}

			currentLayerHeight += layerHeight;
			previousHeight = currentHeight;
			currentTexCoord -= deltaTexCoord;
			currentHeight = heightTex.SampleLevel(heightSamp, currentTexCoord, mipLevel).r;

			if (currentLayerHeight >= 1.0 || any(currentTexCoord < 0.0) || any(currentTexCoord > 1.0)) {
				break;
			}
		}

		return texCoord;
	}

	float GetParallaxShadow(
		Texture2D<float4> heightTex,
		SamplerState heightSamp,
		float2 texCoord,
		float3 lightDirTS,
		float heightScale,
		float extendShadows,
		float screenNoise)
	{
		float mipLevel = GetMipLevel(texCoord, heightTex, screenNoise);
		float3 light = normalize(lightDirTS);
		float lightDotNormal = abs(light.z);
		float2 shadowDir = light.xy / max(lightDotNormal, 0.001);
		float2 extCoords = texCoord - shadowDir * extendShadows * heightScale;

		const float shadowSamples = 8.0;
		float layerStep = 1.0 / shadowSamples;
		float shadow = 0.0;
		float currentLayer = 0.0;

		[unroll]
		for (float i = 0; i < shadowSamples; i++) {
			currentLayer += layerStep;
			float2 stepCoord = extCoords - shadowDir * currentLayer * heightScale;
			float stepHeight = heightTex.SampleLevel(heightSamp, stepCoord, mipLevel).r;
			float layerDiff = (currentLayer - stepHeight) * ShadowIntensity;
			shadow += saturate(layerDiff) * (1.0 - shadow);
		}

		return 1.0 - shadow * 0.5;
	}

	struct MaterialState
	{
		float3 albedo;
		float3 normalWS;
		float roughness;
		float metallic;
		float3 emissive;
		float materialID;
	};

	MaterialState MakeMaterialState(float3 albedo, float3 normalWS)
	{
		MaterialState state;
		state.albedo = saturate(albedo);
		state.normalWS = normalWS;
		state.roughness = 0.5;
		state.metallic = 0.0;
		state.emissive = float3(0.0, 0.0, 0.0);
		state.materialID = 0.0;
		return state;
	}

	MaterialState ApplyComplexMaterial(MaterialState state, float4 smoothSpecSample)
	{
		const float smoothness = saturate(smoothSpecSample.g);
		state.roughness = saturate(1.0 - smoothness);
		return state;
	}

	MaterialState Apply(
		MaterialState state,
		float2 texCoord,
		float3 worldPos,
		Texture2D<float4> smoothSpecMaskTex,
		SamplerState smoothSpecMaskSamp)
	{
		const float4 smoothSpecSample = smoothSpecMaskTex.Sample(smoothSpecMaskSamp, texCoord);
		return ApplyComplexMaterial(state, smoothSpecSample);
	}
}

#endif
