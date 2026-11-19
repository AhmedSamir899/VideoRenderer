// Small luma copy of a frame for the fade detection.
// Every output pixel is the average of 6x6 taps spread over the cell of the source it covers (a plain point sample
// would let the film grain and the compression noise through), converted to luma. The output is 16-bit (R16_UNORM).

Texture2D tex : register(t0);

cbuffer PS_FADE : register(b0)
{
	float4 cell;   // x0, y0 (first pixel of the picture), cell width, cell height (in source pixels)
	float4 limit;  // last pixel x, last pixel y of the picture (inclusive), unused, unused
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
	const int N = 6;
	const float2 origin = cell.xy + floor(input.Pos.xy) * cell.zw;

	float3 sum = 0;
	[unroll]
	for (int j = 0; j < N; j++) {
		[unroll]
		for (int i = 0; i < N; i++) {
			const float2 p = origin + (float2(i, j) + 0.5) * cell.zw / N;
			const int2 ip = int2(min(floor(p), limit.xy));
			sum += tex.Load(int3(ip, 0)).rgb;
		}
	}

	const float3 avg = sum / (N * N);
	return float4(dot(avg, float3(0.2126, 0.7152, 0.0722)), 0, 0, 1);
}
