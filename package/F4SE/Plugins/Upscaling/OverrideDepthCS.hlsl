Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);
RWTexture2D<float> LinearDepthOutput : register(u1);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
};

float GetScreenDepth(float depth)
{
	return (CameraData.w / (-depth * CameraData.z + CameraData.x));
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside texture dimensions
	if (any(dispatchID.xy >= ScreenSize))
		return;

	float depth = DepthInput[dispatchID.xy * RenderSize / ScreenSize];

	DepthOutput[dispatchID.xy] = depth;
	LinearDepthOutput[dispatchID.xy] = GetScreenDepth(depth);
}