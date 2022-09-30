# rs1.31
* Engines must no longer call `rs_sendFrame`, `rs_getImage` and `rs_releaseImage`. These have been replaced with a variant which appends `2` to the name. This is in order to change the calling parameters to use a single `SenderFrame` object, while maintaining ABI compatibility with prior releases.
* `VulkanData` now contains the required information directly, rather than requiring a structure (which was due to ABI compatibility concerns.)
* `HostMemoryData` now contains a `format` field which must be set to the pixel format the engine is supplying. Previously this was assumed to be BGRA8.

# rs1.30
* Added support for read-only parameters in the schema - see `REMOTEPARAMETER_READ_ONLY` flag. Breaking change: This requires the engine to pass a `FrameResponseData` to `rs_sendFrame` as the 3rd parameter.
* Added support for Vulkan engines
* Added ability to expose an engine type & version, changed the .json file format saved in `rs_saveSchema` to be versioned (rs1.29 schemas are still compatible.)
* Engines are required to call `rs_releaseImage` when finished with an image parameter retrieved via `rs_getImage`.

# rs1.29
* This is the first version which is ABI forward-compatible with future RenderStream APIs.

