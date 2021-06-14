# RenderStream
RenderStream is a protocol for controlling third party render engines from the [disguise](http://disguise.one/) software. 

Plugins are available for
* Unreal Engine (https://github.com/disguise-one/RenderStream-UE)
* Unity (https://github.com/disguise-one/RenderStream-Unity)
* Notch (bundled with disguise software)

# Getting it
The API is distributed as a 64-bit Windows DLL as part of the disguise software, available at https://download.disguise.one/

# Requirements
* 64-bit Windows 8 and above
* A valid [disguise software license](https://store.disguise.one/) or RenderStream license (available on [rx range](https://www.disguise.one/en/products/rx-range/) hardware)
* An r18.0 install of the disguise software

# Supports
Please raise issues on this GitHub repository or contact support@disguise.one for general support

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
