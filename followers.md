# Followers

Followers allow RenderStream-enabled engines to distribute simulation data between nodes. This allows engines to perform processing which would otherwise be difficult
or impossible to distribute, due to necessary local state, network dependencies, or numerical precision issues.

RenderStream does not provide the primitives to perform this synchronisation - the engine is responsible for the serialisation, transmission and deserialisation of the engine
data. What the follower system in RenderStream allows for, is the synchronisation of RenderStream-specific information and the associated synchronisation of the resulting frame
buffers.

## Overview of the process

The engine integration must decide the following roles for each node within a cluster:
- A single node in the cluster is nominated as the `controller`.
- All other nodes must behave as `followers`.

## Controller processing logic
- Call `rs_awaitFrameData` and respond as appropriate.
- Perform any simulation or other non-deterministic or state-mutating processing.
- Serialise & transmit the results of the above processing. This MUST include the `tTracked` value from the `FrameData` returned from `rs_awaitFrameData`
- Render the frame using `rs_getFrameCamera` and calls `rs_sendFrame` as normal.

## Follower processing logic
- Once on startup, call `rs_setFollower(1)`.
- Block on receiving the results of non-deterministic processing from the controller.
- Deserialise and apply the received results.
- Use the `tTracked` value from the received results, and call `rs_beginFollowerFrame(tTracked)`. Do *not* call `rs_awaitFrameData`.
- The status returned from `rs_beginFollowerFrame` can be any of the values normally returned from `rs_awaitFrameData`.
- If `RS_ERROR_STREAMS_CHANGED` is returned, the engine must adjust the streams as appropriate, and call `rs_beginFollowerFrame` with the same `tTracked` value.
- Render the frame using `rs_getFrameCamera` and call `rs_sendFrame` as normal.
