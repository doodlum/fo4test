struct DFLightPSInput
{
	float4 position : SV_Position;
};

struct DFLightPSOutput
{
	float4 target0 : SV_Target0;
	float4 target1 : SV_Target1;
};

DFLightPSOutput main(DFLightPSInput input)
{
	DFLightPSOutput output;
	output.target0 = 0.0f;
	output.target1 = 0.0f;
	return output;
}
