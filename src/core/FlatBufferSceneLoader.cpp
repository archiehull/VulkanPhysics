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
    // Engine order: pitch (x), yaw (y), roll (z)
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

static float CalculateVolume(const Simulation::Object* fbObj, const glm::vec3& finalScale) {
    if (!fbObj) return 1.0f;
    float volume = 1.0f;
    
    switch (fbObj->shape_type()) {
        case Simulation::Shape_Sphere: {
            float r = std::max({finalScale.x, finalScale.y, finalScale.z}) * 0.5f;
            volume = (4.0f / 3.0f) * glm::pi<float>() * r * r * r;
            break;
        }
        case Simulation::Shape_Cuboid: {
            volume = finalScale.x * finalScale.y * finalScale.z;
            break;
        }
        case Simulation::Shape_Cylinder:
        case Simulation::Shape_Capsule: {
            float r = std::max(finalScale.x, finalScale.z) * 0.5f;
            float h = finalScale.y;
            volume = glm::pi<float>() * r * r * h;
            if (fbObj->shape_type() == Simulation::Shape_Capsule) {
                volume += (4.0f / 3.0f) * glm::pi<float>() * r * r * r;
            }
            break;
        }
        case Simulation::Shape_Plane:
            volume = 0.0f; 
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
            
            // Apply shape's radius to the transform scale
            scale *= (r * 2.0f);
            entity = scene.AddSphere(name, 16, 16, pos, scale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 0; // Sphere
            col.radius = std::max({scale.x, scale.y, scale.z}) * 0.5f; 
            col.collisionSide = CollisionSide::OUTSIDE; 
            col.hasCollision = true;
            break;
        }
        case Simulation::Shape_Cuboid: {
            auto cube = fbObj->shape_as_Cuboid();
            glm::vec3 size = cube ? SafeGetVec3(cube->size(), glm::vec3(1.0f)) : glm::vec3(1.0f);
            
            // Apply shape's size to the transform scale
            scale *= size;
            entity = scene.AddCube(name, pos, scale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 3; // Box/AABB
            col.halfExtents = scale * 0.5f; // Physics needs half-extents
            col.radius = std::max({scale.x, scale.y, scale.z}) * 0.5f; 
            col.collisionSide = CollisionSide::INSIDE; 
            col.hasCollision = true;
            break;
        }
        case Simulation::Shape_Cylinder: {
            auto cylinder = fbObj->shape_as_Cylinder();
            float r = cylinder ? cylinder->radius() : 1.0f;
            float h = cylinder ? cylinder->height() : 2.0f;
            
            // Apply shape dimensions to the transform scale
            scale *= glm::vec3(r * 2.0f, h, r * 2.0f);
            entity = scene.AddCylinder(name, 32, pos, scale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 2; // Cylinder (using 2 for Capsule/Cylinder parity)
            col.radius = std::max(scale.x, scale.z) * 0.5f;
            col.height = scale.y;
            col.hasCollision = true;
            col.autoScale = false;
            break;
        }
        case Simulation::Shape_Capsule: {
            auto capsule = fbObj->shape_as_Capsule();
            float r = capsule ? capsule->radius() : 0.2f;
            float cylinderBodyL = capsule ? capsule->height() : 1.6f; // Use 1.6f to result in 2.0f total height by default
            
            // Calculate Total Height: H = L + 2r
            float totalHeight = cylinderBodyL + (2.0f * r);

            // Pass the calculated total height to the scene
            entity = scene.AddCapsule(name, r, totalHeight, 32, 16, pos, scale, defaultTexture);
            
            if (!scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
                scene.GetRegistry().AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.type = 2; // Capsule
            col.radius = r * std::max(scale.x, scale.z);
            col.height = totalHeight * scale.y;
            col.hasCollision = true;
            col.autoScale = false;
            break;
        }
        case Simulation::Shape_Plane: {
            auto plane = fbObj->shape_as_Plane();
            glm::vec3 normal = plane ? SafeGetVec3(plane->normal(), glm::vec3(0.0f, 1.0f, 0.0f)) : glm::vec3(0.0f, 1.0f, 0.0f);
            
            // To visually represent an infinite plane, we scale it massively
            scale *= glm::vec3(250.0f, 1.0f, 250.0f);

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
        if (fbObj->shape_type() != Simulation::Shape_Sphere && fbObj->shape_type() != Simulation::Shape_Plane) {
            renderComp.opacity = 0.3f;
        } else {
            renderComp.opacity = 1.0f;
        }
    }

    if (s_debug_fb_loader) {
        std::cout << "[FlatBufferSceneLoader] Object '" << name << "' created entity=" << entity << std::endl;
    }

    // Support for Container/Inverted rendering
    if (fbObj->collision_type() == Simulation::CollisionType_CONTAINER) {
        if (scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
            auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
            col.collisionSide = CollisionSide::INSIDE;
            // Default wall thickness for containers if not specified in FB
            col.wallThickness = 0.2f; 
        }
    }

    // 4. Update ECS Transform Component
    try {
        auto& transComp = scene.GetRegistry().GetComponent<TransformComponent>(entity);
        transComp.position = pos;
        transComp.rotation = rotEuler;
        transComp.scale = scale;
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
        
        // Add ownership component based on SimulatedObject owner
        if (simObj) {
            ObjectOwnershipType ownerType = static_cast<ObjectOwnershipType>(simObj->owner());
            scene.GetRegistry().AddComponent<OwnershipComponent>(entity, { ownerType });

            if (static_cast<int>(ownerType) == PhysicsSystem::localPeerId) {
                scene.RegisterLocallyOwnedNetworkEntity(entity);
            }
        }
    }

    // 6. Support for Animated Objects (Path Animation)
    if (fbObj->behaviour_type() == Simulation::Behaviour_AnimatedObject) {
        auto animatedData = fbObj->behaviour_as_AnimatedObject();
        if (animatedData) {
            PathAnimationComponent pathComp{};
            pathComp.totalDuration = animatedData->total_duration();
            
            // Map Easing
            if (animatedData->easing() == Simulation::EasingType_SMOOTHSTEP) {
                pathComp.easing = PathAnimationEasing::Smoothstep;
            } else {
                pathComp.easing = PathAnimationEasing::Linear;
            }

            // Map Path Mode
            if (animatedData->path_mode() == Simulation::PathMode_LOOP) {
                pathComp.playMode = PathAnimationPlayMode::Loop;
            } else if (animatedData->path_mode() == Simulation::PathMode_REVERSE) {
                pathComp.playMode = PathAnimationPlayMode::Bounce;
            } else {
                pathComp.playMode = PathAnimationPlayMode::Once;
            }

            // Map Waypoints
            if (animatedData->waypoints()) {
                for (const auto* wp : *animatedData->waypoints()) {
                    PathWaypoint waypoint{};
                    waypoint.position = SafeGetVec3(wp->position());
                    waypoint.orientation = SafeGetEuler(wp->rotation());
                    waypoint.timeFromStart = wp->time();
                    pathComp.waypoints.push_back(waypoint);
                }
            }
            
            pathComp.perPointRotation = true;
            pathComp.timingMode = PathAnimationTimingMode::Absolute;
            pathComp.initialized = false;
            pathComp.isPlaying = true;
            pathComp.connectEndToStart = animatedData->connect_end_to_start();
            
            scene.GetRegistry().AddComponent<PathAnimationComponent>(entity, pathComp);
        }
    }

    // 7. Handle Container Collisions
    if (scene.GetRegistry().HasComponent<ColliderComponent>(entity)) {
        auto& col = scene.GetRegistry().GetComponent<ColliderComponent>(entity);
        if (fbObj->collision_type() == Simulation::CollisionType_CONTAINER) {
            col.collisionSide = CollisionSide::INSIDE;
        } else {
            col.collisionSide = CollisionSide::OUTSIDE;
        }
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
                    camCfg.yaw = rot.y; // Map yaw directly (matching JSON loader parity)
                    camCfg.pitch = rot.x;        // Map pitch directly
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
        
        // 8. Spawners
        if (fbScene->spawners() && fbScene->spawners_type()) {
            for (flatbuffers::uoffset_t i = 0; i < fbScene->spawners()->size(); ++i) {
                auto type = fbScene->spawners_type()->Get(i);
                auto data = fbScene->spawners()->Get(i);
                if (!data) continue;
                
                Entity spawnerEntity = scene.GetRegistry().CreateEntity();
                ObjectSpawnerComponent spawnerComp{};
                
                const Simulation::BaseSpawner* base = nullptr;
                std::string geometry = "Sphere";
                
                // Handle Union types manually since we're iterating parallel vectors
                if (type == Simulation::SpawnerType_SphereSpawner) {
                    auto s = static_cast<const Simulation::SphereSpawner*>(data);
                    base = s->base();
                    geometry = "Sphere";
                    if (s->radius_range()) {
                        spawnerComp.randomizeScale = true;
                        spawnerComp.scaleMin = glm::vec3(s->radius_range()->min() * 2.0f);
                        spawnerComp.scaleMax = glm::vec3(s->radius_range()->max() * 2.0f);
                        spawnerComp.spawnObjectScale = 1.0f;
                    }
                } else if (type == Simulation::SpawnerType_CuboidSpawner) {
                    auto s = static_cast<const Simulation::CuboidSpawner*>(data);
                    base = s->base();
                    geometry = "Cube";
                    if (s->size_range()) {
                        spawnerComp.randomizeScale = true;
                        spawnerComp.scaleMin = SafeGetVec3(s->size_range()->min());
                        spawnerComp.scaleMax = SafeGetVec3(s->size_range()->max());
                        spawnerComp.spawnScale = glm::vec3(1.0f);
                    }
                } else if (type == Simulation::SpawnerType_CylinderSpawner) {
                    auto s = static_cast<const Simulation::CylinderSpawner*>(data);
                    base = s->base();
                    geometry = "Cylinder";
                    if (s->radius_range() || s->height_range()) {
                        spawnerComp.randomizeScale = true;
                        float minR = s->radius_range() ? s->radius_range()->min() : 0.5f;
                        float maxR = s->radius_range() ? s->radius_range()->max() : 0.5f;
                        float minH = s->height_range() ? s->height_range()->min() : 1.0f;
                        float maxH = s->height_range() ? s->height_range()->max() : 1.0f;
                        spawnerComp.scaleMin = glm::vec3(minR * 2.0f, minH, minR * 2.0f);
                        spawnerComp.scaleMax = glm::vec3(maxR * 2.0f, maxH, maxR * 2.0f);
                        spawnerComp.spawnScale = glm::vec3(1.0f);
                    }
                } else if (type == Simulation::SpawnerType_CapsuleSpawner) {
                    auto s = static_cast<const Simulation::CapsuleSpawner*>(data);
                    base = s->base();
                    geometry = "Capsule";
                    if (s->radius_range() || s->height_range()) {
                        spawnerComp.randomizeScale = true;
                        float minR = s->radius_range() ? s->radius_range()->min() : 0.2f;
                        float maxR = s->radius_range() ? s->radius_range()->max() : 0.2f;
                        float minH = s->height_range() ? s->height_range()->min() : 1.6f;
                        float maxH = s->height_range() ? s->height_range()->max() : 1.6f;
                        // For capsules, base proportions are radius 0.2, height 1.0.
                        // We map the absolute radius/height to these multipliers.
                        spawnerComp.scaleMin = glm::vec3(minR / 0.2f, minH, minR / 0.2f);
                        spawnerComp.scaleMax = glm::vec3(maxR / 0.2f, maxH, maxR / 0.2f);
                        spawnerComp.spawnScale = glm::vec3(1.0f);
                    }
                }
                
                if (base) {
                    spawnerComp.spawnGeometryType = geometry;
                    spawnerComp.spawnTexturePath = "textures/default.jpg";
                    
                    // Location
                    TransformComponent trans{};
                    if (base->location_type() == Simulation::SpawnLocation_FixedLocation) {
                        auto loc = base->location_as_FixedLocation();
                        if (loc && loc->transform()) {
                            trans.position = SafeGetVec3(loc->transform()->position());
                            trans.rotation = SafeGetEuler(loc->transform()->orientation());
                        }
                    } else if (base->location_type() == Simulation::SpawnLocation_RandomBox) {
                        auto box = base->location_as_RandomBox();
                        if (box) {
                            spawnerComp.randomizePosition = true;
                            spawnerComp.randomPosMin = SafeGetVec3(box->min());
                            spawnerComp.randomPosMax = SafeGetVec3(box->max());
                            trans.position = glm::vec3(0.0f);
                        }
                    } else if (base->location_type() == Simulation::SpawnLocation_RandomSphere) {
                        auto sph = base->location_as_RandomSphere();
                        if (sph) {
                            trans.position = SafeGetVec3(sph->center());
                        }
                    }
                    trans.UpdateMatrix();
                    scene.GetRegistry().AddComponent<TransformComponent>(spawnerEntity, trans);
                    
                    // Velocity Ranges
                    if (base->linear_velocity()) {
                        spawnerComp.randomizeVelocity = true;
                        spawnerComp.velocityMin = SafeGetVec3(base->linear_velocity()->min());
                        spawnerComp.velocityMax = SafeGetVec3(base->linear_velocity()->max());
                        spawnerComp.spawnVelocity = glm::vec3(0.0f);
                    }
                    
                    if (base->angular_velocity()) {
                        spawnerComp.randomizeAngularVelocity = true;
                        spawnerComp.angularVelocityMin = SafeGetVec3(base->angular_velocity()->min());
                        spawnerComp.angularVelocityMax = SafeGetVec3(base->angular_velocity()->max());
                        spawnerComp.spawnAngularVelocity = glm::vec3(0.0f);
                    }
                    
                    // Spawn logic
                    if (base->spawn_type_type() == Simulation::SpawnType_RepeatingSpawn) {
                        auto rep = base->spawn_type_as_RepeatingSpawn();
                        spawnerComp.spawnInterval = rep->interval();
                        spawnerComp.maxSpawnsPerRun = (int)rep->max_count();
                        spawnerComp.alwaysOn = (rep->max_count() <= 0);
                    } else if (base->spawn_type_type() == Simulation::SpawnType_SingleBurstSpawn) {
                        auto burst = base->spawn_type_as_SingleBurstSpawn();
                        spawnerComp.maxSpawnsPerRun = (int)burst->count();
                        spawnerComp.alwaysOn = false;
                        spawnerComp.spawnInterval = 0.001f; // Burst
                    }
                    
                    // Assign spawner ownership (ONE, TWO, THREE, FOUR, or SEQUENTIAL)
                    if (base->owner() == Simulation::SpawnerOwnerType_SEQUENTIAL) {
                        spawnerComp.assignedOwner = 255; // Use 255 as a marker for SEQUENTIAL
                        spawnerComp.nextSequentialOwner = 0; // Start with player 1
                    } else {
                        // Map SpawnerOwnerType to owner index (ONE=0, TWO=1, THREE=2, FOUR=3)
                        spawnerComp.assignedOwner = std::min((uint8_t)base->owner(), (uint8_t)3);
                    }
                    
                    spawnerComp.isRunning = true;
                    scene.GetRegistry().AddComponent<ObjectSpawnerComponent>(spawnerEntity, spawnerComp);
                    
                    if (base->name()) {
                        scene.GetRegistry().AddComponent<NameComponent>(spawnerEntity, { base->name()->str() });
                    }
                }
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
