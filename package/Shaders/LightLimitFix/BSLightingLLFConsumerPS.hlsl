// Visible BSLighting LLF consumer pixel shader.
//
// Replaces the vanilla FO4 BSLighting PS for descriptors observed by the LLF
// descriptor detour. Preserves vanilla resource shape (CB1[8], CB2[38],
// t0/t1/t2/t14, optional t4 cubemap / t6 / t15, samplers s0/s1/s2/s14,
// optional s4/s6/s15, single render target o0, alpha-test discard) and adds
// the LLF strict + clustered lighting consumer at b3/t35/t36/t37 from
// LightLimitFix.hlsli.
//
// Algorithm fidelity is intentionally simplified vs vanilla (Lambert diffuse +
// Blinn-Phong specular) — the goal is to validate that LLF clustered lights
// reach BSLighting pixels without breaking the frame, before iterating toward
// vanilla parity. See `.codex/docs/bslighting-vanilla-asm-baseline.md` for the
// vanilla algorithm reconstruction.

#include "LightLimitFix/LightLimitFix.hlsli"

#ifndef FO4CS_SHADER_DESCRIPTOR
#define FO4CS_SHADER_DESCRIPTOR 0
#endif

// Descriptor bit decoding observed in vanilla dumps. See
// `.codex/docs/bslighting-vanilla-asm-baseline.md`.
#define BSL_DESC_BIT_CUBE  0x100  // t4/s4 texturecube
#define BSL_DESC_BIT_AUX15 0x040  // t15/s15 texture2d (e.g. specular lookup)
#define BSL_DESC_BIT_AUX6  0x200  // t6/s6 texture2d (replaces t4)

#define BSL_HAS_CUBE  ((FO4CS_SHADER_DESCRIPTOR & BSL_DESC_BIT_CUBE) != 0)
#define BSL_HAS_AUX15 ((FO4CS_SHADER_DESCRIPTOR & BSL_DESC_BIT_AUX15) != 0)
#define BSL_HAS_AUX6  ((FO4CS_SHADER_DESCRIPTOR & BSL_DESC_BIT_AUX6) != 0)

cbuffer BSLightingRuntimeData : register(b1)
{
	float4 BSLightingCB1[8];
}

cbuffer BSLightingLightData : register(b2)
{
	float4 BSLightingCB2[38];
}

Texture2D<float4>      BSLightingAlbedo   : register(t0);
Texture2D<float4>      BSLightingNormal   : register(t1);
Texture2D<float4>      BSLightingSpecular : register(t2);
#if BSL_HAS_CUBE
TextureCube<float4>    BSLightingCubemap  : register(t4);
#endif
#if BSL_HAS_AUX6
Texture2D<float4>      BSLightingAux6     : register(t6);
#endif
Texture2DArray<float4> BSLightingShadow   : register(t14);
#if BSL_HAS_AUX15
Texture2D<float4>      BSLightingAux15    : register(t15);
#endif

SamplerState BSLightingSamplerAlbedo   : register(s0);
SamplerState BSLightingSamplerNormal   : register(s1);
SamplerState BSLightingSamplerSpecular : register(s2);
#if BSL_HAS_CUBE
SamplerState BSLightingSamplerCubemap  : register(s4);
#endif
#if BSL_HAS_AUX6
SamplerState BSLightingSamplerAux6     : register(s6);
#endif
SamplerState BSLightingSamplerShadow   : register(s14);
#if BSL_HAS_AUX15
SamplerState BSLightingSamplerAux15    : register(s15);
#endif

struct PSInput
{
	float4 position    : SV_Position;
	float2 texCoord0   : TEXCOORD0;
	float4 worldPos    : TEXCOORD4;  // vanilla v2: xyz=worldPos, w=viewDepth
	float3 tangentX    : TEXCOORD1;
	float3 tangentY    : TEXCOORD2;
	float3 tangentZ    : TEXCOORD3;
	float3 viewVector  : TEXCOORD5;  // vanilla v6: world-space view ray
	float4 vertexColor : COLOR0;
	bool   isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
	float4 target0 : SV_Target0;
};

// Reconstruct world-space normal from tangent-space normalmap, matching
// vanilla's mad-by-2-minus-1 + sqrt-z + TBN multiply.
float3 DecodeWorldNormal(float2 nrmXY, float3 tx, float3 ty, float3 tz, bool frontFace)
{
	float2 xy = mad(nrmXY, 2.0f, -1.0f);
	float zSq = saturate(1.0f - dot(xy, xy));
	float3 tangentN = float3(xy, sqrt(zSq));

	float3 worldN;
	worldN.x = dot(tx, tangentN);
	worldN.y = dot(ty, tangentN);
	worldN.z = dot(tz, tangentN);
	worldN = normalize(worldN);

	// Vanilla flips Y on backface (movc r4.w, v8.x, r4.y, -r4.y).
	if (!frontFace) {
		worldN.y = -worldN.y;
	}
	return worldN;
}

