#include "FlatBufferSceneLoader.h"
#include "../rendering/Scene.h"
#include "Components.h"
#include "../systems/PhysicsSystem.h"
#include "../core/scene_generated.h"
#include "Config.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <exception>

static bool s_verbose_fb_loader = false;
static bool s_debug_fb_loader = false;

void FlatBufferSceneLoader::SetVerbose(bool v) {
    s_verbose_fb_loader = v;
}

struct MaterialData {
    float density = 1.0f;
    float restitution = 1.0f;
float friction = 0.05f; 
};

struct MaterialInteractionData {
    std::string materialA;
    std::string materialB;
    float restitution = 0.0f;
    float dynamicFriction = 0.0f;
};

// Helper to safely extract Vec3 with a default fallback
static glm::vec3 SafeGetVec3(const Simulation::Vec3* vec, const glm::vec3& defaultValue = glm::vec3(0.0f)) {
    if (!vec) return defaultValue;
    return glm::vec3(vec->x(), vec->y(), vec->z());
}

static glm::vec3 SafeGetVec3(const Simulation::Vec3& vec) {
    return glm::vec3(vec.x(), vec.y(), vec.z());
}

// Helper to safely extract RotationEuler with a default fallback
static glm::vec3 SafeGetEuler(const Simulation::RotationEuler* rot, const glm::vec3& defaultValue = glm::vec3(0.0f)) {
    if (!rot) return defaultValue;
    // The engine uses pitch (x), yaw (y), roll (z) in degrees
    return glm::vec3(rot->pitch(), rot->yaw(), rot->roll());
}

static glm::vec3 SafeGetEuler(const Simulation::RotationEuler& rot) {
    return glm::vec3(rot.pitch(), rot.yaw(), rot.roll());
}

static const char* ShapeTypeToString(Simulation::Shape shape) {
    switch (shape) {
        case Simulation::Shape_Sphere: return "Sphere";
        case Simulation::Shape_Cuboid: return "Cuboid";
        case Simulation::Shape_Cylinder: return "Cylinder";
        case Simulation::Shape_Capsule: return "Capsule";
        case Simulation::Shape_Plane: return "Plane";
        default: return "Unknown";
    }
}

static const char* BehaviourTypeToString(Simulation::Behaviour behaviour) {
    switch (behaviour) {
        case Simulation::Behaviour_StaticObject: return "Static";
        case Simulation::Behaviour_SimulatedObject: return "Simulated";
        default: return "Unknown";
    }
}

static float CalculateVolume(const Simulation::Object* fbObj, const glm::vec3& scale) {
    if (!fbObj) return 1.0f;
    float volume = 1.0f;
    
    switch (fbObj->shape_type()) {
        case Simulation::Shape_Sphere: {
            auto sphere = fbObj->shape_as_Sphere();
            float r = sphere ? sphere->radius() : 1.0f;
            r *= std::max({scale.x, scale.y, scale.z});
            volume = (4.0f / 3.0f) * glm::pi<float>() * r * r * r;
            break;
        }
        case Simulation::Shape_Cuboid: {
            auto cube = fbObj->shape_as_Cuboid();
            glm::vec3 size = cube ? SafeGetVec3(cube->size(), glm::vec3(1.0f)) : glm::vec3(1.0f);
            size *= scale;
            volume = size.x * size.y * size.z;
            break;
        }
        case Simulation::Shape_Cylinder:
        case Simulation::Shape_Capsule: {
            float r = 1.0f;
            float h = 2.0f;
            if (fbObj->shape_type() == Simulation::Shape_Cylinder) {
                auto cyl = fbObj->shape_as_Cylinder();
                if (cyl) { r = cyl->radius(); h = cyl->height(); }
            } else {
                auto cap = fbObj->shape_as_Capsule();
                if (cap) { r = cap->radius(); h = cap->height(); }
            }
            r *= std::max(scale.x, scale.z);
            h *= scale.y;
            volume = glm::pi<float>() * r * r * h;
            if (fbObj->shape_type() == Simulation::Shape_Capsule) {
                volume += (4.0f / 3.0f) * glm::pi<float>() * r * r * r;
            }
            break;
        }
        case Simulation::Shape_Plane:
            volume = 0.0f; // Planes typically have infinite or zero mass
            break;
        default:
            break;
    }
    return volume;
}

static MaterialData ResolveMaterialData(
    const std::string& materialName,
    const std::unordered_map<std::string, MaterialData>& materials)
{
    auto it = materials.find(materialName);
    if (it != materials.end()) {
        return it->second;
    }
    return MaterialData{};
}

