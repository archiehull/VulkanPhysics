#include "ParticleLibrary.h"
#include <glm/glm.hpp>

namespace ParticleLibrary {

    namespace {
        // Helper to avoid duplicated initialization code
        ParticleProps CreateProps(
            const glm::vec3& velocity,
            const glm::vec3& velocityVar,
            const glm::vec4& colorBegin,
            const glm::vec4& colorEnd,
            float sizeBegin,
            float sizeEnd,
            float sizeVar,
            float lifeTime,
            const std::string& texturePath,
            bool isAdditive)
        {
            ParticleProps p;
            p.velocity = velocity;
            p.velocityVariation = velocityVar;
            p.colorBegin = colorBegin;
            p.colorEnd = colorEnd;
            p.sizeBegin = sizeBegin;
            p.sizeEnd = sizeEnd;
            p.sizeVariation = sizeVar;
            p.lifeTime = lifeTime;
            p.texturePath = texturePath;
            p.isAdditive = isAdditive;
            return p;
        }
    }

    const ParticleProps& GetFireProps() {
        static const ParticleProps props = CreateProps(
            glm::vec3(0.0f, 2.0f, 0.0f),        // Velocity
            glm::vec3(0.5f, 1.0f, 0.5f),        // Velocity Variation
            glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),  // Color Begin
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),  // Color End
            0.5f,                               // Size Begin
            0.1f,                               // Size End
            0.3f,                               // Size Variation
            1.0f,                               // Lifetime
            "textures/kenney_particle-pack/transparent/fire_01.png", // Texture
            true                                // Is Additive
        );
        return props;
    }

    const ParticleProps& GetSmokeProps() {
        static const ParticleProps props = CreateProps(
            glm::vec3(0.0f, 1.5f, 0.0f),        // Velocity
            glm::vec3(0.8f, 0.5f, 0.8f),        // Velocity Variation
            glm::vec4(0.2f, 0.2f, 0.2f, 0.8f),  // Color Begin
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),  // Color End
            0.2f,                               // Size Begin
            1.5f,                               // Size End
            0.5f,                               // Size Variation
            3.0f,                               // Lifetime
            "textures/kenney_particle-pack/transparent/smoke_01.png", // Texture
            false                               // Is Additive
        );
        return props;
    }

    const ParticleProps& GetRainProps() {
        static const ParticleProps props = CreateProps(
            glm::vec3(0.0f, -30.0f, 0.0f),      // Velocity (Faster fall)
            glm::vec3(0.2f, 2.0f, 0.2f),        // Velocity Variation
            glm::vec4(0.0f, 0.3f, 1.0f, 1.0f),  // Color Begin
            glm::vec4(0.0f, 0.3f, 1.0f, 0.6f),  // Color End
            0.20f,                              // Size Begin (Increased from 0.15)
            0.20f,                              // Size End   (Increased from 0.15)
            0.05f,                              // Size Variation
            4.0f,                               // Lifetime
            "textures/kenney_particle-pack/transparent/circle_05.png",
            true
        );
        return props;
    }

    const ParticleProps& GetSnowProps() {
        static const ParticleProps props = CreateProps(
            glm::vec3(0.0f, -2.5f, 0.0f),       // Velocity
            glm::vec3(1.5f, 0.2f, 1.5f),        // Velocity Variation
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),  // Color Begin (Max opacity)
            glm::vec4(1.0f, 1.0f, 1.0f, 0.8f),  // Color End   (Higher fade)
            1.2f,                               // Size Begin (Doubled from 0.6)
            1.2f,                               // Size End   (Doubled from 0.6)
            0.4f,                               // Size Variation
            12.0f,                              // Lifetime
            "textures/kenney_particle-pack/transparent/star_01.png",
            true
        );
        return props;
    }

    const ParticleProps& GetDustProps() {
        static const ParticleProps props = CreateProps(
            glm::vec3(0.5f, 0.1f, 0.5f),        // Velocity
            glm::vec3(1.0f, 0.2f, 1.0f),        // Velocity Variation
            glm::vec4(0.8f, 0.7f, 0.5f, 0.4f),  // Color Begin
            glm::vec4(0.8f, 0.7f, 0.5f, 0.0f),  // Color End
            0.05f,                              // Size Begin
            0.05f,                              // Size End
            0.02f,                              // Size Variation
            5.0f,                               // Lifetime
            "textures/kenney_particle-pack/transparent/circle_02.png", // Texture
            false                               // Is Additive
        );
        return props;
    }

    const ParticleProps& GetDustStormProps() {
        static const ParticleProps props = []() {
            ParticleProps p = CreateProps(
                glm::vec3(0.0f, 0.0f, 0.0f),         // Velocity (Scene overrides this based on direction)
                glm::vec3(5.0f),                     // Velocity Variation
                glm::vec4(0.7f, 0.6f, 0.4f, 0.8f),   // Color Begin (Darker Sand)
                glm::vec4(0.7f, 0.6f, 0.4f, 0.0f),   // Color End
                2.5f,                                // Size Begin (Huge)
                3.0f,                               // Size End
                1.25f,                                // Size Variation
                3.0f,                                // Lifetime
                "textures/kenney_particle-pack/transparent/smoke_02.png",
                false                                // Is Additive? No (Alpha blend for dust)
            );

            p.positionVariation = glm::vec3(4.0f, 2.0f, 4.0f);

            return p;
            }(); 
        return props;
    }

    std::vector<std::pair<std::string, ParticleProps>> GetAllPresets() {
        return {
            {"Fire", GetFireProps()},
            {"Smoke", GetSmokeProps()},
            {"Rain", GetRainProps()},
            {"Snow", GetSnowProps()},
            {"Dust", GetDustProps()},
            {"Dust Storm", GetDustStormProps()}
        };
    }

} // namespace ParticleLibrary