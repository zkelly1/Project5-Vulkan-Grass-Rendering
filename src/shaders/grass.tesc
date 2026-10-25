#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec4 v0[];
layout(location = 1) out vec4 v1[];
layout(location = 2) out vec4 v2[];
layout(location = 3) out vec4 up[];

void main() {
    v0[gl_InvocationID] = inV0[gl_InvocationID];
    v1[gl_InvocationID] = inV1[gl_InvocationID];
    v2[gl_InvocationID] = inV2[gl_InvocationID];
    up[gl_InvocationID] = inUp[gl_InvocationID];
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    // One segment across the width and eight along the curve.
    gl_TessLevelInner[0] = 1.0;
    gl_TessLevelInner[1] = 8.0;
    gl_TessLevelOuter[0] = 8.0;
    gl_TessLevelOuter[1] = 1.0;
    gl_TessLevelOuter[2] = 8.0;
    gl_TessLevelOuter[3] = 1.0;
}
