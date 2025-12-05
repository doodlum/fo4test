Texture2D<float2> MotionVectorInput : register(t0);
Texture2D<float> DepthInput : register(t1);

RWTexture2D<float2> MotionVectorOutput : register(u0);

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;  // BufferDim.xy * ResolutionScale
	float2 pad0;
	float4 CameraData;
};

float GetScreenDepth(float depth)
{
	return (CameraData.w / (-depth * CameraData.z + CameraData.x));
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside true sampling dimensions
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	float depth = GetScreenDepth(DepthInput[dispatchID.xy]);

	// Find longest motion vector in 5x5 neighborhood
	float2 motionVector = MotionVectorInput[dispatchID.xy];
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll]
	for (int y = -2; y <= 2; y++) {
		[unroll]
		for (int x = -2; x <= 2; x++) {
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Skip samples outside true sampling dimensions
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			float neighborDepth = GetScreenDepth(DepthInput[samplePos]);

			// Take neighbor if it's longer AND closer
			if (neighborDepth < depth){
				float depthDifference = depth - neighborDepth;
				depthDifference = saturate(depthDifference / 4096);

				float2 neighborMotionVector = MotionVectorInput[samplePos] * depthDifference;

				// Square motion vector for length
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq){
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = longestMotionVector;
}