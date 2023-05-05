# RS2.0
Compatible with disguise designer version r25.0 and above.
* Removed unused parameters from D3TrackingData
* Added aperture and focus distance values to CameraData
* Added plugin version identifier to Schema
* Added support for skeleton parameters

# RS1.32
Compatible with disguise designer version r23.4 and above.
* Added mappingName field to StreamDescription.
    * Contains the name of the mapping in d3 that this stream is used for.
* Added iFragment field to StreamDescription.
    * Contains index of the fragment of the mapping in d3 that this stream is used for.
    * d3 will split mappings into multiple fragments when a mapping is distributed over multiple render engines.
	
# RS1.31
Compatible with disguise designer version r22.0 and above.
* Engines must no longer call `rs_sendFrame`, `rs_getImage` and `rs_releaseImage`. These have been replaced with a variant which appends `2` to the name. This is in order to change the calling parameters to use a single `SenderFrame` object, while maintaining ABI compatibility with prior releases.
* `VulkanData` now contains the required information directly, rather than requiring a structure (which was due to ABI compatibility concerns.)
* `HostMemoryData` now contains a `format` field which must be set to the pixel format the engine is supplying. Previously this was assumed to be BGRA8.

# RS1.30
Compatible with disguise designer version r22.0 and above.
* Added support for read-only parameters in the schema - see `REMOTEPARAMETER_READ_ONLY` flag. Breaking change: This requires the engine to pass a `FrameResponseData` to `rs_sendFrame` as the 3rd parameter.
* Added support for Vulkan engines
* Added ability to expose an engine type & version, changed the .json file format saved in `rs_saveSchema` to be versioned (rs1.29 schemas are still compatible.)
* Engines are required to call `rs_releaseImage` when finished with an image parameter retrieved via `rs_getImage`.

# RS1.29
Compatible with disguise designer version r21.0 and above.
* This is the first version which is ABI forward-compatible with future RenderStream APIs.

