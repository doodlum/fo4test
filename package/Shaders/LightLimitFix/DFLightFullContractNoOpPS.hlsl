// Runtime proof shader for FO4 PreNG full-shadowed DFLight resource contracts.
// Normal visible pixels output zero; the offscreen branch keeps vanilla DFLight
// resources plus LLF b3/t35-t37 declared and consumed by compiled bytecode.

#include "LightLimitFix/DFLightCommon.hlsli"

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;
	output.target0 = 0.0f;
	output.target1 = 0.0f;

	if (input.position.x < -0.5f) {
		const float diagnostic = ConsumeDFLightFullContract(input.position);
		output.target0 = diagnostic.xxxx;
		output.target1 = diagnostic.xxxx;
	}

	return output;
}
