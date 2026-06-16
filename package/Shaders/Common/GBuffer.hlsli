#ifndef FO4CS_COMMON_GBUFFER_HLSLI
#define FO4CS_COMMON_GBUFFER_HLSLI

namespace GBuffer
{
	struct SurfaceData
	{
		float3 albedo;
		float3 normalWS;
		float roughness;
		float metallic;
		float3 emissive;
		float materialID;
	};

	struct DeferredLightingOutput
	{
		float4 color : SV_Target0;
		float4 normalRoughness : SV_Target2;
		float4 albedo : SV_Target3;
		float4 emissive : SV_Target4;
		float4 material : SV_Target5;
	};

	float3 SafeNormalize(float3 value, float3 fallback)
	{
		const float lengthSquared = dot(value, value);
		return lengthSquared > 1.0e-8f ? value * rsqrt(lengthSquared) : fallback;
	}

	float4 EncodeNormalRoughness(float3 normalWS, float roughness)
	{
		const float3 encodedNormal = SafeNormalize(normalWS, float3(0.0f, 0.0f, 1.0f)) * 0.5f + 0.5f;
		return float4(saturate(encodedNormal), saturate(roughness));
	}

	float4 EncodeAlbedo(float3 albedo, float metallic)
	{
		return float4(saturate(albedo), saturate(metallic));
	}

	float4 EncodeEmissive(float3 emissive)
	{
		return float4(max(emissive, 0.0f), 0.0f);
	}

	float4 EncodeMaterial(float materialID)
	{
		return float4(materialID, 0.0f, 0.0f, 0.0f);
	}

	SurfaceData MakeDefaultSurface(float3 albedo, float3 normalWS)
	{
		SurfaceData surface;
		surface.albedo = saturate(albedo);
		surface.normalWS = SafeNormalize(normalWS, float3(0.0f, 0.0f, 1.0f));
		surface.roughness = 0.5f;
		surface.metallic = 0.0f;
		surface.emissive = 0.0f;
		surface.materialID = 0.0f;
		return surface;
	}

	DeferredLightingOutput BuildDeferredLightingOutput(float4 color, SurfaceData surface)
	{
		DeferredLightingOutput output;
		output.color = color;
		output.normalRoughness = EncodeNormalRoughness(surface.normalWS, surface.roughness);
		output.albedo = EncodeAlbedo(surface.albedo, surface.metallic);
		output.emissive = EncodeEmissive(surface.emissive);
		output.material = EncodeMaterial(surface.materialID);
		return output;
	}
}

#endif
