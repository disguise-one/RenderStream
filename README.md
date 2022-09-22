# RenderStream
RenderStream is a protocol for controlling third party render engines from the [disguise](http://disguise.one/) software. 

Plugins are available for
* Unreal Engine (https://github.com/disguise-one/RenderStream-UE)
* Unity (https://github.com/disguise-one/RenderStream-Unity)
* Notch (bundled with disguise software)

# Getting it
The RenderStream DLL is distributed as a 64-bit Windows DLL as part of the disguise software, available at https://download.disguise.one/

This repository is for API definitions, documentation and examples.

# Requirements
* 64-bit Windows 10 and above
* A valid [disguise software license](https://store.disguise.one/) or RenderStream license (available on [rx range](https://www.disguise.one/en/products/rx-range/) hardware)
* An r21.3 install of the disguise software

# Support
Please raise RenderStream API issues on this GitHub repository or contact support@disguise.one for general disguise support

# Getting started

The RenderStream DLL is distributed as part of the disguise software. To locate the install, you MUST look up its path through the Windows registry.

The path to the main disguise software executable can be found at `Computer\HKEY_CURRENT_USER\SOFTWARE\d3 Technologies\d3 Production Suite\exe path`

Replace the filename in the path with `d3renderstream.dll`. E.g. `C:\Program Files\d3 Production Suite\build\msvc\d3stub.exe` becomes `C:\Program Files\d3 Production Suite\build\msvc\d3renderstream.dll`

Load the DLL, look up the exposed API functions and call `rs_initialise` with the major and minor version your integration is built against. 

NOTE WELL: Workload functions will fail if your integration was not launched via the disguise software. For details on how to add your application as an asset and launch it see [Discovery and launching](#discovery-and-launching)

Call `rs_setSchema` to tell the disguise software what scenes and remote parameters the asset exposes.

Poll `rs_getStreams` for the requested streams.

Poll `rs_awaitFrameData` once per frame for a frame request. 

If a frame request is received, update your simulation using the received frame data as deterministically as possible to keep multiple render nodes in sync. Call `rs_getFrameCamera` for each requested stream, render the streams and send each stream's frame back using `rs_sendFrame`.

# High level overview of RenderStream design

* Users place a custom .exe file or known filetypes within the RenderStream projects folder on the rx machine.
* `d3service`, which is running on the rx machine, will detect that file as a 'RenderStream Asset'.
* RenderStream Assets can be sequenced in a RenderStream layer on the disguise timeline.
* The user chooses when to start the workload defined by the RenderStream layer.
* `d3service` on the rx machines will launch the asset as a new process, called a workload instance.
* There can be multiple workload instances on a given machine, but usually workload instances are spread across multiple rx machines.
* The RenderStream DLL within the workload instance queries the network for the set of streams it needs to provide from a given instance.
* The application is responsible for allocating surfaces and rendering at the requested resolution.
* The RenderStream DLL asynchronously receives requests to render frames.
* The application calls `rs_awaitFrameData` which blocks until a render request is received.
* The application uses the frame data (camera, parameter and timing information) to render a frame.
* The application calls `rs_sendFrame` which returns the rendered frame back to the disguise session for final composition, temporal alignment, colour correction and output to the display devices.
* The application loops around to call `rs_awaitFrameData` again, and the process repeats until the user stops the workload, which causes `rs_awaitFrameData` to request a quit. If the application does not quit in a timely manner, `d3service` will send a close event and eventually terminate the application process.

# Application programming dos and don'ts

In order to support rendering fragments of the same frame or multiple views onto the same scene, care must be taken to avoid common pitfalls which result in divergent simulations of animating scenes. There are also some API subtleties which, when implemented, can improve users' experience working with RenderStream applications.

* DO respond to `RS_ERROR_STREAMS_CHANGED` return value from `rs_awaitFrameData` to change stream definitions at runtime - this includes bit depth and resolution changes.
* DO NOT reference any time source other than the RenderStream time provided by `rs_awaitFrameData`
* DO initialise pseudo-random sources with a fixed seed
* DO NOT use any true random entropy
* DO NOT use arbitrary network communications without first synchronising their values against some RenderStream provided value (such as timestamp) - the time at which network packets arrive on multiple computers can vary, causing different machines to respond differently.

# Discovery and launching

`d3service` scans the RenderStream Projects folder (determined by the `Computer\HKEY_CURRENT_USER\Software\d3 Technologies\d3 Production Suite\RenderStream Projects Folder` registry key) on startup and watches it for changes. 

Any known filetype is detected as a 'RenderStream Asset' and shared with all other machines running disguise software. If a schema file (matching the asset filename with a 'rs_' prefix and '.json' extension) is available, it is parsed and shared alongside the asset.

When a user launches a workload using the asset via a RenderStream layer, `d3service` on each render machine in the cluster pool looks up the executable for the filetype and launches it with the appropriate arguments and environment variables. 

NOTE WELL that this environment must be available for all calls into the RenderStream API. Any new processes spawned by the executable must inherit the environment.

## Known filetypes

`d3service` has built-in support for .exe, .lnk, .uproject and .dfxdll files. Custom filetypes can be added by creating a 'permitted_custom_extensions.txt' file in the RenderStream Projects root with one extension per line (e.g. 'dfx' for .dfx files)

* .exe files are launched as-is with the workload arguments supplied in the RenderStream layer.
* .lnk files are parsed for their executable and arguments ("Target"), and working directory ("Start in") and launched using the target executable. Arguments specified in the .lnk will appear before any workload arguments.
* .uproject files are launched with the Unreal Engine editor associated with the uproject
* .dfxdll files are launched with the notch_host.exe bundled with disguise software
* Files with custom extensions are launched with the default application associated with the file type. The asset's filename will appear before any workload arguments.

# Integrating bidirectional logging

There are 2 independent logging mechanisms, as well as independent channels of status and profiling information. It is the applications responsibility to use these functions - it is highly recommended to do so, as the benefits to users are significant.

## RenderStream logging into application framework

If the application has a logging system of its own, it can be useful to get logging out of the RenderStream DLL into the application logs. This is accomplished by registering a `logger_t` function with one of the `rs_register*LoggingFunc` functions. This will be periodically called with a nul-terminated string with information from the RenderStream DLL. Note that the logging functions will be called from a background thread created by the RenderStream DLL, so ensure the function is reentrant.

## Application logging into RenderStream / d3

Redirecting application logging into RenderStream allows the logging data to be distributed across the network, for viewing in the d3 application. This is extremely useful, especially in clusters of more than one render machine. In order to log information remotely in this manner, call `rs_logToD3` with the message you wish to send.

Please ensure the data logged using this function is clean UTF-8.

Messages are sent atomically, which means that the caller should not use multiple calls to `rs_logToD3` to build a single message.

Messages have a timestamp prepended and newline appended to them.

## Reporting transient application status

As a part of the workload management within d3, it's possible to have a custom status message sent from the application. This is sent using `rs_setNewStatusMessage` - note this is displayed inline with the overall workload instance status, and should be kept short, with no newline. Aim to update the status approximately 1-2 times per second. The status message is designed to communicate information about where in the loading process the application might be. For example, if the application is required to compile shaders or load a large file from disk which takes a significant amount of time, a status message is useful to reassure the user that the application is working.

## Reporting custom application performance data

Often in RenderStream applications, performance is a critical driving factor. Information about the performance of the workload instances is very important to understand if the scene being rendered needs optimising or to give insight into what is happening within the application. This is accomplished by calling `rs_sendProfilingData` with an array of `ProfilingEntry` objects, along with a count of valid entries in the array.

A `ProfilingEntry` contains a `name` string and floating point `value`. The memory backing the `name` field in each entry can be freed immediately after calling `rs_sendProfilingData`, if required.

These profiling entries expand on automatically-gathered profiling data and are available in the metric monitoring section of d3, for remote analysis.

# Schema management

The RenderStream schema is a per-application block of data which tells d3 what sort of sequencable parameters you would like to define for your application. Applications can provide multiple channels and scenes. Channels determine what is rendered from the scene, and the scene determines an overall environment. Scenes have separate lists of controllable parameters.

## Creating a `Schema`

Schema creation needs to be performed at least once by the application - either to save or set the schema for the first time. RenderStream allows `Schema` objects to be loaded from disk via `rs_loadSchema`, if required, but the initial source of the `Schema` is the application code.

The application creates a `Schema` object, and fills the `scenes` member with an application-allocated list of `RemoteParameters` objects, each of which represent a scene. The application fills in a name for the scene, as well as allocating a dynamic list of `RemoteParameter` objects which contain a range of information which d3 uses to constrain sequencing these values.

## Saving a `Schema`

Saving a schema is an optional step - it is possible to rely entirely on `rs_setSchema` at runtime. Some application frameworks do not have enough metadata at runtime to provide the information necessary to create the `Schema` object, and having a schema on-disk allows d3service asset scanning to pre-populate assets with schema information.

During initial development or an editing mode within your application framework, a schema can be defined which includes information about the various externally sequencable parameters the application can accept. This is usually done before RenderStream launches the application, and will be serialised as a `.json` cache of this information, next to the file. It is however possible to update this schema dynamically at any time, even while RenderStream is actively running the workload.

Once the various objects have been allocated and filled in, the application calls `rs_saveSchema(pathToExe, schema)` where `pathToExe` would be the expected location of the executable file of the application, and `schema` would be the previously created `Schema` object as discussed in [Creating a schema](#creating-a-schema).

## Loading a `Schema`

Once a `Schema` object is saved to disk, it's possible to pass the path to the exe file and find the corresponding .json file again. This allows applications which discard metadata at runtime to still provide a valid `Schema` to `rs_setSchema`. If the application has all information necessary to create the `Schema` again at runtime, then there is no reason to call this function.

## Setting a `Schema` at runtime

In order for RenderStream to activate the schema, the application must call `rs_setSchema` with the `Schema` object it has created or loaded. This is typically done immediately after initialisation, but if the application allows dynamic editing of parameters or scenes, it is reasonable to call `rs_setSchema` whenever these change.

# Stream management

## Stream definitions

On startup, and in response to `RS_ERROR_STREAMS_CHANGED` from `rs_awaitFrameData`, the application should call `rs_getStreams`. To understand what to pass to call this function, please see [Buffer calling convention](#buffer-calling-convention).

Once the application has a list of streams the RenderStream system needs from a given instance of a workload, it should allocate the necessary buffers to send frames.

## Handling frame requests

Once the application has a list of stream definitions, it should enter a loop of calling `rs_awaitFrameData` and checking the return status value.

On `RS_ERROR_STREAMS_CHANGED`, apply the above [Stream definitions](#stream-definitions) logic.

On `RS_ERROR_QUIT`, gracefully terminate the application.

On `RS_ERROR_TIMEOUT`, the application can do any non-RenderStream processing it requires. If running as a part of a workload, it is not recommended to perform a render in response to a timeout, and it is not valid to call `rs_sendFrame`.

On RS_ERROR_SUCCESS, the application knows it has been requested to provide a frame for display by d3. It's important to note that a RenderStream 'frame' encompasses potentially multiple individual renders, as multiple streams can be served across the network cluster, or within the individual application.

With the `FrameData` object returned by RenderStream, the application should apply the updates to time provided by RenderStream. If the application has used the schema system, it should also call `rs_getFrameParameters` using the schema information to allocate the correct buffer size. The application is expected to apply these values immediately to whatever elements of the simulation the parameters represent. This is done once per RenderStream frame, not per stream.

The application then calls `rs_getFrameCamera` in an inner loop with the `StreamHandle` value available in each stream definition it queried earlier. See "applying camera data" below. No application simulation or update should be done within this inner loop - only rendering. The goal is to render the same scene from multiple viewpoints, and the method for this may vary per engine. Note that these viewpoints may diverge significantly from each other, depending on the use case. Multiple streams would be rendered to separate buffers. If the application does not support a movable camera, the application is not required to call `rs_getFrameCamera`. Note that this means the application would only be able to serve 2D workloads.

Once the render calls are dispatched (i.e. it is not necessary to wait for any GPU work to complete), the application should call `rs_sendFrame` with the same `StreamHandle` as provided the camera information, as well as a `CameraResponseData` object which must include the tTracked value from the incoming `FrameData` and the `CameraData` from the corresponding call to `rs_getFrameCamera`, if it was performed.

## Applying camera data

The `CameraData` struct, filled in by `rs_getFrameCamera` has 2 modes - perspective, and orthographic. The camera data provided should be applied without smoothing or interpolation, as this is synchronised between all render nodes.

To determine if the camera is in perspective or orthographic mode, check if `orthoWidth == 0` - if true, the camera is a perspective projection, otherwise it is orthographic.

The x, y, z fields are translation coordinates in metres, and the rx,ry, and rz fields correspond to pitch, yaw and roll, in degrees, respectively. The euler order for applying the rotation is yaw, then pitch, then roll. For perspective cameras, this defines the position and orientation of the focal point of the camera. For orthographic cameras, this defines the center of the image plane within the view volume.

Aspect ratio (`aspectRatio` below) is determined by `sensorX / sensorY`.

Orthographic cameras use `orthoWidth` for the horizontal measurement of the view volume, `orthoWidth / aspectRatio` for the vertical measurement. Both are in metres.

Perspective cameras should use the focal length and sensor sizes to compute throw ratios or field of view values as appropriate.

Once the perspective matrix is found, it is necessary to also apply the clipping values to adjust the projected area. This allows distribution of the frame buffer across multiple nodes. This information is available in the `StreamDescription` for the given stream.

The samples in this repository show this process in detail, and it is recommended to use the calculations given in the samples as reference for this, to ensure correctness.

# Buffer calling convention

Several methods in the RenderStream API require a buffer to be allocated and freed by the application, so that RenderStream can fill that buffer with information for the application to process.

The question of how much to allocate can be answered by that method. For an example, using `rs_getStreams`:
```C
StreamDescriptions* descriptions = NULL;
uint32_t descriptionsSize = 0;
rs_getStreams(NULL, &descriptionsSize); // returns RS_ERROR_BUFFER_OVERFLOW, and sets descriptionsSize to required size in bytes.
descriptions = malloc(descriptionsSize);
rs_getStreams(descriptions, &descriptionsSize); // returns RS_ERROR_SUCCESS
```
