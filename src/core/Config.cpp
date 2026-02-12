#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

AppConfig ConfigLoader::Load(const std::string& filepath) {
    AppConfig config;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        std::cerr << "Could not open config file: " << filepath << ". Using defaults." << std::endl;
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string key;
        ss >> key;

        if (key == "WindowSize") {
            ss >> config.windowWidth >> config.windowHeight;
        }
        else if (key == "TimeParams") {
            ss >> config.time.dayLengthSeconds >> config.time.daysPerSeason;
        }
        else if (key == "SeasonTemps") {
            ss >> config.seasons.summerBaseTemp >> config.seasons.winterBaseTemp >> config.seasons.dayNightTempDiff;
        }
        else if (key == "WeatherIntervals") {
            ss >> config.weather.minClearInterval >> config.weather.maxClearInterval;
        }
        else if (key == "WeatherDuration") {
            ss >> config.weather.minPrecipitationDuration >> config.weather.maxPrecipitationDuration;
        }
        else if (key == "FireSuppression") {
            ss >> config.weather.fireSuppressionDuration;
        }
        else if (key == "SunOrbit") {
            ss >> config.sunOrbit.directionDegrees >> config.sunOrbit.radius >> config.sunOrbit.initialAngle;
        }
        else if (key == "MoonOrbit") {
            ss >> config.moonOrbit.directionDegrees >> config.moonOrbit.radius >> config.moonOrbit.initialAngle;
        }
        else if (key == "SunHeatBonus") {
            ss >> config.sunHeatBonus;
        }
        else if (key == "TerrainParams") {
            ss >> config.terrainHeightScale >> config.terrainNoiseFreq;
        }
        else if (key == "ProceduralObjectCount") {
            ss >> config.proceduralObjectCount;
        }
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
        else if (key == "StaticObject") {
            StaticObjectConfig obj;
            std::string flammableStr;
            ss >> obj.name >> obj.modelPath >> obj.texturePath
                >> obj.position.x >> obj.position.y >> obj.position.z
                >> obj.rotation.x >> obj.rotation.y >> obj.rotation.z
                >> obj.scale.x >> obj.scale.y >> obj.scale.z
                >> flammableStr;
            obj.isFlammable = (flammableStr == "1" || flammableStr == "true");
            config.staticObjects.push_back(obj);
        }
        // --- Custom Camera Parsing ---
        else if (key == "CustomCamera") {
            // Format: CustomCamera Name Type PosX PosY PosZ TargetX TargetY TargetZ
            CustomCameraConfig cam;
            ss >> cam.name >> cam.type
                >> cam.position.x >> cam.position.y >> cam.position.z
                >> cam.target.x >> cam.target.y >> cam.target.z;
            config.customCameras.push_back(cam);
        }
    }

    return config;
}