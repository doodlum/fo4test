#ifndef FO4CS_EXTENDED_MATERIALS_HLSLI
#define FO4CS_EXTENDED_MATERIALS_HLSLI

namespace ExtendedMaterials
{
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
		state.roughness = 0.5f;
		state.metallic = 0.0f;
		state.emissive = 0.0f;
		state.materialID = 0.0f;
		return state;
	}

	MaterialState Apply(MaterialState state, float2 texCoord, float3 worldPos)
	{
		return state;
	}
}

#endif
