//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// A constant buffer that stores the model transform.
cbuffer ModelConstantBuffer : register(b0)
{
	float4x4 model;
	float4x4 normal;
	
};



// A constant buffer that stores each set of view and projection matrices in column-major format.
cbuffer ViewProjectionConstantBuffer : register(b1)
{
	float4x4 viewProjection[2];
};

// Per-vertex data used as input to the vertex shader.
struct VertexShaderInput
{
	float4 pos     : POSITION;
	float4 color   : COLOR0;
	uint        instId  : SV_InstanceID;
};

// Per-vertex data passed to the geometry shader.
// Note that the render target array index will be set by the geometry shader
// using the value of viewId.
struct VertexShaderOutput
{
	float4 pos     : SV_POSITION;
	float4 color   : COLOR0;
	uint        viewId  : TEXCOORD0;  // SV_InstanceID % 2
};

// Simple shader to do vertex processing on the GPU.
VertexShaderOutput main(VertexShaderInput input)
{
	
	VertexShaderOutput output;
	float4 pos = input.pos;

	// Note which view this vertex has been sent to. Used for matrix lookup.
	// Taking the modulo of the instance ID allows geometry instancing to be used
	// along with stereo instanced drawing; in that case, two copies of each 
	// instance would be drawn, one for left and one for right.
	int idx = input.instId % 2;

	// Transform the vertex position into world space.
	pos = mul(pos, model);

	pos = mul(pos, normal);

	// Correct for perspective and project the vertex position onto the screen.
	pos = mul(pos, viewProjection[idx]);
	//pos = mul(input.pos, WorldViewProjection);

	// Write minimum-precision floating-point data.
	output.pos = pos;

	// Pass the color through without modification.
	output.color = input.color;

	// Set the instance ID. The pass-through geometry shader will set the
	// render target array index to whatever value is set here.
	output.viewId = idx;

	return output;
}
