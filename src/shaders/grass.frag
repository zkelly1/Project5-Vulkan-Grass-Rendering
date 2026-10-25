#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 normal;
layout(location = 1) in float bladeHeight;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 rootColor = vec3(0.05, 0.16, 0.025);
    vec3 tipColor = vec3(0.35, 0.58, 0.12);
    vec3 color = mix(rootColor, tipColor, bladeHeight);
    vec3 lightDirection = normalize(vec3(1.0, 2.0, 1.0));
    // Blades have no thickness, so light both sides of the strip.
    float diffuse = abs(dot(normalize(normal), lightDirection));
    outColor = vec4(color * (0.35 + 0.65 * diffuse), 1.0);
}
