#version 450

// Must define MAX_LIGHTS and Light struct to match UBO layout exactly
#define MAX_LIGHTS 512

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
    int type;
    int layerMask;
    float padding;
};

layout(location = 0) in vec3 inUVW;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    mat4 lightSpaceMatrix;
    Light lights[MAX_LIGHTS];
    int numLights;
    float dayNightFactor; // 0.0 = Night, 1.0 = Day
} ubo;

layout(set = 1, binding = 0) uniform samplerCube skyboxSampler;

void main() {
    vec4 texColor = texture(skyboxSampler, inUVW);

    // 1. Calculate Grayscale (Desaturation)
    float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    vec3 desaturated = vec3(gray);

    // 2. Mix based on dayNightFactor
    // If factor is 1.0 (Day), we use full texColor.
    // If factor is 0.0 (Night), we use desaturated color.
    vec3 finalColor = mix(desaturated, texColor.rgb, ubo.dayNightFactor);

    // 3. Darken for Night
    // We don't want it to be pitch black, so keep a minimum brightness (e.g. 0.05)
    float brightness = 0.05 + 0.95 * ubo.dayNightFactor;
    finalColor *= brightness;

    outColor = vec4(finalColor, texColor.a);
}