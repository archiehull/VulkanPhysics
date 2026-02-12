#version 450
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstantObject {
    mat4 model;
    int shadingMode;
} pco;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    mat4 lightSpaceMatrix;
} ubo;

void main() {
    gl_Position = ubo.lightSpaceMatrix * pco.model * vec4(inPosition, 1.0);
}