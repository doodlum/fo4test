// Visible BSLighting LLF consumer pixel shader — vanilla-accurate reconstruction.
//
// The vanilla BSLighting algorithm (descriptor 0x101, core + cubemap) was
// decompiled from the runtime shader dump with 3DMigoto's decompiler
// (D:\Tools\HLSLDecompiler\cmd_Decompiler.exe -D). The directional-light
// cascade shadow, Oren-Nayar-ish diffuse, GGX-style specular, back-light/rim,
// 6-light point-light loop, and cubemap reflection below are the verbatim
// decompiled register code — NOT the old simplified Lambert/Blinn-Phong
// placeholder. On top of that, an additive clustered-light loop consumes the
// LLF cluster SRVs (t35/t36/t37) so the forward path sees unlimited point
// lights instead of the vanilla 6-slot cap.
//
// FO4 forward contract (LLF_CLUSTERS_ONLY): there is no Skyrim-style b3
// strict-light buffer; every clustered light is read from t35/t36/t37.
// Shadowed point lights stay owned by the deferred DFLight pass.
//
// Reference: .codex/docs/bslighting-vanilla-asm-baseline.md,
//            .codex/docs/llf-followup-development-plan.md.

#include "LightLimitFix/LightLimitFix.hlsli"

#ifndef FO4CS_SHADER_DESCRIPTOR
#define FO4CS_SHADER_DESCRIPTOR 0
#endif

// 3DMigoto's generated-code idiom: cmp(a OP b) -> -1.0 / 0.0 float mask used as
// a ternary condition. Preserved verbatim so the decompiled register code below
// stays byte-faithful to the vanilla shader.
#define cmp -

// Descriptor bit decoding observed in vanilla dumps. See
// .codex/docs/bslighting-vanilla-asm-baseline.md.
#define BSL_DESC_BIT_CUBE  0x100  // t4/s4 texturecube
#define BSL_DESC_BIT_AUX15 0x040  // t15/s15 texture2d (specular/glow lookup)
#define BSL_DESC_BIT_AUX6  0x200  // t6/s6 texture2d (replaces t4)

#define BSL_HAS_CUBE  ((FO4CS_SHADER_DESCRIPTOR & BSL_DESC_BIT_CUBE) != 0)
#define BSL_HAS_AUX15 ((FO4CS_SHADER_DESCRIPTOR & BSL_DESC_BIT_AUX15) != 0)
#define BSL_HAS_AUX6  ((FO4CS_SHADER_DESCRIPTOR & BSL_DESC_BIT_AUX6) != 0)

Texture2DArray<float4> t14 : register(t14);
#if BSL_HAS_CUBE
TextureCube<float4> t4 : register(t4);
#endif
#if BSL_HAS_AUX6
Texture2D<float4> t6 : register(t6);
#endif
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);
#if BSL_HAS_AUX15
Texture2D<float4> t15 : register(t15);
#endif

SamplerState s14_s : register(s14);
#if BSL_HAS_CUBE
SamplerState s4_s : register(s4);
#endif
#if BSL_HAS_AUX6
SamplerState s6_s : register(s6);
#endif
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);
#if BSL_HAS_AUX15
SamplerState s15_s : register(s15);
#endif

cbuffer cb2 : register(b2)
{
	float4 cb2[38];
}

cbuffer cb1 : register(b1)
{
	float4 cb1[8];
}

