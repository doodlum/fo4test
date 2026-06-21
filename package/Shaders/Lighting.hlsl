// Lighting.hlsl — FO4 Community Shaders lighting pixel shader
// Intended shader-side companion for a verified FO4 lighting path; not an unconditional vanilla PS replacement.
// Architecture mirrors Skyrim CS: compiled per (shaderType, descriptor)
// with feature defines injected at compile time.
// PreNG note: BSShaderHooks holds ReplacePixelShaders on FALLOUT_PRE_NG, so this
// file is not evidence that active FO4 lighting shaders consume LLF b3/t35-t37.

#include "Common/GBuffer.hlsli"

#if defined(EXTENDED_MATERIALS)
#include "ExtendedMaterials/ExtendedMaterials.hlsli"
#endif

#if defined(DEFERRED) && defined(EXTENDED_MATERIALS)
Texture2D<float4> SmoothSpecMaskTexture : register(t2);
SamplerState SmoothSpecMaskSampler : register(s2);
#endif

#if defined(LIGHT_LIMIT_FIX)
#include "LightLimitFix/LightLimitFix.hlsli"

	cbuffer LightLimitFixSupportData : register(b0)
	{
		uint EnableVisualisation;
		uint VisualisationMode;
		float CameraNear;
		float CameraFar;
		uint4 ClusterSize;
	}

	float GetLLFCameraNear()
	{
		return CameraNear > 0.0f ? CameraNear : 0.1f;
	}

	float GetLLFCameraFar()
	{
		const float cameraNear = GetLLFCameraNear();
		return CameraFar > cameraNear ? CameraFar : 10000.0f;
	}

	float3 EvaluateLight(LightLimitFix::Light light, float3 worldPos, float3 N, float3 V)
	{
		float3 toLight = light.positionWS[0].xyz - worldPos;
		float dist = max(length(toLight), 1e-4f);
		float3 L = toLight / dist;

		float intensityFactor = saturate(dist / max(light.radius, 1e-4f));
		float attenuation = (1.0f - intensityFactor * intensityFactor) * light.fade;

		float NdotL = saturate(dot(N, L));
		float3 diffuse = light.color * NdotL * attenuation;

		float3 H = normalize(L + V);
		float spec = pow(saturate(dot(N, H)), 32.0);
		float3 specular = light.color * spec * attenuation * 0.5;

		return diffuse + specular;
	}
#endif

// PS entry point — invoked by FO4's BSShader::SetupGeometry pipeline.
// Input signature matches FO4's default lighting pixel shader layout.
#if defined(DEFERRED)
GBuffer::DeferredLightingOutput main(
#else
float4 main(
#endif
	float4 position  : SV_Position,
	float2 texCoord  : TEXCOORD0,
	float4 worldData : TEXCOORD4,
	float3 tangentWS : TEXCOORD1,
	float3 bitangentWS : TEXCOORD2,
	float3 normalWS  : TEXCOORD3,
	float3 viewDirWS : TEXCOORD5,
	float4 vertexColor : COLOR0,
	bool frontFace : SV_IsFrontFace
#if defined(DEFERRED)
)
#else
) : SV_Target0
#endif
{
	float3 worldPos = worldData.xyz;
	float3 normal = normalize(normalWS);
	float3 totalLight = float3(0.03, 0.03, 0.03); // ambient

#if defined(LIGHT_LIMIT_FIX)
	float3 V = normalize(viewDirWS);
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	LightLimitFix::TryGetCluster(
		position.xy / float2(1920.0, 1080.0),
		position.z,
		ClusterSize,
		GetLLFCameraNear(),
		GetLLFCameraFar(),
		clusterLightOffset,
		clusteredLightCount);

	float3 N = normalize(normal);
	uint totalLightCount = LightLimitFix::GetStrictLightCount() + clusteredLightCount;

	[loop] for (uint i = 0; i < totalLightCount; i++) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!LightLimitFix::GetStrictOrClusteredLight(i, clusterLightOffset, light))
			continue;

		totalLight += EvaluateLight(light, worldPos, N, V);
	}
#endif

	float4 color = float4(totalLight, 1.0);

#if defined(DEFERRED)
	GBuffer::SurfaceData surface = GBuffer::MakeDefaultSurface(saturate(totalLight), normal);
#	if defined(EXTENDED_MATERIALS)
	ExtendedMaterials::MaterialState material = ExtendedMaterials::MakeMaterialState(surface.albedo, surface.normalWS);
	material = ExtendedMaterials::Apply(material, texCoord, worldPos, SmoothSpecMaskTexture, SmoothSpecMaskSampler);
	surface.albedo = material.albedo;
	surface.normalWS = material.normalWS;
	surface.roughness = material.roughness;
	surface.metallic = material.metallic;
	surface.emissive = material.emissive;
	surface.materialID = material.materialID;
#	endif
	return GBuffer::BuildDeferredLightingOutput(color, surface);
#else
	return color;
#endif
}
