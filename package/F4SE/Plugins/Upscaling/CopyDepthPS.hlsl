Texture2D<float> DepthInput : register(t0);
SamplerState DepthSampler : register(s0);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 Texcoord : TEXCOORD0;
};

struct PS_OUTPUT
{
	float Depth : SV_Depth;
};

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT output;
	output.Depth = DepthInput.SampleLevel(DepthSampler, input.Position.xy / ScreenSize, 0);
	return output;
}
