#version 450

#define MAX_LIGHTS 512

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
    int type;
    int layerMask;
    float padding;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    mat4 lightSpaceMatrix;
    Light lights[MAX_LIGHTS];
    int numLights;
    float dayNightFactor; 
} ubo;

layout(push_constant) uniform PushConstantObject {
    mat4 model;
    int shadingMode;
    int receiveShadows; 
    int layerMask;      
    float burnFactor;
} pco;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec3 fragGouraudColor;
layout(location = 5) out vec4 fragPosLightSpace;
layout(location = 6) out vec3 fragOtherLightColor;

void main() {
    vec4 worldPos = pco.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragColor = inColor;
    fragUV = inTexCoord;
    
    mat3 normalMatrix = mat3(transpose(inverse(pco.model)));
    vec3 normal = normalize(normalMatrix * inNormal);
    
    fragNormal = normal;
    fragPos = vec3(worldPos);

    fragPosLightSpace = ubo.lightSpaceMatrix * worldPos;

    // Initialize accumulators
    vec3 sunColor = vec3(0.0);
    vec3 otherColor = vec3(0.0);

    // --- GOURAUD SHADING CALCULATION ---
    // (Calculated per-vertex for performance)
    if (pco.shadingMode == 0) {
        vec3 viewDir = normalize(ubo.viewPos - fragPos);

        for(int i = 0; i < ubo.numLights; i++) {
            // Skip lights that don't affect this object's layer
            if ((ubo.lights[i].layerMask & pco.layerMask) == 0) {
                continue;
            }

            float distance = length(ubo.lights[i].position - fragPos);
            float attenuation = 1.0;
            float specularStrength = 0.5;

            if (ubo.lights[i].type == 1) {
                // --- FIRE / POINT LIGHT ---
                // Apply the same extreme falloff as the Fragment Shader
                attenuation = 1.0 / (1.0 + 15.0 * distance + 45.0 * distance * distance);
                specularStrength = 0.05; // Reduced specular for fire
            } 
            else {
                // --- SUN (Type 0) ---
                attenuation = 1.0;
            }

            // Ambient (Only Sun adds ambient)
            vec3 ambient = vec3(0.0);
            if (ubo.lights[i].type == 0) {
                 float ambientStrength = 0.1;
                 ambient = ambientStrength * ubo.lights[i].color * ubo.lights[i].intensity;
            }

            // Diffuse
            vec3 lightDir = normalize(ubo.lights[i].position - fragPos);
            float diff = max(dot(normal, lightDir), 0.0);
            vec3 diffuse = diff * ubo.lights[i].color * ubo.lights[i].intensity;

            // Specular
            vec3 reflectDir = reflect(-lightDir, normal);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
            vec3 specular = specularStrength * spec * ubo.lights[i].color * ubo.lights[i].intensity;

            // Apply attenuation to Diffuse + Specular only
            vec3 result = ambient + (diffuse + specular) * attenuation;

            // Sort into Sun (0) vs Other (1+) buckets
            if (i == 0) {
                sunColor += result;
            } else {
                otherColor += result;
            }
        }
    }
    
    fragGouraudColor = sunColor;
    fragOtherLightColor = otherColor;
}