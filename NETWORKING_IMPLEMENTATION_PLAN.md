# Networking Implementation Plan

Goal: implement a 4-peer full-mesh P2P networking layer for VulkanPhysics that uses Winsock 2, survives high latency and packet loss, and keeps networking work pinned to CPU cores 2 and 3.

## Original Brief and Constraints

- Client-server is forbidden; use a full-mesh peer-to-peer topology for exactly 4 users.
- Use Winsock 2 for networking.
- Physics state should be sent over UDP.
- Reliable global UI actions, such as scene changes, should use TCP.
- Handle aggressive network conditions, including around 20% packet loss and high latency.
- Networking threads must be restricted to CPU cores 2 and 3.
- Use an ownership model so only the owning peer simulates an object.
- Non-owning peers should receive state updates and smooth them with interpolation or dead reckoning.
- Keep the implementation asynchronous and avoid blocking sockets.
- Use FlatBuffers for serialization.

## How to use this file

- Work top to bottom.
- Only move to the next section when the current one is stable.
- Tick off each checkbox as it is completed.
- Keep the implementation small and test each step before adding the next layer.

## 1. Confirm current engine touchpoints

- [ ] Locate the main networking entry points in `Application`.
- [ ] Confirm where scene loading and RPC broadcasting already happen.
- [ ] Confirm which thread currently owns simulation updates.
- [ ] Confirm which ECS component stores ownership.
- [ ] Confirm which FlatBuffers schema already represents object ownership and physics state.
- [ ] Write down the exact files that will be changed.

Acceptance criteria:
- You know where to connect networking without rewriting the whole engine.
- You know which parts are already present and which parts are missing.

## 2. Add a runtime networking schema

- [ ] Create a new FlatBuffers schema for runtime network messages.
- [ ] Add a UDP message for physics snapshots.
- [ ] Add a TCP message for reliable events.
- [ ] Include sender peer id in every runtime message.
- [ ] Include sequence numbers in every UDP snapshot.
- [ ] Include a timestamp or tick index in every snapshot.
- [ ] Include entity id, ownership, position, rotation, linear velocity, and angular velocity in the physics payload.
- [ ] Generate the FlatBuffers header and confirm it compiles.

Acceptance criteria:
- You can serialize and parse a physics packet without touching gameplay code.
- The schema supports both unreliable state sync and reliable UI events.

## 3. Build the NetworkManager skeleton

- [ ] Add a concrete `NetworkManager` implementation under `src/network`.
- [ ] Add startup and shutdown methods.
- [ ] Add `IsRunning`, peer count, and debug logging controls.
- [ ] Add a thread-safe queue for inbound events.
- [ ] Add a thread-safe queue for outbound commands if needed.
- [ ] Add placeholders for UDP send, UDP receive, TCP send, and TCP receive.
- [ ] Hook `Application` to construct and stop the network manager cleanly.

Acceptance criteria:
- The project builds with the new class in place.
- `Application` can start and stop networking without crashing.

## 4. Implement Winsock 2 initialization and sockets

- [ ] Call `WSAStartup` during network startup.
- [ ] Create a non-blocking UDP socket for physics replication.
- [ ] Create a TCP listener or TCP peer socket for reliable events.
- [ ] Bind the UDP socket to the configured local port.
- [ ] Bind the TCP socket to the configured local port.
- [ ] Put sockets into non-blocking mode.
- [ ] Add cleanup for sockets and `WSACleanup`.

Acceptance criteria:
- The app can open and close all sockets repeatedly.
- No network operation blocks the simulation thread.

## 5. Add CPU affinity for network threads

- [ ] Create a dedicated receive thread.
- [ ] Create a dedicated send thread.
- [ ] Pin the receive thread to CPU core 2.
- [ ] Pin the send thread to CPU core 3.
- [ ] Verify affinity with logging on startup.
- [ ] Keep simulation and rendering outside those cores unless you intentionally change that later.

Acceptance criteria:
- Networking work runs on the intended cores.
- Affinity is visible in logs and does not fail silently.

## 6. Implement full-mesh peer bootstrap

- [ ] Add a fixed 4-peer configuration model.
- [ ] Add local peer id selection.
- [ ] Add peer endpoint configuration for all remote peers.
- [ ] Add a handshake phase at startup.
- [ ] Exchange peer ids, ports, and readiness flags.
- [ ] Establish direct connections to every other peer.
- [ ] Ensure each peer ends with 3 active remote links.
- [ ] Make the connection process deterministic so duplicate connections are avoided.

Acceptance criteria:
- Four running instances discover each other and complete a full mesh.
- Each peer reports exactly three remote peers connected.

## 7. Separate reliable and unreliable traffic

- [ ] Send physics state over UDP only.
- [ ] Send scene changes, spawn commands, and other global UI events over TCP.
- [ ] Add message type identifiers so the receiver can route packets correctly.
- [ ] Add simple acknowledgment or delivery confirmation for important TCP messages.
- [ ] Keep UDP packets small and focused on current state only.

