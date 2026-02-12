#version 450

layout(location = 0) in vec3 inPos;      // Quad vertex pos (from static buffer)
layout(location = 1) in vec2 inUV;       // Quad UV (from static buffer)

// Instanced Data (changes per particle)
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in vec4 inInstanceColor;
layout(location = 4) in float inInstanceSize;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    // ... other UBO fields ignore for now
} ubo;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

void main() {
    fragColor = inInstanceColor;
    fragUV = inUV;

    // Billboarding: Extract Camera Right and Up vectors from View Matrix
    // View Matrix columns 0, 1, 2 correspond to Right, Up, Forward in World Space
    vec3 cameraRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 cameraUp    = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);

    // Calculate vertex position: Center + (Right * x * size) + (Up * y * size)
    vec3 vertexPosWorld = inInstancePos 
        + (cameraRight * inPos.x * inInstanceSize) 
        + (cameraUp * inPos.y * inInstanceSize);

    gl_Position = ubo.proj * ubo.view * vec4(vertexPosWorld, 1.0);
}