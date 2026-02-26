#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

AppConfig ConfigLoader::Load(const std::string& filepath) {
    AppConfig config;
    ParseFile(config, filepath);
    return config;
}

std::vector<SceneOption> ConfigLoader::GetAvailableScenes(const std::string& rootDir) {
    std::vector<SceneOption> scenes;

    try {
        if (fs::exists(rootDir) && fs::is_directory(rootDir)) {
            for (const auto& entry : fs::directory_iterator(rootDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".world") {
                    std::string name = entry.path().stem().string();
                    scenes.push_back({ name, entry.path().string() });
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "ConfigLoader Error: " << e.what() << std::endl;
    }

    return scenes;
}

enum class ConfigSection { None, Settings, Scene, Input };

void ConfigLoader::ParseFile(AppConfig& config, const std::string& filepath) {
    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        std::cerr << "Error: Config file not found: " << filepath << std::endl;
        return;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to open config file: " << filepath << std::endl;
        return;
    }

    std::string line;
    SceneObjectConfig* currentObject = nullptr;
    ProceduralTextureConfig* currentTexture = nullptr;
    CustomParticleConfig* currentParticle = nullptr;
    ConfigSection currentSection = ConfigSection::None;

    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));

        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line == "[Settings]") { currentSection = ConfigSection::Settings; continue; }
        if (line == "[Scene]") { currentSection = ConfigSection::Scene; continue; }
        if (line == "[Input]") { currentSection = ConfigSection::Input; continue; }

        if (currentSection == ConfigSection::Input) {
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string actionStr = line.substr(0, equalPos);
                std::string keysStr = line.substr(equalPos + 1);

                actionStr.erase(actionStr.find_last_not_of(" \t") + 1);
                size_t keyStart = keysStr.find_first_not_of(" \t");
                if (keyStart != std::string::npos) keysStr = keysStr.substr(keyStart);

                config.inputBindings[actionStr] = keysStr;
            }
            continue;
        }

        std::stringstream ss(line);
        std::string key;
        ss >> key;

        // --- Object / Texture / Particle Blocks ---
        if (key == "Object") {
            SceneObjectConfig newObj;
            ss >> newObj.name;
            config.sceneObjects.push_back(newObj);
            currentObject = &config.sceneObjects.back();
            currentTexture = nullptr;
            currentParticle = nullptr;
        }
        else if (key == "EndObject") {
            currentObject = nullptr;
        }
        else if (key == "ProceduralTexture") {
            ProceduralTextureConfig newTex;
            ss >> newTex.name;
            config.proceduralTextures.push_back(newTex);
            currentTexture = &config.proceduralTextures.back();
            currentObject = nullptr;
            currentParticle = nullptr;
        }
        else if (key == "EndTexture") {
            currentTexture = nullptr;
        }
        else if (key == "CustomParticle") {
            CustomParticleConfig p;
            ss >> p.name >> p.texturePath >> p.rate >> p.lifeTime;
            std::string addStr;
            if (!ss.eof()) ss >> addStr;
            p.isAdditive = (addStr == "1" || addStr == "true");
            config.customParticles.push_back(p);
            currentParticle = &config.customParticles.back();
            currentObject = nullptr;
            currentTexture = nullptr;
        }
        else if (key == "EndParticle") {
            currentParticle = nullptr;
        }

        // --- Particle Fields ---
        else if (currentParticle) {
            if (key == "PosVar") ss >> currentParticle->posVar.x >> currentParticle->posVar.y >> currentParticle->posVar.z;
            else if (key == "Velocity") ss >> currentParticle->vel.x >> currentParticle->vel.y >> currentParticle->vel.z;
            else if (key == "VelVar") ss >> currentParticle->velVar.x >> currentParticle->velVar.y >> currentParticle->velVar.z;
            else if (key == "ColorBegin") ss >> currentParticle->colorBegin.r >> currentParticle->colorBegin.g >> currentParticle->colorBegin.b >> currentParticle->colorBegin.a;
            else if (key == "ColorEnd") ss >> currentParticle->colorEnd.r >> currentParticle->colorEnd.g >> currentParticle->colorEnd.b >> currentParticle->colorEnd.a;
            else if (key == "Size") ss >> currentParticle->size.x >> currentParticle->size.y >> currentParticle->size.z;
        }
        // --- Texture Fields ---
        else if (currentTexture) {
            if (key == "Type") ss >> currentTexture->type;
            else if (key == "Color1") ss >> currentTexture->color1.r >> currentTexture->color1.g >> currentTexture->color1.b >> currentTexture->color1.a;
            else if (key == "Color2") ss >> currentTexture->color2.r >> currentTexture->color2.g >> currentTexture->color2.b >> currentTexture->color2.a;
            else if (key == "Size") {
                ss >> currentTexture->width;
                currentTexture->height = currentTexture->width;
                if (!ss.eof()) ss >> currentTexture->height;
            }
            else if (key == "CellSize") ss >> currentTexture->cellSize;
            else if (key == "Vertical") {
                std::string boolStr;
                ss >> boolStr;
                currentTexture->isVertical = (boolStr == "true" || boolStr == "1");
            }
        }
        // --- Object Fields ---
        else if (currentObject) {
            if (key == "Type") ss >> currentObject->type;
            else if (key == "Model") ss >> currentObject->modelPath;
            else if (key == "Texture") ss >> currentObject->texturePath;
            else if (key == "Position") ss >> currentObject->position.x >> currentObject->position.y >> currentObject->position.z;
            else if (key == "Rotation") ss >> currentObject->rotation.x >> currentObject->rotation.y >> currentObject->rotation.z;
            else if (key == "Scale") ss >> currentObject->scale.x >> currentObject->scale.y >> currentObject->scale.z;
            else if (key == "Params") ss >> currentObject->params.x >> currentObject->params.y >> currentObject->params.z;
            else if (key == "AttachParticle") {
                AttachedParticleConfig ap;
                std::string durStr;
                ss >> ap.particleName >> durStr;
                ap.duration = (durStr == "inf" || durStr == "-1") ? -1.0f : std::stof(durStr);
                currentObject->attachedParticles.push_back(ap);
            }
            else if (key == "RenderProps") {
                std::string castS, recvS, vis;
                ss >> currentObject->shadingMode >> castS >> recvS >> vis >> currentObject->layerMask;
                currentObject->castsShadow = (castS == "true" || castS == "1");
                currentObject->receiveShadows = (recvS == "true" || recvS == "1");
                currentObject->visible = (vis == "true" || vis == "1");
            }
            else if (key == "PhysicsProps") {
                std::string flamStr, colStr, staticStr;

                // Read the original two parameters
                ss >> flamStr >> colStr;
                currentObject->isFlammable = (flamStr == "true" || flamStr == "1");
                currentObject->hasCollision = (colStr == "true" || colStr == "1");

                // Backwards Compatibility: Only try to read isStatic if it exists
                if (!ss.eof()) {
                    ss >> staticStr;
                    currentObject->isStatic = (staticStr == "true" || staticStr == "1");
                }
            }
            else if (key == "ColliderProps") {
                ss >> currentObject->colliderType;
                if (currentObject->colliderType == 1) { // Plane
                    ss >> currentObject->colliderNormal.x >> currentObject->colliderNormal.y >> currentObject->colliderNormal.z;

                    if (!ss.eof()) {
                        ss >> currentObject->colliderRadius;
                    }
                    else {
                        currentObject->colliderRadius = 0.0f; // 0 = Infinite
                    }
                }
                else { // Sphere
                    ss >> currentObject->colliderRadius;
                }
            }
            else if (key == "Orbit") {
                std::string orbitStr;
                ss >> orbitStr;
                currentObject->hasOrbit = (orbitStr == "true" || orbitStr == "1");
                if (currentObject->hasOrbit) {
                    ss >> currentObject->orbitRadius >> currentObject->orbitSpeed >> currentObject->orbitDirection >> currentObject->orbitInitialAngle;
                }
            }
            else if (key == "Light") {
                std::string lightStr;
                ss >> lightStr;
                currentObject->isLight = (lightStr == "true" || lightStr == "1");
                if (currentObject->isLight) {
                    ss >> currentObject->lightColor.x >> currentObject->lightColor.y >> currentObject->lightColor.z >> currentObject->lightIntensity >> currentObject->lightType;
                }
            }
        }
        // --- Global Settings ---
        else if (key == "WindowSize") ss >> config.windowWidth >> config.windowHeight;
        else if (key == "TimeParams") ss >> config.time.dayLengthSeconds >> config.time.daysPerSeason;
        else if (key == "SeasonTemps") ss >> config.seasons.summerBaseTemp >> config.seasons.winterBaseTemp >> config.seasons.dayNightTempDiff;
        else if (key == "WeatherIntervals") ss >> config.weather.minClearInterval >> config.weather.maxClearInterval;
        else if (key == "WeatherDuration") ss >> config.weather.minPrecipitationDuration >> config.weather.maxPrecipitationDuration;
        else if (key == "FireSuppression") ss >> config.weather.fireSuppressionDuration;
        else if (key == "SunHeatBonus") ss >> config.sunHeatBonus;
        else if (key == "ProceduralObjectCount") ss >> config.proceduralObjectCount;
        else if (key == "ProceduralPlant") {
            ProceduralPlantConfig plant;
            std::string flammableStr;
            ss >> plant.modelPath >> plant.texturePath >> plant.frequency
                >> plant.minScale.x >> plant.minScale.y >> plant.minScale.z
                >> plant.maxScale.x >> plant.maxScale.y >> plant.maxScale.z
                >> plant.baseRotation.x >> plant.baseRotation.y >> plant.baseRotation.z
                >> flammableStr;
            plant.isFlammable = (flammableStr == "1" || flammableStr == "true");
            config.proceduralPlants.push_back(plant);
        }
        else if (key == "Camera") {
            CustomCameraConfig cam;
            ss >> cam.name >> cam.type >> cam.actionBind >> cam.position.x >> cam.position.y >> cam.position.z;

            if (cam.type == "Orbit") {
                ss >> cam.orbitRadius >> cam.target.x >> cam.target.y >> cam.target.z;
            }
            else if (cam.type == "RandomTarget") {
                ss >> cam.orbitRadius >> cam.targetMatch;
            }
            else if (cam.type == "FreeRoam") {
                // --- ADD THIS: Read Yaw and Pitch if they exist ---
                if (!ss.eof()) ss >> cam.yaw;
                if (!ss.eof()) ss >> cam.pitch;
            }

            config.customCameras.push_back(cam);
            }
    }
}