// Simplified Blinn-Phong + Lambert per light. Adds diffuse and specular into
// the running accumulators. Matches the structure of vanilla's per-light
// loop body but uses a stable analytic form that fxc can verify offline.
void AccumulateLight(
	in float3 lightColor,
	in float3 lightVecWS,   // unnormalized: lightPosWS - surfaceWS
	in float  invRadius,
	in float3 worldNormal,
	in float3 viewDir,      // surface -> camera
	in float  glossExp,
	in float  specStrength,
	inout float3 diffuseAccum,
	inout float3 specularAccum)
{
	float dist = length(lightVecWS);
	float3 L = lightVecWS / max(dist, 1.0e-4f);

	// Vanilla-style attenuation: pow(saturate(1 - (d * invRadius)^2), 2.2).
	float t = saturate(dist * max(invRadius, 0.0f));
	float falloff = saturate(1.0f - t * t);
	float atten = pow(falloff, 2.2f);

	float NdotL = saturate(dot(worldNormal, L));
	diffuseAccum += lightColor * (NdotL * atten);

	float3 H = normalize(L + viewDir);
	float NdotH = saturate(dot(worldNormal, H));
	float spec = pow(NdotH, max(glossExp, 1.0f)) * specStrength;
	specularAccum += lightColor * (spec * atten);
}

PSOutput main(PSInput input)
{
	PSOutput output;

	// --- 1. surface decode (vanilla shape preserved) ---
	float2 uv = input.texCoord0;
	float4 albedoSample = BSLightingAlbedo.Sample(BSLightingSamplerAlbedo, uv);
	float2 normalXY     = BSLightingNormal.Sample(BSLightingSamplerNormal, uv).xy;
	float2 specSample   = BSLightingSpecular.Sample(BSLightingSamplerSpecular, uv).xy;

	// Vanilla alpha test: discard if albedo.a * vertexColor.a < cb2[3].x.
	float alphaThreshold = BSLightingCB2[3].x;
	clip(albedoSample.a * input.vertexColor.w - alphaThreshold);

	float3 worldNormal = DecodeWorldNormal(
		normalXY,
		input.tangentX,
		input.tangentY,
		input.tangentZ,
		input.isFrontFace);

	// View direction (surface -> camera) reconstructed from interpolated view
	// vector, matching vanilla's r0.yzw = normalize(v6).
	float3 viewDir = normalize(-input.viewVector);

	// Vanilla maps t2.y -> glossiness exponent via cb2[11].x scale; we keep a
	// monotonic approximation that depends on the same texture channel so the
	// material variation is preserved.
	float gloss = saturate(specSample.y * max(BSLightingCB2[11].x, 0.0f));
	float glossExp = lerp(2.0f, 64.0f, gloss);
	float specStrength = saturate(specSample.x * max(BSLightingCB2[11].y, 0.0f));

	// --- 2. directional light (vanilla CB2[0] dir, CB2[1] color) ---
	float3 sunDir   = normalize(-BSLightingCB2[0].xyz);
	float3 sunColor = BSLightingCB2[1].xyz;

	float3 diffuse  = (float3)0.0f;
	float3 specular = (float3)0.0f;

	float NdotSun = saturate(dot(worldNormal, sunDir));
	diffuse  += sunColor * NdotSun;

	float3 sunH = normalize(sunDir + viewDir);
	float sunSpec = pow(saturate(dot(worldNormal, sunH)), max(glossExp, 1.0f)) * specStrength;
	specular += sunColor * sunSpec;

	// Ambient floor approximating vanilla's environment + back-light terms so
	// totally unlit pixels do not go pitch black before LLF kicks in.
	float3 ambient = max(BSLightingCB1[7].xyz, 0.0f) * 0.35f;
	diffuse += ambient;

	// --- 3. LLF strict + clustered consumer ---
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	// Must match LightLimitFix::clusterSize[3] = {8, 8, 16} in C++ until the
	// runtime starts emitting dynamic cluster size to the shader.
	const uint3 clusterSize = uint3(8, 8, 16);
	const float2 clusterUV = saturate(input.position.xy * BSLightingCB1[0].zw);
	const float viewZ = max(input.worldPos.w, 0.001f);

	LightLimitFix::TryGetCluster(
		clusterUV,
		viewZ,
		clusterSize,
		max(BSLightingCB1[0].x, 0.001f),
		max(BSLightingCB1[0].y, 1.0f),
		clusterLightOffset,
		clusteredLightCount);

	const uint strictCount = LightLimitFix::GetStrictLightCount();
	const uint totalLLF = strictCount + clusteredLightCount;

	[loop]
	for (uint li = 0; li < totalLLF; ++li) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!LightLimitFix::GetStrictOrClusteredLight(li, clusterLightOffset, light)) {
			continue;
		}

		float3 lightPos = light.positionWS[0].xyz;
		float3 lightVec = lightPos - input.worldPos.xyz;
		float3 lightCol = max(light.color, 0.0f) * saturate(light.fade);

		AccumulateLight(
			lightCol,
			lightVec,
			max(light.invRadius, 0.0f),
			worldNormal,
			viewDir,
			glossExp,
			specStrength,
			diffuse,
			specular);
	}

	// --- 4. composite (single RT, vanilla-shaped) ---
	float3 lit = albedoSample.rgb * diffuse + specular;
	lit *= input.vertexColor.rgb;
	lit += BSLightingCB2[3].yzw * albedoSample.rgb;  // emissive/tint term

	output.target0.rgb = lit;
	output.target0.a   = BSLightingCB2[2].z;
	return output;
}
