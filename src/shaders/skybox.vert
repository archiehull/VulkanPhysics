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

layout(location = 0) out vec3 outUVW;

void main() {
    // Calculate the world position of the vertex
    vec3 worldPos = vec3(pco.model * vec4(inPosition, 1.0));

    // CHANGE: Calculate the direction from the camera to the vertex.
    // This view vector is used to sample the cubemap, creating the illusion
    // that the texture is at infinite depth (like a real skybox or portal).
    outUVW = worldPos - ubo.viewPos;

    // Standard projection
    gl_Position = ubo.proj * ubo.view * pco.model * vec4(inPosition, 1.0);
}