Acceptance criteria:
- Scene changes do not depend on UDP delivery.
- Physics updates continue even if a UDP packet is lost.

## 8. Make ownership authoritative

- [ ] Read the existing ownership component and owner enum.
- [ ] Define how local peer id maps to object owner id.
- [ ] Add a helper that checks whether an entity is locally owned.
- [ ] Gate physics simulation so only the owner advances rigid body state.
- [ ] Prevent non-owners from applying collision response to owned objects.
- [ ] Treat non-owned objects as replicated proxies.

Acceptance criteria:
- Only the owning peer simulates each object.
- Remote peers stop fighting over the same physics state.

## 9. Add outbound state broadcasting

- [ ] Build a snapshot of owned entities at a fixed send rate.
- [ ] Serialize transform, linear velocity, angular velocity, and ownership.
- [ ] Assign a sequence number to each snapshot.
- [ ] Broadcast the snapshot to every remote peer.
- [ ] Skip sending unchanged data if that helps bandwidth, but do not make the protocol fragile.
- [ ] Keep the sender side lock duration short.

Acceptance criteria:
- Owned objects produce a steady stream of network state.
- Outbound networking does not stall physics or rendering.

## 10. Add inbound state application

- [ ] Receive UDP snapshots on the network thread.
- [ ] Drop stale packets using sequence numbers.
- [ ] Store the latest state per entity.
- [ ] Apply received state on the simulation or main thread through a safe queue.
- [ ] Only apply remote state to non-owned objects.
- [ ] Keep authoritative local objects untouched by remote packets.

Acceptance criteria:
- Remote objects update cleanly when packets arrive.
- Old packets do not overwrite newer data.

## 11. Add interpolation and dead reckoning

- [ ] Keep a short history buffer per replicated entity.
- [ ] Interpolate between the last two valid snapshots.
- [ ] Use linear velocity and angular velocity to predict motion between packets.
- [ ] Clamp extrapolation to avoid wild divergence.
- [ ] Correct large errors gradually instead of snapping instantly.
- [ ] Add a fallback snap only when the error is extreme.

Acceptance criteria:
- Remote objects move smoothly under normal conditions.
- Jitter and 100 ms latency do not cause obvious teleporting.

## 12. Integrate scene RPC and other reliable UI actions

- [ ] Route scene load changes through the TCP channel.
- [ ] Route spawn/despawn and other important global actions through the reliable path.
- [ ] Apply reliable events in a thread-safe way.
- [ ] Make sure the local scene change and the remote scene change stay in sync.

Acceptance criteria:
- Scene changes arrive reliably.
- UI actions are not lost under packet loss.

## 13. Add resilience for poor network conditions

- [ ] Test with packet loss around 20 percent.
- [ ] Test with latency around 100 ms.
- [ ] Test with jitter if possible.
- [ ] Confirm the game stays responsive when packets are dropped.
- [ ] Confirm stale packets are ignored.
- [ ] Confirm interpolation hides small gaps.
- [ ] Confirm the simulation does not freeze because of network loss.

Acceptance criteria:
- The engine remains playable under aggressive network conditions.
- UDP loss affects quality, not correctness or liveness.

## 14. Add debugging and visibility

- [ ] Show connected peer count.
- [ ] Show current owner for selected objects.
- [ ] Show last UDP send and receive time.
- [ ] Show packet loss or drop counters.
- [ ] Show interpolation buffer depth.
- [ ] Show correction count or snap count.
- [ ] Add log messages for startup, connection, disconnect, and shutdown.

Acceptance criteria:
- You can see whether networking is healthy without attaching a debugger.

## 15. Validate with a repeatable test pass

- [ ] Run two instances locally first.
- [ ] Run four instances locally after the two-peer case is stable.
- [ ] Confirm each instance connects to all others.
- [ ] Confirm one peer can change a scene and all peers follow.
- [ ] Confirm owned objects stay authoritative on the owner peer.
- [ ] Confirm remote object motion is smooth.
- [ ] Confirm the app survives packet loss and latency without crashing.
- [ ] Confirm shutdown is clean with no hanging threads.

Acceptance criteria:
- You can demonstrate the full mesh, ownership model, and resilience together.

## Suggested implementation order

- [ ] Step 1: confirm touchpoints.
- [ ] Step 2: add runtime schema.
- [ ] Step 3: create NetworkManager skeleton.
- [ ] Step 4: bring up Winsock 2 sockets.
- [ ] Step 5: add thread affinity.
- [ ] Step 6: implement mesh bootstrap.
- [ ] Step 7: separate UDP physics and TCP events.
- [ ] Step 8: enforce ownership authority.
- [ ] Step 9: broadcast owned state.
- [ ] Step 10: apply inbound state.
- [ ] Step 11: add interpolation and dead reckoning.
- [ ] Step 12: finalize reliable UI events.
- [ ] Step 13: stress test under poor network conditions.
- [ ] Step 14: polish debug output.
- [ ] Step 15: run the final 4-peer validation pass.
