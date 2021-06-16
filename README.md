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
* 64-bit Windows 8 and above
* A valid [disguise software license](https://store.disguise.one/) or RenderStream license (available on [rx range](https://www.disguise.one/en/products/rx-range/) hardware)
* An r18.0 install of the disguise software

# Support
Please raise RenderStream API issues on this GitHub repository or contact support@disguise.one for general disguise support

# Getting started

The RenderStream DLL is distributed as part of the disguise software. To locate the install, you MUST look up its path through the Windows registry.

The path to the main disguise software executable can be found at `Computer\HKEY_CURRENT_USER\SOFTWARE\d3 Technologies\d3 Production Suite\exe path`

Replace the filename in the path with `d3renderstream.dll`. E.g. `C:\Program Files\d3 Production Suite\build\msvc\d3stub.exe` becomes `C:\Program Files\d3 Production Suite\build\msvc\d3renderstream.dll`

Load the DLL, look up the exposed API functions and call `rs_initialise` with the major and minor version your integration is built against. 

NB. Workload functions will fail if your integration was not launched via the disguise software. For details on how to add your application as an asset and launch it see https://help.disguise.one/Content/Configuring/RenderStream-r18.htm

Call `rs_setSchema` to tell the disguise software what scenes and remote parameters the asset exposes.

Poll `rs_getStreams` for the requested streams.

Poll `rs_awaitFrameData` once per frame for a frame request. 

If a frame request is received, update your simulation using the received frame data as deterministically as possible to keep multiple render nodes in sync. Call `rs_getFrameCamera` for each requested stream, render the streams and send each stream's frame back using `rs_sendFrame`.

# High level overview of RenderStream design

* Users place a custom .exe file or built-in known filetypes within the RenderStream projects folder on the rx machine.
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
* The application loops around to call `rs_awaitFrameData` again, and the process repeats until the user stops the workload, which causes WM_CLOSE to be issued to the application.

# Application programming dos and don'ts

In order to support rendering fragments of the same frame or multiple views onto the same scene, care must be taken to avoid common pitfalls which result in divergent simulations of animating scenes. There are also some API subtleties which, when implemented, can improve users' experience working with RenderStream applications.

* DO respond to `RS_ERROR_STREAMS_CHANGED` return value from `rs_awaitFrameData` to change stream definitions at runtime - this includes bit depth and resolution changes.
* DO NOT reference any time source other than the RenderStream time provided by `rs_awaitFrameData`
* DO initialise pseudo-random sources with a fixed seed
* DO NOT use any true random entropy
* DO NOT use arbitrary network communications without first synchronising their values against some RenderStream provided value (such as timestamp) - the time at which network packets arrive on multiple computers can vary, causing different machines to respond differently.

# Integrating bidirectional logging

There are 2 independent logging mechanisms, as well as independent channels of status and profiling information. It is the applications responsibility to use these functions - it is highly recommended to do so, as the benefits to users are significant.

## RenderStream logging into application framework

If the application has a logging system of its own, it can be useful to get logging out of the RenderStream DLL into the application logs. This is accomplished by registering a `logger_t` function with one of the `rs_register*LoggingFunc` functions. This will be periodically called with a nul-terminated string with information from the RenderStream DLL. Note that the logging functions will be called from a background thread created by the RenderStream DLL, so ensure the function is reentrant.

## Application logging into RenderStream / d3

Redirecting application logging into d3 console files allows the logging data to be distributed across the network, for viewing in the d3 application. This is useful, especially in clusters of more than one machine. In order to log information remotely in this manner, simply call `rs_logToD3` with the message you wish to send. Please ensure the data logged using this function is clean UTF-8.

## Reporting transient application status

As a part of the workload management within d3, it's possible to have a custom status message sent from the application. This is sent using `rs_setNewStatusMessage` - note this is displayed inline with the overall workload instance status, and should be kept short, with no newline. Aim to update the status approximately 1-2 times per second. The status message is designed to communicate information about where in the loading process the application might be. For example, if the application is required to compile shaders or load a large file from disk which takes a significant amount of time, a status message is useful to reassure the user that the application is working.

## Reporting custom application performance data

Often in RenderStream applications, performance is a critical driving factor. Information about the performance of the workload instances is very important to understand if the scene being rendered needs optimising or to give insight into what is happening within the application. This is accomplished by calling `rs_sendProfilingData` with an array of `ProfilingEntry` objects, along with a count of valid entries in the array.

A `ProfilingEntry` contains a `name` string and floating point `value`. The memory backing the `name` field in each entry can be freed immediately after calling `rs_sendProfilingData`, if required.

These profiling entries expand on automatically-gathered profiling data and are available in the metric monitoring section of d3, for remote analysis.
