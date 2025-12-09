Texture2D<float3> MainPreAlphaInput : register(t0);
Texture2D<float3> MainPostAlphaInput : register(t1);

RWTexture2D<float> ReactiveMaskOutput : register(u0);

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim; // BufferDim.xy * ResolutionScale
	float2 pad0;
	float4 CameraData;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside true sampling dimensions
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

    float3 mainPreAlpha  = MainPreAlphaInput[dispatchID.xy];
    float3 mainPostAlpha = MainPostAlphaInput[dispatchID.xy];

    float3 delta = abs(mainPostAlpha - mainPreAlpha);

   	float reactiveMask = max(delta.x, max(delta.y, delta.z));

	ReactiveMaskOutput[dispatchID.xy] = reactiveMask;
}