void main(
	float4 v0 : SV_POSITION0,
	float4 v1 : TEXCOORD0,
	float4 v2 : TEXCOORD4,
	float4 v3 : TEXCOORD1,
	float4 v4 : TEXCOORD2,
	float4 v5 : TEXCOORD3,
	float3 v6 : TEXCOORD5,
	float4 v7 : COLOR0,
	uint v8 : SV_IsFrontFace0,
	out float4 o0 : SV_Target0)
{
	float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14;
	uint4 bitmask, uiDest;
	float4 fDest;

	// ---- surface decode (verbatim vanilla) ----
	r0.x = dot(v6.xyz, v6.xyz);
	r0.x = rsqrt(r0.x);
	r0.yzw = v6.xyz * r0.xxx;
	r1.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
	r2.xy = t1.Sample(s1_s, v1.xy).xy;
	r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
	r2.w = dot(r2.xy, r2.xy);
	r2.w = min(1, r2.w);
	r2.w = 1 + -r2.w;
	r2.z = sqrt(r2.w);
	r3.xy = t2.Sample(s2_s, v1.xy).xy;
	r3.zw = cb2[11].xy * r3.yx;
	r2.w = -r3.y * cb2[11].x + 1;
	r2.w = r2.w * 2 + -1;
	r2.w = 8.65590954 * r2.w;
	r2.w = exp2(r2.w);
	r2.w = 1 + r2.w;
	r2.w = 3 / r2.w;
	r2.w = 3 + -r2.w;
	r3.w = 3.14159274 * r3.w;
	r3.z = r3.z * 10 + 1;
	r3.z = exp2(r3.z);
	r4.x = dot(v3.xyz, r2.xyz);
	r4.y = dot(v4.xyz, r2.xyz);
	r4.z = dot(v5.xyz, r2.xyz);
	r2.x = dot(r4.xyz, r4.xyz);
	r2.x = rsqrt(r2.x);
	r4.xyz = r4.xyz * r2.xxx;
	r4.w = v8.x ? r4.y : -r4.y;

	// ---- directional + cascade shadow (verbatim vanilla) ----
	r2.x = cmp(cb2[10].x == 1.000000);
	if (r2.x != 0) {
		r2.x = cmp(v2.w < cb2[37].w);
		r5.xyz = cmp(cb2[37].zyx < v2.www);
		r6.xyzw = r5.zzzz ? cb2[28].xyzw : cb2[25].xyzw;
		r7.xyzw = r5.zzzz ? cb2[29].xyzw : cb2[26].xyzw;
		r2.y = r5.z ? 1.000000 : 0;
		r6.xyzw = r5.yyyy ? cb2[31].xyzw : r6.xyzw;
		r7.xyzw = r5.yyyy ? cb2[32].xyzw : r7.xyzw;
		r2.y = r5.y ? 2 : r2.y;
		r6.xyzw = r5.xxxx ? cb2[34].xyzw : r6.xyzw;
		r7.xyzw = r5.xxxx ? cb2[35].xyzw : r7.xyzw;
		r8.z = r5.x ? 3 : r2.y;
		r9.xyz = v2.xyz;
		r9.w = 1;
		r8.x = dot(r6.xyzw, r9.xyzw);
		r8.y = dot(r7.xyzw, r9.xyzw);
		r2.y = t14.Sample(s14_s, r8.xyz).x;
		if (r2.x != 0) {
			r6.xyzw = r5.zzzz ? cb2[30].xyzw : cb2[27].xyzw;
			r6.xyzw = r5.yyyy ? cb2[33].xyzw : r6.xyzw;
			r5.xyzw = r5.xxxx ? cb2[36].xyzw : r6.xyzw;
			r2.x = dot(r5.xyzw, r9.xyzw);
			r2.x = cmp(r2.y >= r2.x);
			r2.x = r2.x ? 1.000000 : 0;
		} else {
			r2.x = 1;
		}
	} else {
		r2.x = 1;
	}

	// ---- directional BRDF (verbatim vanilla) ----
	r2.y = 1 + -cb2[11].x;
	r2.z = dot(r0.ywz, r4.xzw);
	r4.y = dot(cb2[0].xzy, r4.xzw);
	r5.xyz = -r4.xwz * r2.zzz + r0.yzw;
	r6.xyz = -r4.xwz * r4.yyy + cb2[0].xyz;
	r5.w = dot(r5.xyz, r6.xyz);
	r6.x = r2.y * r2.y;
	r6.yz = r2.yy * r2.yy + float2(0.569999993,0.0900000036);
	r6.xy = r6.xx / r6.yz;
	r2.y = 0.449999988 * r6.y;
	r6.x = -r6.x * 0.5 + 1;
	r6.y = -r2.z * r2.z + 1;
	r6.z = -r4.y * r4.y + 1;
	r6.z = saturate(r6.y * r6.z);
	r6.z = sqrt(r6.z);
	r6.w = max(r4.y, r2.z);
	r6.z = r6.z / r6.w;
	r6.w = max(0, r4.y);
	r5.w = max(0, r5.w);
	r5.w = r5.w * r2.y;
	r5.w = r5.w * r6.z + r6.x;
	r5.w = r6.w * r5.w;
	r7.xyz = cb2[1].xyz * r5.www;
	r5.w = min(1, r6.w);
	r6.z = saturate(r2.z);
	r6.w = 1 + -r6.z;
	r6.w = log2(r6.w);
	r6.w = 0.00999999978 * r6.w;
	r6.w = exp2(r6.w);
	r7.w = saturate(dot(r0.yzw, -cb2[0].xyz));
	r7.w = r7.w * r6.w;
	r7.w = r7.w * r5.w;
	r8.xyz = cb2[1].xyz * r7.www;
	r8.xyz = r8.xyz * r2.www;
	r7.xyz = r7.xyz * r2.xxx + r8.xyz;
	r8.xyz = cb1[7].yyy * r1.xyz;
	r7.w = saturate(-r4.y);
	r8.xyz = r7.www * r8.xyz;
	r7.xyz = cb2[1].xyz * r8.xyz + r7.xyz;
	r4.y = cb1[7].x + r4.y;
	r7.w = 1 + cb1[7].x;
	r4.y = saturate(r4.y / r7.w);
	r4.y = r4.y + -r5.w;
	r4.y = max(0, r4.y);
	r8.xyz = cb2[1].xyz * r4.yyy;
	r7.xyz = r8.xyz * r1.xyz + r7.xyz;
	r7.xyz = r7.xyz * r2.xxx;
	r8.xyz = v6.xyz * r0.xxx + cb2[0].xyz;
	r4.y = dot(r8.xyz, r8.xyz);
	r4.y = rsqrt(r4.y);
	r8.xyz = r8.xyz * r4.yyy;
	r4.y = saturate(dot(r0.yzw, r8.xyz));
	r7.w = saturate(dot(r8.xzy, r4.xzw));
	r8.x = 2 + r3.z;
	r8.x = 0.159154937 * r8.x;
	r8.y = log2(r7.w);
	r8.y = r8.y * r3.z;
	r8.y = exp2(r8.y);
	r8.z = max(1.1920929e-07, r4.y);
	r8.w = min(r6.z, r5.w);
	r7.w = r7.w + r7.w;
	r9.x = r8.w * r7.w;
	r9.x = cmp(r8.z >= r9.x);
	r8.w = cmp(r6.z == r8.w);
	r9.y = r5.w / r6.z;
	r8.w = r8.w ? 1 : r9.y;
	r7.w = r8.w * r7.w;
	r7.w = r7.w / r8.z;
	r8.z = 1 / r6.z;
	r7.w = r9.x ? r7.w : r8.z;
	r4.y = 1 + -r4.y;
	r8.w = r4.y * r4.y;
	r8.yw = r8.xw * r8.yw;
	r9.x = r8.w * r4.y;
	r4.y = -r4.y * r8.w + 1;
	r4.y = r4.y * 0.200000003 + r9.x;
	r4.y = min(1, r4.y);
	r4.y = r4.y * r7.w;
	r4.y = r4.y * r8.y;
	r4.y = 0.25 * r4.y;
	r4.y = min(15, r4.y);
	r4.y = r4.y * r3.w;
	r9.xyz = cb2[1].xyz * r4.yyy;
	r9.xyz = r9.xyz * r5.www;
	r9.xyz = r9.xyz * r2.xxx;

	// ---- vanilla 6-light point-light loop (verbatim vanilla) ----
	r2.x = frac(cb2[19].w);
	r2.x = cb2[19].w + -r2.x;
	r2.x = min(20, r2.x);
	r2.x = max(0, r2.x);
	r4.y = cmp(0 < r2.x);
	if (r4.y != 0) {
		r10.xyz = r7.xyz;
		r11.xyz = r9.xyz;
		r4.y = 0;
		while (true) {
			r5.w = (int)r4.y;
			r5.w = cmp(r5.w >= r2.x);
			if (r5.w != 0) break;
			r12.xyz = cb2[r4.y+13].xyz + -v2.xyz;
			r5.w = dot(r12.xyz, r12.xyz);
			r7.w = sqrt(r5.w);
			r7.w = saturate(r7.w / cb2[r4.y+13].w);
			r7.w = -r7.w * r7.w + 1;
			r7.w = log2(r7.w);
			r7.w = 2.20000005 * r7.w;
			r7.w = exp2(r7.w);
			r5.w = rsqrt(r5.w);
			r12.xyz = r12.xyz * r5.www;
			r5.w = dot(r12.xzy, r4.xzw);
			r13.xyz = -r4.xwz * r5.www + r12.xyz;
			r8.y = dot(r5.xyz, r13.xyz);
			r8.w = -r5.w * r5.w + 1;
			r8.w = saturate(r8.w * r6.y);
			r8.w = sqrt(r8.w);
			r9.w = max(r5.w, r2.z);
			r8.w = r8.w / r9.w;
			r5.w = max(0, r5.w);
			r8.y = max(0, r8.y);
			r8.y = r8.y * r2.y;
			r8.y = r8.y * r8.w + r6.x;
			r8.y = r8.y * r5.w;
			r13.xyz = cb2[r4.y+19].xyz * r8.yyy;
			r8.y = r7.w * r7.w;
			r13.xyz = r8.yyy * r13.xyz + r10.xyz;
			r5.w = min(1, r5.w);
			r8.y = saturate(dot(r0.yzw, -r12.xyz));
			r8.y = r8.y * r6.w;
			r8.y = r8.y * r5.w;
			r14.xyz = cb2[r4.y+19].xyz * r8.yyy;
			r14.xyz = r14.xyz * r2.www;
			r10.xyz = r14.xyz * r7.www + r13.xyz;
			r12.xyz = v6.xyz * r0.xxx + r12.xyz;
			r8.y = dot(r12.xyz, r12.xyz);
			r8.y = rsqrt(r8.y);
			r12.xyz = r12.xyz * r8.yyy;
			r8.y = saturate(dot(r0.yzw, r12.xyz));
			r8.w = saturate(dot(r12.xzy, r4.xzw));
			r9.w = log2(r8.w);
			r9.w = r9.w * r3.z;
			r9.w = exp2(r9.w);
			r9.w = r9.w * r8.x;
			r10.w = max(1.1920929e-07, r8.y);
			r11.w = min(r6.z, r5.w);
			r8.w = r8.w + r8.w;
			r12.x = r11.w * r8.w;
			r12.x = cmp(r10.w >= r12.x);
			r11.w = cmp(r6.z == r11.w);
			r12.y = r5.w / r6.z;
			r11.w = r11.w ? 1 : r12.y;
			r8.w = r11.w * r8.w;
			r8.w = r8.w / r10.w;
			r8.w = r12.x ? r8.w : r8.z;
			r8.y = 1 + -r8.y;
			r10.w = r8.y * r8.y;
			r10.w = r10.w * r10.w;
			r11.w = r10.w * r8.y;
			r8.y = -r8.y * r10.w + 1;
			r8.y = r8.y * 0.200000003 + r11.w;
			r8.y = min(1, r8.y);
			r8.y = r8.y * r8.w;
			r8.y = r8.y * r9.w;
			r8.y = 0.25 * r8.y;
			r8.y = min(15, r8.y);
			r8.y = r8.y * r3.w;
			r12.xyz = cb2[r4.y+19].xyz * r8.yyy;
			r12.xyz = r12.xyz * r5.www;
			r11.xyz = r12.xyz * r7.www;
			r4.y = (int)r4.y + 1;
		}
		r7.xyz = r10.xyz;
		r9.xyz = r11.xyz;
	}

	// ---- LLF clustered lights (additive; FO4 forward, no b3) ----
	{
		uint clusterOffset;
		uint clusterCount;
		const uint3 clusterSize = uint3(8, 8, 16);
		const float2 clusterUV = saturate(v0.xy * cb1[0].zw);
		const float viewZ = max(v2.w, 0.001f);
		LightLimitFix::TryGetCluster(
			clusterUV,
			viewZ,
			clusterSize,
			max(cb1[0].x, 0.001f),
			max(cb1[0].y, 1.0f),
			clusterOffset,
			clusterCount);

		r10.xyz = r7.xyz;
		r11.xyz = r9.xyz;
		uint li = 0;
		while (true) {
			if (li >= clusterCount) break;
			LightLimitFix::Light llfLight = (LightLimitFix::Light)0;
			if (!LightLimitFix::GetClusteredLight(li, clusterOffset, llfLight)) {
				li = li + 1;
				continue;
			}
			float3 lightColor = llfLight.color * saturate(llfLight.fade);

			r12.xyz = llfLight.positionWS[0].xyz + -v2.xyz;
			r5.w = dot(r12.xyz, r12.xyz);
			r7.w = sqrt(r5.w);
			r7.w = saturate(r7.w * max(llfLight.invRadius, 0.0f));
			r7.w = -r7.w * r7.w + 1;
			r7.w = log2(r7.w);
			r7.w = 2.20000005 * r7.w;
			r7.w = exp2(r7.w);
			r5.w = rsqrt(r5.w);
			r12.xyz = r12.xyz * r5.www;
			r5.w = dot(r12.xzy, r4.xzw);
			r13.xyz = -r4.xwz * r5.www + r12.xyz;
			r8.y = dot(r5.xyz, r13.xyz);
			r8.w = -r5.w * r5.w + 1;
			r8.w = saturate(r8.w * r6.y);
			r8.w = sqrt(r8.w);
			r9.w = max(r5.w, r2.z);
			r8.w = r8.w / r9.w;
			r5.w = max(0, r5.w);
			r8.y = max(0, r8.y);
			r8.y = r8.y * r2.y;
			r8.y = r8.y * r8.w + r6.x;
			r8.y = r8.y * r5.w;
			r13.xyz = lightColor * r8.yyy;
			r8.y = r7.w * r7.w;
			r13.xyz = r8.yyy * r13.xyz + r10.xyz;
			r5.w = min(1, r5.w);
			r8.y = saturate(dot(r0.yzw, -r12.xyz));
			r8.y = r8.y * r6.w;
			r8.y = r8.y * r5.w;
			r14.xyz = lightColor * r8.yyy;
			r14.xyz = r14.xyz * r2.www;
			r10.xyz = r14.xyz * r7.www + r13.xyz;
			r12.xyz = v6.xyz * r0.xxx + r12.xyz;
			r8.y = dot(r12.xyz, r12.xyz);
			r8.y = rsqrt(r8.y);
			r12.xyz = r12.xyz * r8.yyy;
			r8.y = saturate(dot(r0.yzw, r12.xyz));
			r8.w = saturate(dot(r12.xzy, r4.xzw));
			r9.w = log2(r8.w);
			r9.w = r9.w * r3.z;
			r9.w = exp2(r9.w);
			r9.w = r9.w * r8.x;
			r10.w = max(1.1920929e-07, r8.y);
			r11.w = min(r6.z, r5.w);
			r8.w = r8.w + r8.w;
			r12.x = r11.w * r8.w;
			r12.x = cmp(r10.w >= r12.x);
			r11.w = cmp(r6.z == r11.w);
			r12.y = r5.w / r6.z;
			r11.w = r11.w ? 1 : r12.y;
			r8.w = r11.w * r8.w;
			r8.w = r8.w / r10.w;
			r8.w = r12.x ? r8.w : r8.z;
			r8.y = 1 + -r8.y;
			r10.w = r8.y * r8.y;
			r10.w = r10.w * r10.w;
			r11.w = r10.w * r8.y;
			r8.y = -r8.y * r10.w + 1;
			r8.y = r8.y * 0.200000003 + r11.w;
			r8.y = min(1, r8.y);
			r8.y = r8.y * r8.w;
			r8.y = r8.y * r9.w;
			r8.y = 0.25 * r8.y;
			r8.y = min(15, r8.y);
			r8.y = r8.y * r3.w;
			r12.xyz = lightColor * r8.yyy;
			r12.xyz = r12.xyz * r5.www;
			r11.xyz = r12.xyz * r7.www;
			li = li + 1;
		}
		r7.xyz = r10.xyz;
		r9.xyz = r11.xyz;
	}

	// ---- cubemap reflection (verbatim vanilla 0x101) + composite ----
#if BSL_HAS_CUBE
	r0.x = 1 + -r3.y;
	r0.x = 6 * r0.x;
	r0.x = v0.z * 0.001953125 + r0.x;
	r2.x = 3 * r3.x;
	r2.y = saturate(-0.300000012 + r3.y);
	r2.y = rsqrt(r2.y);
	r2.y = 1 / r2.y;
	r2.y = min(1, r2.y);
	r2.x = r2.x * r2.y;
	r2.x = cb2[11].y * r2.x;
	r2.y = r2.z + r2.z;
	r0.yzw = r2.yyy * r4.xwz + -r0.yzw;
	r0.yzw = -r0.yzw;
	r0.xyz = t4.SampleLevel(s4_s, r0.yzw, r0.x).xyz;
	r0.xyz = r0.xyz * r2.xxx;
	r0.xyz = cb1[2].xxx * r0.xyz;
	r2.xyz = cb2[3].yzw + r7.xyz;
	r1.xyz = r2.xyz * r1.xyz;
	r1.xyz = r1.xyz * v7.xyz + r9.xyz;
	o0.xyz = r0.xyz * r2.xyz + r1.xyz;
#else
	r2.xyz = cb2[3].yzw + r7.xyz;
	r1.xyz = r2.xyz * r1.xyz;
	o0.xyz = r1.xyz * v7.xyz + r9.xyz;
#endif
	r0.x = r1.w * v7.w + -cb2[3].x;
	r0.x = cmp(r0.x < 0);
	if (r0.x != 0) discard;
	o0.w = cb2[2].z;
	return;
}