static void ApplyMaterialToPhysicsComponent(
    PhysicsComponent& physComp,
    const Simulation::Object* fbObj,
    const std::unordered_map<std::string, MaterialData>& materials)
{
    physComp.restitution = 1.0f;
    physComp.friction = 0.05f;

    if (!fbObj || !fbObj->material()) {
        return;
    }

    const std::string materialName = fbObj->material()->str();
    const MaterialData material = ResolveMaterialData(materialName, materials);

    physComp.restitution = material.restitution;
    physComp.friction = material.friction;
}

static void ParseObject(Scene& scene, const Simulation::Object* fbObj, const std::unordered_map<std::string, MaterialData>& materials) {
    if (!fbObj) return;

    // 1. Strings: Default Name and Texture
    std::string name = fbObj->name() ? fbObj->name()->str() : "UnnamedObject_" + std::to_string(scene.GetRegistry().GetEntityCount());
    std::string defaultTexture = "grey_solid";

    if (s_debug_fb_loader) {
        std::cout << "[FlatBufferSceneLoader] Object '" << name << "' shape=" << ShapeTypeToString(fbObj->shape_type())
                  << " behaviour=" << BehaviourTypeToString(fbObj->behaviour_type()) << std::endl;
    }

    // 2. Transform: Default to Origin, No Rotation, Scale of 1
    glm::vec3 pos = glm::vec3(0.0f);
    glm::vec3 rotEuler = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    if (auto fbTrans = fbObj->transform()) {
        pos = SafeGetVec3(fbTrans->position());
        rotEuler = SafeGetEuler(fbTrans->orientation());
        scale = SafeGetVec3(fbTrans->scale());
    }

    Entity entity = MAX_ENTITIES;

    // 3. Shapes
    switch (fbObj->shape_type()) {
        case Simulation::Shape_Sphere: {
            auto sphere = fbObj->shape_as_Sphere();
            float r = sphere ? sphere->radius() : 1.0f;
            
            // Set Visuals (Unit Sphere scaled by radius*2 * transform scale)
            glm::vec3 finalScale = scale * glm::vec3(r * 2.0f);
            entity = scene.AddSphere(name, 16, 16, pos, finalScale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 0; // Sphere
            col.radius = r * std::max({scale.x, scale.y, scale.z}); 
            col.collisionSide = CollisionSide::OUTSIDE; 
            col.hasCollision = true;
            break;
        }
        case Simulation::Shape_Cuboid: {
            auto cube = fbObj->shape_as_Cuboid();
            glm::vec3 size = cube ? SafeGetVec3(cube->size(), glm::vec3(1.0f)) : glm::vec3(1.0f);
            
            // Set Visuals (Unit Cube scaled to size * transform scale)
            glm::vec3 finalScale = scale * size;
            entity = scene.AddCube(name, pos, finalScale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 3; // Box/AABB
            col.halfExtents = finalScale * 0.5f; // Physics needs half-extents
            col.radius = std::max({finalScale.x, finalScale.y, finalScale.z}) * 0.5f; // Fallback for radius-based checks
            col.collisionSide = CollisionSide::INSIDE; 
            col.hasCollision = true;
            break;
        }
        case Simulation::Shape_Cylinder: {
            auto cylinder = fbObj->shape_as_Cylinder();
            float r = cylinder ? cylinder->radius() : 1.0f;
            float h = cylinder ? cylinder->height() : 2.0f;
            
            // Set Visuals (Unit Cylinder scaled by radius*2 and height)
            glm::vec3 finalScale = scale * glm::vec3(r * 2.0f, h, r * 2.0f);
            entity = scene.AddCylinder(name, 16, pos, finalScale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 2; // Capsule/Cylinder
            col.hasCollision = true;
            break;
        }
        case Simulation::Shape_Capsule: {
            auto capsule = fbObj->shape_as_Capsule();
            float r = capsule ? capsule->radius() : 0.5f;
            float h = capsule ? capsule->height() : 2.0f;
            
            // The engine might not have AddCapsule, using AddCylinder as fallback
            glm::vec3 finalScale = scale * glm::vec3(r * 2.0f, h, r * 2.0f);
            entity = scene.AddCylinder(name, 16, pos, finalScale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 2; // Capsule
            col.hasCollision = true;
            break;
        }
        case Simulation::Shape_Plane: {
            auto plane = fbObj->shape_as_Plane();
            glm::vec3 normal = plane ? SafeGetVec3(plane->normal(), glm::vec3(0.0f, 1.0f, 0.0f)) : glm::vec3(0.0f, 1.0f, 0.0f);
            
            entity = scene.AddPlane(name, pos, scale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 1; // Plane
            col.normal = glm::normalize(normal);
            col.radius = 0.0f; // 0.0f flags it as infinite for the physics engine
            col.height = 0.0f;
            break;
        }
        default:
            if (s_verbose_fb_loader) {
                std::cerr << "[FlatBufferSceneLoader] Warning: Object '" << name << "' has no valid shape. Skipping." << std::endl;
            }
            return;
    }

    if (entity == MAX_ENTITIES) return;
    
    // Set default opacity based on shape
    if (scene.GetRegistry().HasComponent<RenderComponent>(entity)) {
        auto& renderComp = scene.GetRegistry().GetComponent<RenderComponent>(entity);
        if (fbObj->shape_type() != Simulation::Shape_Sphere) {
            renderComp.opacity = 0.3f;
        } else {
            renderComp.opacity = 1.0f;
        }
    }

    if (s_debug_fb_loader) {
        std::cout << "[FlatBufferSceneLoader] Object '" << name << "' created entity=" << entity << std::endl;
    }

    // Support for Container/Inverted rendering could be handled here if engine supports it.
    if (fbObj->collision_type() == Simulation::CollisionType_CONTAINER) {
        // Depending on RenderComponent properties...
    }

    // 4. Update ECS Transform Component
    try {
        auto& transComp = scene.GetRegistry().GetComponent<TransformComponent>(entity);
        transComp.position = pos;
        transComp.rotation = rotEuler;
        // Scale is already set by AddSphere/AddCube/AddCylinder/AddPlane logic above
        transComp.UpdateMatrix();
    } catch (...) {
        if (s_verbose_fb_loader) {
            std::cerr << "[FlatBufferSceneLoader] Error setting TransformComponent for entity created from object '" << name << "'" << std::endl;
        }
    }

    // 5. Update ECS Physics Component
    if (!scene.GetRegistry().HasComponent<PhysicsComponent>(entity)) {
        scene.GetRegistry().AddComponent<PhysicsComponent>(entity, PhysicsComponent{});
    }
    auto& physComp = scene.GetRegistry().GetComponent<PhysicsComponent>(entity);
    
    float density = 1.0f;
    if (fbObj->material()) {
        const auto materialIt = materials.find(fbObj->material()->str());
        if (materialIt != materials.end()) {
            density = materialIt->second.density;
        }
    }

    ApplyMaterialToPhysicsComponent(physComp, fbObj, materials);

    if (fbObj->behaviour_type() == Simulation::Behaviour_StaticObject || fbObj->shape_type() == Simulation::Shape_Plane) {
        physComp.isStatic = true;
        physComp.SetMass(0.0f);
        physComp.velocity = glm::vec3(0.0f);
        physComp.angularVelocity = glm::vec3(0.0f);
    } 
    else if (fbObj->behaviour_type() == Simulation::Behaviour_SimulatedObject) {
        physComp.isStatic = false;
        auto simObj = fbObj->behaviour_as_SimulatedObject();
        
        // Safely extract physics state (velocities)
        if (simObj && simObj->initial_state()) {
            auto state = simObj->initial_state();
            physComp.velocity = SafeGetVec3(state->linear_velocity());
            physComp.angularVelocity = SafeGetVec3(state->angular_velocity());
        } else {
            physComp.velocity = glm::vec3(0.0f);
            physComp.angularVelocity = glm::vec3(0.0f);
        }

        // Calculate Mass = Density * Volume
        float volume = CalculateVolume(fbObj, scale);
        physComp.SetMass(density * volume);
    }
}

bool FlatBufferSceneLoader::LoadScene(Scene& scene, AppConfig& config, const std::string& filepath) {
    try {
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Begin loading: " << filepath << std::endl;
        }
        // 1. Read the binary file
        std::ifstream infile(filepath, std::ios::binary);
        if (!infile) {
            if (s_verbose_fb_loader) {
                std::cerr << "[FlatBufferSceneLoader] Failed to open: " << filepath << std::endl;
            }
            return false;
        }

        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Reading file contents..." << std::endl;
        }
        
        infile.seekg(0, std::ios::end);
        std::streamoff tell = infile.tellg();
        if (tell <= 0) {
            if (s_verbose_fb_loader) {
                std::cerr << "[FlatBufferSceneLoader] File is empty or tellg failed: " << filepath << std::endl;
            }
            return false;
        }
        size_t length = static_cast<size_t>(tell);
        infile.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(length);
        infile.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(length));
        infile.close();

        if (s_verbose_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Read file: " << filepath << " (" << length << " bytes)" << std::endl;
        }

        // 2. Get the root Scene object
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Parsing FlatBuffer root..." << std::endl;
        }
        auto fbScene = Simulation::GetScene(data.data());
        if (!fbScene) {
            if (s_verbose_fb_loader) {
                std::cerr << "[FlatBufferSceneLoader] Failed to parse FlatBuffer data from: " << filepath << std::endl;
            }
            return false;
        }

        if (s_verbose_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Parsed FB scene. Gravity_on=" << (fbScene->gravity_on() ? "yes" : "no") << std::endl;
        }

        // 3. Apply global settings
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Applying global settings..." << std::endl;
        }
        PhysicsSystem::applyGravity = fbScene->gravity_on();

        // 4. Cache Materials
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Caching materials..." << std::endl;
        }
        std::unordered_map<std::string, MaterialData> materials;
        size_t materialCount = 0;
        if (fbScene->materials()) {
            for (const auto* mat : *fbScene->materials()) {
                if (mat && mat->name()) {
                    // Default density to 1.0f if not specified or zero
                    float density = mat->density() != 0.0f ? mat->density() : 1.0f;
                    materials[mat->name()->str()] = MaterialData{ density, 1.0f, 0.05f };
                    materialCount++;
                }
            }
        }
        
        // 4b. Parse Interactions to "bake" restitution and friction into materials
        // We focus on interactions with "PlaneMat" as the baseline for objects
        if (fbScene->interactions()) {
            for (const auto* inter : *fbScene->interactions()) {
                if (!inter) continue;
                std::string matA = inter->material_a() ? inter->material_a()->str() : "";
                std::string matB = inter->material_b() ? inter->material_b()->str() : "";
                
                float res = inter->restitution();
                float fric = inter->dynamic_friction();

                if (matB == "PlaneMat" && materials.count(matA)) {
                    materials[matA].restitution = res;
                    materials[matA].friction = fric;
                } else if (matA == "PlaneMat" && materials.count(matB)) {
                    materials[matB].restitution = res;
                    materials[matB].friction = fric;
                }
            }
        }

        if (s_verbose_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Materials: " << materialCount << std::endl;
        }

        // 5. Parse Objects
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Parsing objects..." << std::endl;
        }
        size_t objectCount = fbScene->objects() ? fbScene->objects()->size() : 0;
        if (s_verbose_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Objects: " << objectCount << std::endl;
        }

        if (fbScene->objects()) {
            size_t idx = 0;
            for (const auto* fbObj : *fbScene->objects()) {
                // Print progress every 10 items to avoid excessive I/O
                if (s_verbose_fb_loader && (idx % 10 == 0 || idx + 1 == objectCount)) {
                    std::cout << "[FlatBufferSceneLoader] Parsing object " << idx << "/" << objectCount << std::endl;
                }
                try {
                    ParseObject(scene, fbObj, materials);
                } catch (const std::exception& e) {
                    std::cerr << "[FlatBufferSceneLoader] Exception while parsing object " << idx << ": " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[FlatBufferSceneLoader] Unknown exception while parsing object " << idx << std::endl;
                }
                ++idx;
            }
        }

        // 6. Cameras
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Parsing cameras..." << std::endl;
        }
        if (fbScene->cameras()) {
            for (const auto* fbCam : *fbScene->cameras()) {
                if (!fbCam) continue;
                CustomCameraConfig camCfg;
                camCfg.name = fbCam->name() ? fbCam->name()->str() : "UnnamedCamera";
                if (fbCam->transform()) {
                    camCfg.position = SafeGetVec3(fbCam->transform()->position());
                    glm::vec3 rot = SafeGetEuler(fbCam->transform()->orientation());
                    camCfg.pitch = rot.x;
                    camCfg.yaw = rot.y;
                }
                camCfg.type = "FreeRoam"; // Default fallback
                config.customCameras.push_back(camCfg);
            }
        }

        // 7. Lights
        if (s_debug_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Parsing lights..." << std::endl;
        }
        if (fbScene->lights()) {
            for (const auto* fbLight : *fbScene->lights()) {
                if (!fbLight) continue;
                std::string lName = fbLight->name() ? fbLight->name()->str() : "UnnamedLight";
                glm::vec3 lPos = SafeGetVec3(fbLight->position());
                glm::vec3 lColor = SafeGetVec3(fbLight->color(), glm::vec3(1.0f));
                float lIntensity = fbLight->intensity();
                int lType = static_cast<int>(fbLight->type());
                scene.AddLight(lName, lPos, lColor, lIntensity, lType);
            }
        }

        if (s_verbose_fb_loader) {
            std::cout << "[FlatBufferSceneLoader] Finished loading scene: " << filepath << std::endl;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FlatBufferSceneLoader] Unhandled exception loading scene '" << filepath << "': " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[FlatBufferSceneLoader] Unknown unhandled exception loading scene '" << filepath << "'" << std::endl;
        return false;
    }
}
