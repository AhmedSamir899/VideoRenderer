// Debanding.
// Smooths the small steps (banding) in gradients. Applied after the color conversion and before scaling.
// For every pixel a few neighbours, placed on a rotated cross at a random angle and distance, are averaged.
// Where the pixel and that average differ by less than the threshold, the pixel is moved toward the average.
// A real edge is much larger than the threshold and is left alone. A little noise is added at the end to hide
// what is left and to keep low bit-depth textures from banding again.

Texture2D tex : register(t0);

cbuffer PS_DEBAND : register(b0)
{
	float4 clampRect;   // min x, min y, max x, max y (pixels, inclusive): samples never leave the picture
	float  threshold;   // largest difference that is smoothed, first iteration (0..1)
	float  radius;      // search distance in pixels, first iteration
	float  grain;       // noise amplitude (0..1)
	uint   seed;        // changes every frame
	int    iterations;  // 1..3
	float3 pad;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

uint pcgHash(uint x)
{
	// PCG-style integer hash
	x = x * 747796405u + 2891336453u;
	x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	return (x >> 22u) ^ x;
}

float rand01(inout uint state)
{
	state = pcgHash(state);
	return (state & 0xFFFFFFu) * (1.0 / 16777216.0);
}

float3 loadPx(int2 p)
{
	p = clamp(p, int2(clampRect.xy), int2(clampRect.zw));
	return tex.Load(int3(p, 0)).rgb;
}

float4 main(PS_INPUT input) : SV_Target
{
	int2 p = int2(input.Pos.xy);
	float4 src = tex.Load(int3(p, 0));
	float3 color = src.rgb;

	uint state = pcgHash(uint(p.x) ^ pcgHash(uint(p.y) ^ pcgHash(seed)));

	[unroll]
	for (int i = 1; i <= 3; i++) {
		if (i <= iterations) {
			float dist = rand01(state) * radius * i;
			float ang  = rand01(state) * 6.2831853;
			float2 o   = dist * float2(cos(ang), sin(ang));
			int2 o0 = int2(round(o));
			int2 o1 = int2(round(float2(-o.y, o.x)));

			float3 avg = 0.25 * (loadPx(p + o0) + loadPx(p + o1) + loadPx(p - o0) + loadPx(p - o1));
			float3 diff = abs(color - avg);
			float t = threshold / i;
			float3 w = 1.0 - smoothstep(0.0, t, diff); // 1 where the difference is tiny, 0 at a real edge
			color = lerp(color, avg, w);
		}
	}

	float n0 = rand01(state);
	float n1 = rand01(state);
	float n2 = rand01(state);
	color += (float3(n0, n1, n2) - 0.5) * grain;

	return float4(color, src.a);
}
