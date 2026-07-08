# VulkanPhysics

A Vulkan-based ECS sandbox for real-time physics simulation, rendering, networking experiments, and in-editor tooling.

## Overview

VulkanPhysics combines:
- A custom **Entity Component System (ECS)** runtime
- A **Vulkan + GLFW + ImGui** rendering/editor stack
- A modular **physics pipeline** (rigid body, cloth, springs, collision responses)
- Optional **distributed simulation/network synchronization**
- A built-in **lookahead/replay timeline editor**

The solution includes three main projects:
- `VulkanPhysics` (main app)
- `SimulationStaticLib` (static physics/collision library)
- `CPP-GoogleTest` (GTest validation suite)

---

## Core Features

### Physics & Simulation
- Multiple integration modes: **Explicit Euler**, **Semi-Implicit Euler**, **RK4**
- Configurable substepping and time scaling
- Collision detection/resolution for:
  - Sphere, Plane, Capsule, Cylinder, AABB/Cube
  - Sphere-plane, sphere-sphere, sphere-capsule, capsule-capsule, box-capsule, box-box
- Runtime-tunable physics parameters:
  - Gravity direction/strength
  - Linear damping
  - Quadratic drag
  - Sleep thresholds
  - Friction scaling
- Cloth simulation with spring constraints and collision toggles
- Spring systems with dynamic entity anchoring and live connection editing

### Rendering & Graphics
- Vulkan renderer with custom pipeline/shader setup
- Dynamic geometry support:
  - Primitive generation (cube, sphere, plane, capsule, cylinder, bowl, disk, grid, terrain)
  - Model loading (`.obj`, `.sjg`)
- Multiple shading modes (including wireframe)
- Shadow support and simple shadow fallback
- Skybox, particles, textured materials, and procedural texture generation

### Environment & World Systems
- Day/night and seasonal controls
- Weather toggles (rain/snow), dust cloud effects
- Thermodynamics interactions (e.g., ignite/extinguish flows)
- Scene loading from world files (`src/worlds/*.world`, `*.bin`)

### Systems Included
- `PhysicsSystem`
- `ClothSystem`
- `FlockSystem`
- `AnimationSystem`
- `ObjectSpawnerSystem`
- `ParticleUpdateSystem`
- `TimeSystem`
- `WeatherSystem`
- `ThermodynamicsSystem`
- `CameraSystem`
- `OrbitSystem`
- `SimpleShadowSystem`

---

## Replay & Lookahead Features

The editor includes a replay pipeline that can generate future-state snapshots and scrub/play them back:
- Lookahead generation for configurable time windows
- Timeline UI with:
  - First/prev/next/last frame controls
  - Play/pause loop
  - Playback speed scaling
  - Frame scrubbing slider
- Optional **Replay Free Roam** camera behavior
- Automatic frame capture of transform/physics/animation state

Replay controls live under the **Replay** menu and the **Replay Timeline** panel.

---

## Editor UI (Beyond Basic Object Editing)

The ImGui editor supports much more than simple object transforms.

### Main Menu Categories
- Global UI/performance/runtime controls
- Networking panel access and profiling windows
- Layer region manager (volume setup, visibility modes, per-layer assignment)
- Particles menu and emitter presets
- Camera switching and camera-linked spawner workflows
- Light management and scene lighting controls
- Spawner management and visualization helpers
- Simulation panel (pause/step/restart, runtime tuning, ownership overlays)
- Replay controls (lookahead generation and playback state)
- Animation controls (play/pause/rewind all, global playback speed)
- Environment controls (season/weather/time-of-day)
- Scene loading utilities
- Object menu with deep per-entity actions

### Property Window Capabilities
- Multiple pop-out property windows
- Entity creation/deletion and focus controls
- Component-level inspection/editing/removal
- Live editing for:
  - Render/material/texture/procedural texture
  - Geometry swapping
  - Physics/collider/spring/cloth
  - Camera and layer visibility masks
  - Ownership/network sync fields
  - Path animation data
  - Particle emitter attachment and tuning

### Dedicated Utility Windows
- Input Controls window
- Performance profiler window (including per-system timings)
- Runtime compliance/affinity window
- Networking configuration and live peer telemetry
- Spatial partitioning benchmark window (naive vs uniform grid vs octree)

---

## SimulationStaticLib (Static Physics Library)

`SimulationStaticLib/` is a reusable static library that isolates geometry and collision/physics helpers from the main runtime.

### Library Scope
- Collider primitives and math utilities:
  - `Sphere`, `Plane`, `Capsule`, `Cylinder`, `AABB`, `Raycast`
- Shared collision helpers and contact resolution logic
- Rigid body helpers for impulse/force workflows
- Energy/momentum helper utilities
- Angular integration and damping/drag support

This library is linked into both the main application and the test project.

---

## GSuite / GoogleTest Coverage

`CPP-GoogleTest/` provides a test suite covering core simulation and static-library behavior using GoogleTest.

### Test Areas
- Collider shape behavior
- Physics integration
- Angular velocity and orientation behavior
- Plane/sphere/capsule/cylinder/AABB checks
- Spring and collision helper validation

### Notes
- The project references `Microsoft.googletest.v140.windesktop.msvcstl.static.rt-dyn` via NuGet packages.
- Tests build as the `CPP-GoogleTest` project inside `VulkanPhysics.sln`.

---

## Networking & Distributed Simulation

The runtime includes networking components for multi-peer simulation experiments:
- Peer identity and ownership model
- Live peer telemetry (status, ping, packet loss)
- Latency/jitter/packet-loss simulation controls
- Interpolation buffer controls and runtime diagnostics
- Ownership-aware visual overlays in editor/runtime UI

---

## Repository Structure

```text
VulkanPhysics/
├── src/
│   ├── core/          # Application, ECS, config, input, scene loading
│   ├── geometry/      # Mesh generation/loaders
│   ├── menu/          # EditorUI menus/windows
│   ├── network/       # Network manager and schema
│   ├── rendering/     # Scene, renderer, particles, pipelines
│   ├── systems/       # Simulation/game systems
│   ├── vulkan/        # Vulkan wrappers/utilities
│   └── worlds/        # Scene/world definitions
├── SimulationStaticLib/
├── CPP-GoogleTest/
├── models/
├── textures/
└── Additional Libraries/
```

---

## Build Requirements

- Windows
- Visual Studio 2022 (v143 toolset)
- Vulkan SDK (with `glslc` available for shader compilation)
- Included/available dependencies in solution setup:
  - GLFW
  - GLM
  - ImGui
  - FlatBuffers
  - GoogleTest (NuGet package in test project)

---

## Build & Run

1. Open:
   - `VulkanPhysics.sln`
2. Select configuration:
   - `Debug|x64` or `Release|x64`
3. Build solution.
4. Run `VulkanPhysics` as startup project.

---

## Running Tests

1. Build `CPP-GoogleTest` project.
2. Run tests via Visual Studio Test Explorer (or run the built test executable).

---

## Scene Content

Example world files are in:
- `src/worlds/`

These include demos for cloth, flocking, collision tests, spawners, and other simulation scenarios.

---

## Notes

- This repo includes experimental/editor-first functionality and active feature iteration.
- Some systems are designed for runtime experimentation through the Editor UI rather than fixed scripted flows.
