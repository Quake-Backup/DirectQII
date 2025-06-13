

struct VS_MESH {
	float3 PrevPosition : POSITION0;
	float3 PrevNormal : NORMAL0;
	float3 CurrPosition : POSITION1;
	float3 CurrNormal : NORMAL1;
	float2 TexCoord: TEXCOORD;
};

struct PS_MESH {
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD;
	float3 Normal : NORMAL;
};

#ifdef VERTEXSHADER
float4 MeshLerpPosition (VS_MESH vs_in)
{
	return float4 (Move + vs_in.CurrPosition * FrontV + vs_in.PrevPosition * BackV, 1.0f);
}

float3 MeshLerpNormal (VS_MESH vs_in)
{
	// note: this is the correct order for normals; check the light on the hyperblaster v_ model, for example;
	// with the opposite order it flickers pretty badly as the model animates; with this order it's nice and solid
	return normalize (lerp (vs_in.CurrNormal, vs_in.PrevNormal, BackLerp));
}

PS_MESH MeshLightmapVS (VS_MESH vs_in)
{
	PS_MESH vs_out;

	vs_out.Position = mul (LocalMatrix, MeshLerpPosition (vs_in));
	vs_out.TexCoord = vs_in.TexCoord;
	vs_out.Normal = MeshLerpNormal (vs_in);

	return vs_out;
}

float4 MeshPowersuitVS (VS_MESH vs_in) : SV_POSITION
{
	return mul (LocalMatrix, MeshLerpPosition (vs_in) + float4 (MeshLerpNormal (vs_in) * PowersuitScale, 0.0f));
}

PS_DYNAMICLIGHT MeshDynamicVS (VS_MESH vs_in)
{
	return GenericDynamicVS (MeshLerpPosition (vs_in), MeshLerpNormal (vs_in), vs_in.TexCoord);
}
#endif


#ifdef PIXELSHADER
float4 MeshLightmapPS (PS_MESH ps_in) : SV_TARGET0
{
	// read the texture
	float4 diff = mainTexture.Sample (mainSampler, ps_in.TexCoord);

	// apply gamma to diffuse
	diff = GetGamma (diff);

	// perform the lighting using the same equations as qrad.exe/light.exe (assumes shadevector is already normalized)
	float angle = max (dot (normalize (ps_in.Normal), ShadeVector), 0.0f) * 0.5f + 0.5f;

	// perform the light mapping to output
	return float4 (diff.rgb * Desaturate (ShadeLight * angle), diff.a * AlphaVal);
}

float4 MeshFullbrightPS (PS_MESH ps_in) : SV_TARGET0
{
	float4 diff = GetGamma (mainTexture.Sample (mainSampler, ps_in.TexCoord));
	return float4 (diff.rgb, diff.a * AlphaVal);
}

float4 MeshPowersuitPS (float4 Position: SV_POSITION) : SV_TARGET0
{
	return GetGamma (float4 (ShadeLight, AlphaVal));
}
#endif

