
struct VS_DRAWCOMMON {
	float4 Position : POSITION;
	float2 TexCoord : TEXCOORD;
	float4 Color : COLOUR;
};

struct VS_DRAWCHARACTER {
	float4 Position : POSITION;
	float3 TexCoord : TEXCOORD;
};

struct PS_DRAWTEXTURED {
	float4 Position : SV_POSITION;
	float4 Color : COLOUR;
	float2 TexCoord : TEXCOORD;
};

struct PS_DRAWCHARACTER {
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

struct PS_DRAWCOLOURED {
	float4 Position : SV_POSITION;
	float4 Color : COLOUR;
};


#ifdef VERTEXSHADER
PS_DRAWTEXTURED DrawTexturedVS (VS_DRAWCOMMON vs_in)
{
	PS_DRAWTEXTURED vs_out;

	vs_out.Position = mul (orthoMatrix, vs_in.Position);
	vs_out.Color = vs_in.Color;
	vs_out.TexCoord = vs_in.TexCoord;

	return vs_out;
}

PS_DRAWTEXTURED DrawCinematicVS (VS_DRAWCOMMON vs_in)
{
	PS_DRAWTEXTURED vs_out;

	vs_out.Position = vs_in.Position;
	vs_out.Color = vs_in.Color;
	vs_out.TexCoord = vs_in.Position.xy * vs_in.TexCoord + 0.5f;

	return vs_out;
}

PS_DRAWCHARACTER DrawTexArrayVS (VS_DRAWCHARACTER vs_in)
{
	PS_DRAWCHARACTER vs_out;

	vs_out.Position = mul (orthoMatrix, vs_in.Position);
	vs_out.TexCoord = vs_in.TexCoord;

	return vs_out;
}

PS_DRAWCOLOURED DrawColouredVS (VS_DRAWCOMMON vs_in)
{
	PS_DRAWCOLOURED vs_out;

	vs_out.Position = mul (orthoMatrix, vs_in.Position);
	vs_out.Color = vs_in.Color;

	return vs_out;
}

float4 DrawPolyblendVS (uint vertexId : SV_VertexID) : SV_POSITION
{
	return float4 ((float) (vertexId / 2) * 4.0f - 1.0f, (float) (vertexId % 2) * 4.0f - 1.0f, 0, 1);
}

float4 DrawFadescreenVS (uint vertexId : SV_VertexID) : SV_POSITION
{
	return float4 ((float) (vertexId / 2) * 4.0f - 1.0f, (float) (vertexId % 2) * 4.0f - 1.0f, 0, 1);
}
#endif


#ifdef PIXELSHADER
float4 DrawTexturedPS (PS_DRAWTEXTURED ps_in) : SV_TARGET0
{
	// adjust for pre-multiplied alpha
	float4 diff = GetGamma (mainTexture.Sample (drawSampler, ps_in.TexCoord)) * ps_in.Color;
	return float4 (diff.rgb * diff.a, diff.a);
}

float4 DrawCinematicPS (PS_DRAWTEXTURED ps_in) : SV_TARGET0
{
	return GetGamma (mainTexture.Sample (cineSampler, ps_in.TexCoord));
}

float4 DrawTexArrayPS (PS_DRAWCHARACTER ps_in) : SV_TARGET0
{
	// adjust for pre-multiplied alpha
	float4 diff = GetGamma (charTexture.Sample (drawSampler, ps_in.TexCoord));
	return float4 (diff.rgb * diff.a, diff.a);
}

float4 DrawColouredPS (PS_DRAWCOLOURED ps_in) : SV_TARGET0
{
	// this is a quad filled with a single solid colour so it doesn't need to blend
	return GetGamma (ps_in.Color);
}

float4 DrawPolyblendPS (float4 Position : SV_POSITION) : SV_TARGET0
{
	return GetGamma (vBlend);
}

float4 DrawFadescreenPS (float4 Position : SV_POSITION) : SV_TARGET0
{
	// no gamma needed because rgb is black
	return float4 (0, 0, 0, 0.666f);
}
#endif

