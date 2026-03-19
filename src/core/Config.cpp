#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cctype>

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

static bool ParseBoolToken(const std::string& value) {
    return value == "true" || value == "1";
}

static int ParseFlickerPresetToken(std::string preset) {
    std::transform(preset.begin(), preset.end(), preset.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (preset == "fire") return 1;
    if (preset == "candle") return 2;
    if (preset == "faulty") return 3;
    if (preset == "pulse") return 4;
    return 0;
}

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
    LayerRegionConfig* currentLayerRegion = nullptr;
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

        else if (key == "LayerRegion") {
            LayerRegionConfig newRegion;
            ss >> newRegion.name;
            config.layerRegions.push_back(newRegion);
            currentLayerRegion = &config.layerRegions.back();
            currentObject = nullptr;
            currentTexture = nullptr;
            currentParticle = nullptr;
        }
        else if (key == "EndLayerRegion") {
            currentLayerRegion = nullptr;
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
            else if (key == "LayerMask") ss >> currentObject->layerMask;
            else if (key == "AttachParticle") {
                AttachedParticleConfig ap;
                std::string durStr;
                ss >> ap.particleName >> durStr;
                ap.duration = (durStr == "inf" || durStr == "-1") ? -1.0f : std::stof(durStr);
                currentObject->attachedParticles.push_back(ap);
            }
            else if (key == "OnlyInRegionMask") ss >> currentObject->onlyInRegionMask;
            else if (key == "RenderProps") {
                std::vector<std::string> tokens;
                std::string tok;
                while (ss >> tok) tokens.push_back(tok);

                auto toBool = [](const std::string& s) {
                    return s == "true" || s == "1";
                    };

                if (tokens.size() >= 4) {
                    currentObject->shadingMode = std::stoi(tokens[0]);
                    currentObject->castsShadow = toBool(tokens[1]);
                    currentObject->receiveShadows = toBool(tokens[2]);
                    currentObject->visible = toBool(tokens[3]);
                }
                if (tokens.size() >= 5) currentObject->layerMask = std::stoi(tokens[4]);
                if (tokens.size() >= 6) currentObject->onlyInRegionMask = std::stoi(tokens[5]);
            }
            else if (key == "PhysicsProps") {
                currentObject->hasPhysicsConfig = true;
                std::vector<std::string> tokens;
                std::string tok;
                while (ss >> tok) tokens.push_back(tok);

                if (tokens.size() >= 2) {
                    currentObject->isFlammable = (tokens[0] == "true" || tokens[0] == "1");
                    currentObject->hasCollision = (tokens[1] == "true" || tokens[1] == "1");
                }

                auto isBoolToken = [](const std::string& s) {
                    return s == "true" || s == "false" || s == "1" || s == "0";
                    };

                size_t idx = 2;

                // Optional isStatic (legacy-compatible)
                if (idx < tokens.size() && isBoolToken(tokens[idx])) {
                    currentObject->isStatic = (tokens[idx] == "true" || tokens[idx] == "1");
                    ++idx;
                }

                // Optional mass, restitution, friction
                if (idx < tokens.size()) currentObject->mass = std::stof(tokens[idx++]);
                if (idx < tokens.size()) currentObject->restitution = std::stof(tokens[idx++]);
                if (idx < tokens.size()) currentObject->friction = std::stof(tokens[idx++]);
            }
            else if (key == "ColliderProps") {
                currentObject->hasColliderConfig = true;
                ss >> currentObject->colliderType;
                if (currentObject->colliderType == 1) { // Plane
                    ss >> currentObject->colliderNormal.x >> currentObject->colliderNormal.y >> currentObject->colliderNormal.z;

                    if (!ss.eof()) ss >> currentObject->colliderRadius;
                    else currentObject->colliderRadius = 0.0f; // 0 = Infinite

                    if (!ss.eof()) ss >> currentObject->colliderHeight;
                    else currentObject->colliderHeight = currentObject->colliderRadius;
                }
                else { // Sphere
                    ss >> currentObject->colliderRadius;
                }
            }
            else if (key == "Orbit") {
                currentObject->hasOrbitConfig = true;
                std::string orbitStr;
                ss >> orbitStr;
                currentObject->hasOrbit = (orbitStr == "true" || orbitStr == "1");
                if (currentObject->hasOrbit) {
                    ss >> currentObject->orbitRadius >> currentObject->orbitSpeed >> currentObject->orbitDirection >> currentObject->orbitInitialAngle;
                }
            }
            else if (key == "Thermo") {
                std::string thermoStr;
                ss >> thermoStr;
                currentObject->thermoEnabled = ParseBoolToken(thermoStr);
                currentObject->hasThermoOverride = true;
            }
            else if (key == "Light") {
                std::string lightStr;
                ss >> lightStr;
                currentObject->isLight = ParseBoolToken(lightStr);
                if (currentObject->isLight) {
                    ss >> currentObject->lightColor.x >> currentObject->lightColor.y >> currentObject->lightColor.z >> currentObject->lightIntensity >> currentObject->lightType;

                    // Optional inline flicker fields: enabled amount preset
                    std::string flickerEnabledStr;
                    if (ss >> flickerEnabledStr) {
                        currentObject->lightFlickerEnabled = ParseBoolToken(flickerEnabledStr);

                        if (ss >> currentObject->lightFlickerAmount) {
                            std::string flickerPresetStr;
                            if (ss >> flickerPresetStr) {
                                currentObject->lightFlickerPreset = ParseFlickerPresetToken(flickerPresetStr);
                            }
                        }
                        currentObject->hasExplicitLightFlicker = true;
                    }
                }
            }
            else if (key == "LightFlicker") {
                std::string enabledStr;
                ss >> enabledStr >> currentObject->lightFlickerAmount;

                currentObject->lightFlickerEnabled = ParseBoolToken(enabledStr);

                std::string presetStr;
                if (ss >> presetStr) {
                    currentObject->lightFlickerPreset = ParseFlickerPresetToken(presetStr);
                }

                currentObject->hasExplicitLightFlicker = true;
            }
            else if (key == "DustCloudProps") {
                std::string activeStr;
                ss >> activeStr
                    >> currentObject->direction.x >> currentObject->direction.y >> currentObject->direction.z
                    >> currentObject->speed;
                currentObject->isActive = (activeStr == "true" || activeStr == "1");
            }
			// --- Environment Properties ---
            else if (key == "TimeParams") ss >> currentObject->timeConfig.dayLengthSeconds >> currentObject->timeConfig.daysPerSeason;
            else if (key == "SeasonTemps") ss >> currentObject->seasonConfig.summerBaseTemp >> currentObject->seasonConfig.winterBaseTemp >> currentObject->seasonConfig.dayNightTempDiff;
            else if (key == "WeatherIntervals") ss >> currentObject->weatherConfig.minClearInterval >> currentObject->weatherConfig.maxClearInterval;
            else if (key == "WeatherDuration") ss >> currentObject->weatherConfig.minPrecipitationDuration >> currentObject->weatherConfig.maxPrecipitationDuration;
            else if (key == "FireSuppression") ss >> currentObject->weatherConfig.fireSuppressionDuration;
            else if (key == "SunHeatBonus") ss >> currentObject->sunHeatBonus;
            else if (key == "ThermoPolicy") {
                std::string mode;
                ss >> mode;
                std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (mode == "blacklist") currentObject->thermoPolicyMode = 1;
                else if (mode == "whitelist") currentObject->thermoPolicyMode = 2;
                else currentObject->thermoPolicyMode = 0;
            }
            else if (key == "ThermoEntities") {
                currentObject->thermoPolicyEntities.clear();
                std::string entityName;
                while (ss >> entityName) {
                    currentObject->thermoPolicyEntities.push_back(entityName);
                }
            }
            else if (key == "DustCloudProps") {
                std::string activeStr;
                ss >> activeStr >> currentObject->direction.x >> currentObject->direction.y >> currentObject->direction.z >> currentObject->speed;
                currentObject->isActive = (activeStr == "true" || activeStr == "1");
            }
            else if (key == "SpawnerEnabled") {
                std::string value;
                ss >> value;
                currentObject->spawnerEnabled = (value == "true" || value == "1");
            }
            else if (key == "SpawnerAlwaysOn") {
                std::string value;
                ss >> value;
                currentObject->spawnerEnabled = (value == "true" || value == "1");
            }
            else if (key == "SpawnInterval") ss >> currentObject->spawnInterval;
            else if (key == "SpawnerRunDuration") ss >> currentObject->spawnerRunDurationSeconds;
            else if (key == "SpawnerMaxSpawns") ss >> currentObject->spawnerMaxSpawnsPerRun;
            else if (key == "SpawnGeometry") ss >> currentObject->spawnGeometryType;
            else if (key == "SpawnModel") ss >> currentObject->spawnModelPath;
            else if (key == "SpawnTexture") ss >> currentObject->spawnTexturePath;
            else if (key == "SpawnScale") ss >> currentObject->spawnScale.x >> currentObject->spawnScale.y >> currentObject->spawnScale.z;
            else if (key == "SpawnVelocity") ss >> currentObject->spawnVelocity.x >> currentObject->spawnVelocity.y >> currentObject->spawnVelocity.z;
            else if (key == "RandomSpawnVelocity") {
                std::string value;
                ss >> value;
                currentObject->randomizeSpawnVelocity = (value == "true" || value == "1");
            }
            else if (key == "SpawnVelocityRandomRange") {
                ss >> currentObject->spawnVelocityRandomRange.x >> currentObject->spawnVelocityRandomRange.y >> currentObject->spawnVelocityRandomRange.z;
            }
            else if (key == "SpawnAngularVelocity") {
                ss >> currentObject->spawnAngularVelocity.x >> currentObject->spawnAngularVelocity.y >> currentObject->spawnAngularVelocity.z;
            }
            else if (key == "RandomSpawnAngularVelocity") {
                std::string value;
                ss >> value;
                currentObject->randomizeSpawnAngularVelocity = (value == "true" || value == "1");
            }
            else if (key == "SpawnAngularVelocityRandomRange") {
                ss >> currentObject->spawnAngularVelocityRandomRange.x >> currentObject->spawnAngularVelocityRandomRange.y >> currentObject->spawnAngularVelocityRandomRange.z;
            }
            else if (key == "SpawnMass") ss >> currentObject->spawnMass;
            else if (key == "Despawner") {
                std::string value;
                ss >> value;
                currentObject->isDespawner = (value == "true" || value == "1");
            }
        }
        // --- Global Settings ---
        else if (key == "WindowSize") ss >> config.windowWidth >> config.windowHeight;
        else if (key == "VSync") {
            std::string value;
            ss >> value;
            config.vsync = ParseBoolToken(value);
        }
        else if (key == "MaxFPS") {
            ss >> config.maxFps;
            if (config.maxFps < 0) {
                config.maxFps = 0;
            }
        }
        else if (key == "EnableDefaultDeathWall") {
            std::string value;
            ss >> value;
            config.enableDefaultDeathWall = (value == "true" || value == "1");
        }
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
        // --- Layer Region Fields ---
        else if (currentLayerRegion) {
            if (key == "LayerBit") ss >> currentLayerRegion->assignedLayerBit;
            else if (key == "VolumeType") ss >> currentLayerRegion->volumeType;
            else if (key == "Radius") ss >> currentLayerRegion->radius;
            else if (key == "HalfExtents") ss >> currentLayerRegion->halfExtents.x >> currentLayerRegion->halfExtents.y >> currentLayerRegion->halfExtents.z;
            else if (key == "Position") ss >> currentLayerRegion->position.x >> currentLayerRegion->position.y >> currentLayerRegion->position.z;
        }
    }
}