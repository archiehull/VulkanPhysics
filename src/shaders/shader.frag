#version 450

#define MAX_LIGHTS 512

struct Light {
    vec3 position;
    vec3 color;
    vec3 direction; // NEW
    float intensity;
    int type;
    int layerMask;
    float cutoffAngle; // NEW
    float padding;
};

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 fragGouraudColor;
layout(location = 5) in vec4 fragPosLightSpace;
layout(location = 6) in vec3 fragOtherLightColor;

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

layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D refractionSampler;
layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

float ShadowCalculation(vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if(projCoords.z > 1.0 || projCoords.z < 0.0) return 0.0;
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(ubo.lights[0].position - fragPos);
    
    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.0001);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }    
    }
    shadow /= 9.0;
    
    return shadow;
}

void main() {
    // --- 1. SCREEN-SPACE REFRACTION MODE ---
    if (pco.shadingMode == 3) {
        vec3 I = normalize(fragPos - ubo.viewPos);
        vec3 N = normalize(fragNormal);
        
        vec2 viewportSize = vec2(textureSize(refractionSampler, 0));
        vec2 screenUV = gl_FragCoord.xy / viewportSize;
        vec2 distortion = N.xy * 0.40;
        vec2 refractedUV = screenUV + distortion;

        vec3 refractionColor = texture(refractionSampler, refractedUV).rgb;
        vec3 foggyTint = vec3(0.9, 0.95, 1.0) * 0.2;
        vec3 baseColor = mix(refractionColor, foggyTint, 0.6);

        vec3 lightDir = normalize(ubo.lights[0].position - fragPos);
        vec3 H = normalize(lightDir - I);
        float spec = pow(max(dot(N, H), 0.0), 64.0);
        vec3 specularColor = spec * ubo.lights[0].color * 1.5;

        float fresnel = pow(1.0 - max(dot(-I, N), 0.0), 2.0);
        float alpha = clamp(0.2 + (fresnel * 0.7), 0.0, 1.0);
        
        outColor = vec4(baseColor + specularColor, alpha);
        return;
    }

    // --- 2. DARK GLOSS SHELL ---
    if (pco.shadingMode == 4) {
        vec3 I = normalize(fragPos - ubo.viewPos);
        vec3 N = normalize(fragNormal);

        vec3 darkTint = vec3(0.02, 0.02, 0.05);
        vec3 lightDir = normalize(ubo.lights[0].position - fragPos);
        vec3 H = normalize(lightDir - I);

        float spec = pow(max(dot(N, H), 0.0), 8.0);
        vec3 specularColor = spec * ubo.lights[0].color * 0.5;
        float fresnel = pow(1.0 - max(dot(-I, N), 0.0), 3.0);
        float alpha = clamp(0.25 + (fresnel * 0.6), 0.0, 1.0);

        outColor = vec4(darkTint + specularColor, alpha);
        return;
    }

    // --- STANDARD LIGHTING ---
    vec4 texColor = texture(texSampler, fragUV);
    vec3 lighting = vec3(0.0);

    float shadow = ShadowCalculation(fragPosLightSpace);

    // Fade shadows at low sun angles
    if (ubo.numLights > 0) {
        float sunHeight = ubo.lights[0].position.y;
        float fadeStart = 100.0;
        float fadeEnd = 15.0; 
        
        float shadowFade = clamp((sunHeight - fadeEnd) / (fadeStart - fadeEnd), 0.0, 1.0);
        shadowFade = shadowFade * shadowFade * (3.0 - 2.0 * shadowFade);
        shadow *= shadowFade;
    }

    if (pco.receiveShadows == 0) {
        shadow = 0.0;
    }

    if (pco.shadingMode == 0) {
        // Gouraud (calculated in vertex shader)
        lighting = fragGouraudColor * (1.0 - shadow) + fragOtherLightColor;
    } else {
        // Phong (calculated per-pixel)
        vec3 normal = normalize(fragNormal);
        vec3 viewDir = normalize(ubo.viewPos - fragPos);

for(int i = 0; i < ubo.numLights; i++) {
            if ((ubo.lights[i].layerMask & pco.layerMask) == 0) continue;

            float distance = length(ubo.lights[i].position - fragPos);
            float attenuation = 1.0;
            float specularStrength = 0.5;
            float spotIntensity = 1.0; // <--- NEW

            if (ubo.lights[i].type == 1) {
                // Fire
                attenuation = 1.0 / (1.0 + 15.0 * distance + 45.0 * distance * distance);
                specularStrength = 0.05;
            } 
            else if (ubo.lights[i].type == 2) {
                // Point Light
                attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
            }
            else if (ubo.lights[i].type == 3) {
                // --- NEW: SPOTLIGHT ---
                attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
                
                // Vector pointing from the light TO the fragment
                vec3 lightDirToFrag = normalize(fragPos - ubo.lights[i].position);
                
                // Dot product gives us the cosine of the angle between direction and fragment
                float theta = dot(lightDirToFrag, normalize(ubo.lights[i].direction));
                
                // Create a smooth soft edge for the spotlight
                float innerCutoff = ubo.lights[i].cutoffAngle;
                float outerCutoff = innerCutoff - 0.05; // 0.05 spread for a nice soft edge
                
                spotIntensity = clamp((theta - outerCutoff) / (innerCutoff - outerCutoff), 0.0, 1.0);
            }
            else {
                // Sun
                attenuation = 1.0;
                specularStrength = 0.5;
            }

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

            // Shadows (Sun only)
            float lightShadow = 0.0;
            if (i == 0) {
                lightShadow = shadow;
            }

            lighting += (ambient + (1.0 - lightShadow) * (diffuse + specular)) * attenuation * spotIntensity;
        }
    }

    vec3 sootColor = vec3(0.05, 0.05, 0.05);
    vec3 finalColor = lighting * texColor.rgb;
    if (pco.burnFactor > 0.0) {
        finalColor = mix(finalColor, sootColor, pco.burnFactor * 0.9);
    }

    outColor = vec4(finalColor, texColor.a);
}