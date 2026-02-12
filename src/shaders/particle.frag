#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    // Multiply texture color by particle color (fading happens in fragColor.a)
    outColor = texture(texSampler, fragUV) * fragColor;
    
    // Discard transparent pixels (optional, depends on blending)
    if (outColor.a < 0.01) discard;
}