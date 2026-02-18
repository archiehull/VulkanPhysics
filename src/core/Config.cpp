#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

AppConfig ConfigLoader::Load(const std::string& sceneDirectory) {
    AppConfig config;

    // Ensure directory path ends with a slash
    std::string dir = sceneDirectory;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += "/";
    }

    // Load Scene-specific settings (Time, Weather, Global vars)
    ParseFile(config, dir + "settings.cfg");

    // Load Scene Objects and Procedural rules
    ParseFile(config, dir + "scene.cfg");

    return config;
}

void ConfigLoader::ParseFile(AppConfig& config, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open config file: " << filepath << std::endl;
        return;
    }

    std::string line;
    SceneObjectConfig* currentObject = nullptr;

    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
            return !std::isspace(ch);
            }));

        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string key;
        ss >> key;

        // --- Object Block Parsing ---
        if (key == "Object") {
            SceneObjectConfig newObj;
            ss >> newObj.name;
            config.sceneObjects.push_back(newObj);
            currentObject = &config.sceneObjects.back();
        }
        else if (key == "EndObject") {
            currentObject = nullptr;
        }
        else if (currentObject) {
            if (key == "Type") ss >> currentObject->type;
            else if (key == "Model") ss >> currentObject->modelPath;
            else if (key == "Texture") ss >> currentObject->texturePath;
            else if (key == "Position") ss >> currentObject->position.x >> currentObject->position.y >> currentObject->position.z;
            else if (key == "Rotation") ss >> currentObject->rotation.x >> currentObject->rotation.y >> currentObject->rotation.z;
            else if (key == "Scale") ss >> currentObject->scale.x >> currentObject->scale.y >> currentObject->scale.z;
            else if (key == "Params") ss >> currentObject->params.x >> currentObject->params.y >> currentObject->params.z;
            else if (key == "RenderProps") {
                std::string castS, recvS, vis;
                ss >> currentObject->shadingMode >> castS >> recvS >> vis >> currentObject->layerMask;
                currentObject->castsShadow = (castS == "true" || castS == "1");
                currentObject->receiveShadows = (recvS == "true" || recvS == "1");
                currentObject->visible = (vis == "true" || vis == "1");
            }
            else if (key == "PhysicsProps") {
                std::string flamStr, colStr;
                ss >> flamStr >> colStr;
                currentObject->isFlammable = (flamStr == "true" || flamStr == "1");
                currentObject->hasCollision = (colStr == "true" || colStr == "1");
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
        // --- Global Config Parsing ---
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
        else if (key == "CustomCamera") {
            CustomCameraConfig cam;
            ss >> cam.name >> cam.type
                >> cam.position.x >> cam.position.y >> cam.position.z
                >> cam.target.x >> cam.target.y >> cam.target.z;
            config.customCameras.push_back(cam);
        }
    }
}