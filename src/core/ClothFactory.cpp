#include "ClothFactory.h"
#include "Components.h"
#include "../geometry/Geometry.h"

Entity ClothFactory::CreateClothGrid(Scene& scene, VkDevice device, VkPhysicalDevice physicalDevice, const glm::vec3& position, int width, int height, float spacing, float mass, float stiffness, float damping, const std::string& texturePath) {
    auto& registry = scene.GetRegistry();
    
    Entity clothEntity = registry.CreateEntity();
    ClothComponent clothComp;
    clothComp.width = width;
    clothComp.height = height;
    clothComp.spacing = spacing;
    clothComp.dynamicGeometry = std::make_shared<Geometry>(device, physicalDevice);
    
    // Create the particles
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Entity p = registry.CreateEntity();
            clothComp.particles.push_back(p);
            
            TransformComponent tc;
            tc.position = position + glm::vec3(x * spacing, -y * spacing, 0.0f);
            tc.UpdateMatrix();
            registry.AddComponent(p, tc);
            
            PhysicsComponent pc;
            if (y == 0) { // Pin top row
                pc.SetMass(0.0f);
                pc.isStatic = true;
            } else {
                pc.SetMass(mass);
                pc.isStatic = false;
            }
            registry.AddComponent(p, pc);
            
            ColliderComponent cc;
            cc.type = 0; // Sphere
            cc.radius = spacing * 1.5f; // Overlap significantly to prevent tunneling
            cc.collisionLayer = 2; // Assign to cloth layer
            cc.collisionMask = ~2u; // Collide with everything EXCEPT other cloth particles
            cc.isClothParticle = true;
            registry.AddComponent(p, cc);
            
            Vertex v;
            v.pos = tc.position;
            v.color = glm::vec3(0.8f, 0.1f, 0.1f); // Red cloth
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            v.texCoord = glm::vec2((float)x / (width - 1), (float)y / (height - 1));
            clothComp.dynamicGeometry->AddVertex(v);
        }
    }
    
    // Indices for triangles
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            int i0 = y * width + x;
            int i1 = i0 + 1;
            int i2 = (y + 1) * width + x;
            int i3 = i2 + 1;
            
            clothComp.dynamicGeometry->AddIndex(i0);
            clothComp.dynamicGeometry->AddIndex(i2);
            clothComp.dynamicGeometry->AddIndex(i1);
            
            clothComp.dynamicGeometry->AddIndex(i1);
            clothComp.dynamicGeometry->AddIndex(i2);
            clothComp.dynamicGeometry->AddIndex(i3);
            
            // Add reverse faces for double-sided rendering
            clothComp.dynamicGeometry->AddIndex(i0);
            clothComp.dynamicGeometry->AddIndex(i1);
            clothComp.dynamicGeometry->AddIndex(i2);
            
            clothComp.dynamicGeometry->AddIndex(i1);
            clothComp.dynamicGeometry->AddIndex(i3);
            clothComp.dynamicGeometry->AddIndex(i2);
        }
    }
    
    clothComp.dynamicGeometry->CreateBuffers();
    
    // Setup springs
    auto getIndex = [&](int x, int y) { return y * width + x; };
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Entity p = clothComp.particles[getIndex(x, y)];
            SpringComponent sc;
            sc.stiffness = stiffness;
            sc.damping = damping;
            sc.isAttachedToEntity = true;
            
            // Structural (Right and Down)
            if (x < width - 1) {
                sc.connectedEntities.push_back(clothComp.particles[getIndex(x + 1, y)]);
                sc.restingLengths.push_back(spacing);
            }
            if (y < height - 1) {
                sc.connectedEntities.push_back(clothComp.particles[getIndex(x, y + 1)]);
                sc.restingLengths.push_back(spacing);
            }
            
            // Shear (Diagonals)
            if (x < width - 1 && y < height - 1) {
                sc.connectedEntities.push_back(clothComp.particles[getIndex(x + 1, y + 1)]);
                sc.restingLengths.push_back(spacing * 1.41421356f);
            }
            if (x > 0 && y < height - 1) {
                sc.connectedEntities.push_back(clothComp.particles[getIndex(x - 1, y + 1)]);
                sc.restingLengths.push_back(spacing * 1.41421356f);
            }
            
            // Bend (Skip 1, Right and Down)
            if (x < width - 2) {
                sc.connectedEntities.push_back(clothComp.particles[getIndex(x + 2, y)]);
                sc.restingLengths.push_back(spacing * 2.0f);
            }
            if (y < height - 2) {
                sc.connectedEntities.push_back(clothComp.particles[getIndex(x, y + 2)]);
                sc.restingLengths.push_back(spacing * 2.0f);
            }
            
            registry.AddComponent(p, sc);
        }
    }
    
    registry.AddComponent(clothEntity, clothComp);
    
    RenderComponent rc;
    rc.geometry = clothComp.dynamicGeometry;
    rc.geometryName = "ClothMesh";
    rc.texturePath = texturePath;
    rc.visible = true;
    rc.castsShadow = true;
    rc.receiveShadows = true;
    registry.AddComponent(clothEntity, rc);
    
    TransformComponent clothTc;
    clothTc.position = position;
    registry.AddComponent(clothEntity, clothTc);
    
    scene.RegisterRenderableEntity(clothEntity);
    
    return clothEntity;
}
