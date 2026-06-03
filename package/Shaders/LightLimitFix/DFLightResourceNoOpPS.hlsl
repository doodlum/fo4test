#include "LightLimitFix/LightLimitFix.hlsli"

struct DFLightPSInput
{
	float4 position : SV_Position;
};

struct DFLightPSOutput
{
	float4 target0 : SV_Target0;
	float4 target1 : SV_Target1;
};

float ConsumeLLFResourceContract()
{
	float diagnostic = 0.0f;
	const uint strictCount = LightLimitFix::GetStrictLightCount();
	diagnostic += (float)strictCount * 0.00001f;
	diagnostic += (float)(LightLimitFix::ShadowBitMask & 0xFFFFu) * 0.0000001f;

	if (strictCount > 0) {
		const LightLimitFix::Light strictLight = LightLimitFix::StrictLights[0];
		diagnostic += dot(strictLight.color, float3(0.00001f, 0.00002f, 0.00003f));
	}

	const LightLimitFix::LightGrid grid = LightLimitFix::lightGrid[0];
	const uint listHead = LightLimitFix::lightList[0];
	const LightLimitFix::Light clusteredLight = LightLimitFix::lights[0];
	diagnostic += (float)(grid.offset + grid.lightCount + listHead) * 0.0000001f;
	diagnostic += dot(clusteredLight.color, float3(0.00001f, 0.00002f, 0.00003f));
	return diagnostic;
}

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;
	output.target0 = 0.0f;
	output.target1 = 0.0f;

	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeLLFResourceContract();
		output.target0 = diagnostic.xxxx;
		output.target1 = diagnostic.xxxx;
	}

	return output;
}
