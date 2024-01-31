# Followers

RenderStream applications render a view onto a virtual scene. We refer to the state of that scene (location and properties of objects, for example) as the simulation state.

Engine sync allows RenderStream-enabled engines to distribute simulation state between nodes. This is an optional extension to the RenderStream system, designed for engines which wish to serialise their internal, private state to take advantage of RenderStream synchronisation. This allows engines to perform processing which would otherwise be difficult or impossible to distribute, due to local state, frame drops, network dependencies, or numerical precision issues.

RenderStream provides a straightforward set of APIs to accomplish this synchronisation. The engine is responsible for ensuring these APIs are called according to the engine synchronisation contract laid out in this document.

## Rationale

RenderStream by default optimisically expects all nodes to process all frames, and for all frames to be repeatable exclusively based on the shared simulation time. In many circumstances, this is not the case, and individual nodes diverge from each other. This section explains some cases which can cause this behaviour, and how setting up followers can alleviate the issue. Given the solution is distribution of the simulation state, care must be given to the size of the distributed data set. Particle systems, in particular, will be challenging to distribute using this mechanism.

### Local state & frame drops
A common mechanism to implement animation in an application is to simply increment a frame counter once per frame. Physical simulations can sometimes be run at a variable frame step, which alters the integration of acceleration, or the time at which a force is applied.

On a single machine, this is straightforward, but in a cluster, it is possible for individual machines to process a different set of frames. This happens if the engine CPU time is overloaded, and frame requests are queued within RenderStream for too long. RenderStream will discard these requests in order to ensure overall latency remains bounded. It is also possible if parts of the system run from a real-time clock instead of the RenderStream-provided synchronised time values.

Given these local decision points, it is possible for different nodes to drop different frames, and for different nodes to end up processing a different number of frames or at different times. While within d3 these frames will be recombined according to the tracked time, the contents of these frames (the simulation state) can diverge, causing a tearing appearance.

### Networking & other external signals
If individual nodes base a part of their simulation processing on receipt of a network message, it is possible to introudce a discrepancy. This is the case even if the network is completely synchronised, and the message is guaranteed to arrive at exactly the same time across the machines (which is itself nearly impossible to guarantee.)

This is because at a given moment in time, there is no guarantee that any 2 nodes in a cluster are processing the same frame. For example, it's possible that the network message arrives on frame 1 for node 1, but on frame 2 for node 2. When the message is processed on a different frame, against a different simulation state, the simulation will no longer be consistent between nodes, causing simulation divergence.

### Non-deterministic numerical calculations
In particularly complex engines, the order of operations is not guaranteed. It is possible for objects 1 & 2 to be processed in different orders. If those objects somehow depend on one another, the simulation can diverge.

All of these points above are reasons why RenderStream provides the engine synchronisation extension.

## Implementation guide

RenderStream nominates a single node within the cluster as the `controller`. The controller is responsible for updating the simulation state.

Other machines in the cluster are nominated as `followers` - followers will render exactly the same frames as the controller, and will deserialise the simulation state.

The engine can determine whether it is the controller by calling `rs_isController`.

Engine sync is an optional extension to RenderStream. You can verify it's enabled by checking the value returned by `rs_engineSyncEnabled`. If it is not enabled, you must not call the `rs_*FollowerData` functions. If it is enabled, you must call them on every frame, to avoid stalling the follower nodes.

### Controller processing logic
- Call `rs_awaitFrameData` and respond as appropriate.
- Perform any simulation or other non-deterministic or state-mutating processing.
- Serialise the engine state as desired and pass that data to `rs_sendFollowerData.
- Render the frame using `rs_getFrameCamera` and calls `rs_sendFrame` as normal.

### Follower processing logic
- Call `rs_awaitFrameData` and respond as appropriate.
- Call `rs_receiveFollowerData`, deserialise and apply the received bytes to the internal engine state.
- Render the frame using `rs_getFrameCamera` and call `rs_sendFrame` as normal.
