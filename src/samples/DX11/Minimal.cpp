// A minimal RenderStream application that sends back a strobe using host memory
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <vector>
#include <windows.h>
#include <shlwapi.h>
#include <tchar.h>

#include "../../include/d3renderstream.h"

#if defined(UNICODE) || defined(_UNICODE)
#define tcout std::wcout
#define tcerr std::wcerr
#else
#define tcout std::cout
#define tcerr std::cerr
#endif

#pragma comment(lib, "Shlwapi.lib")

// Load renderstream DLL from disguise software's install path
HMODULE loadRenderStream()
{
    HKEY hKey;
    if (FAILED(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\d3 Technologies\\d3 Production Suite"), 0, KEY_READ, &hKey)))
    {
        tcerr << "Failed to open 'Software\\d3 Technologies\\d3 Production Suite' registry key" << std::endl;
        return nullptr;
    }

    TCHAR buffer[512];
    DWORD bufferSize = sizeof(buffer);
    if (FAILED(RegQueryValueEx(hKey, TEXT("exe path"), 0, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize)))
    {
        tcerr << "Failed to query value of 'exe path'" << std::endl;
        return nullptr;
    }

    if (!PathRemoveFileSpec(buffer))
    {
        tcerr << "Failed to remove file spec from path: " << buffer << std::endl;
        return nullptr;
    }

    if (_tcscat_s(buffer, bufferSize, TEXT("\\d3renderstream.dll")) != 0)
    {
        tcerr << "Failed to append filename to path: " << buffer << std::endl;
        return nullptr;
    }

    HMODULE hLib = ::LoadLibraryEx(buffer, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!hLib)
    {
        tcerr << "Failed to load dll: " << buffer << std::endl;
        return nullptr;
    }
    return hLib;
}

// Get streams into (descMem) buffer and return a pointer into it
const StreamDescriptions* getStreams(decltype(rs_getStreams)* rs_getStreams, std::vector<uint8_t>& descMem)
{
    uint32_t nBytes = 0;
    rs_getStreams(nullptr, &nBytes);

    const static int MAX_TRIES = 3;
    int iterations = 0;

    RS_ERROR res = RS_ERROR_BUFFER_OVERFLOW;
    do
    {
        descMem.resize(nBytes);
        res = rs_getStreams(reinterpret_cast<StreamDescriptions*>(descMem.data()), &nBytes);

        if (res == RS_ERROR_SUCCESS)
            break;

        ++iterations;
    } while (res == RS_ERROR_BUFFER_OVERFLOW && iterations < MAX_TRIES);

    if (res != RS_ERROR_SUCCESS)
        throw std::runtime_error("Failed to get streams");

    if (nBytes < sizeof(StreamDescriptions))
        throw std::runtime_error("Invalid stream descriptions");

    return reinterpret_cast<const StreamDescriptions*>(descMem.data());
}

int main()
{
    HMODULE hLib = loadRenderStream();
    if (!hLib)
    {
        tcerr << "Failed to load RenderStream DLL" << std::endl;
        return 1;
    }

#define LOAD_FN(FUNC_NAME) \
    decltype(FUNC_NAME)* FUNC_NAME = reinterpret_cast<decltype(FUNC_NAME)>(GetProcAddress(hLib, #FUNC_NAME)); \
    if (!FUNC_NAME) { \
        tcerr << "Failed to get function " #FUNC_NAME " from DLL" << std::endl; \
        return 2; \
    }

    LOAD_FN(rs_initialise);
    LOAD_FN(rs_getStreams);
    LOAD_FN(rs_awaitFrameData);
    LOAD_FN(rs_getFrameCamera);
    LOAD_FN(rs_sendFrame);
    LOAD_FN(rs_shutdown);

    if (rs_initialise(RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to initialise RenderStream" << std::endl;
        return 3;
    }

    std::vector<uint8_t> descMem;
    const StreamDescriptions* header = nullptr;
    FrameData frameData;
    while (true)
    {
        // Wait for a frame request
        RS_ERROR err = rs_awaitFrameData(5000, &frameData);
        if (err == RS_ERROR_STREAMS_CHANGED)
        {
            try
            {
                header = getStreams(rs_getStreams, descMem);
            }
            catch (const std::exception& e)
            {
                tcerr << e.what() << std::endl;
                rs_shutdown();
                return 4;
            }
            tcout << "Found " << (header ? header->nStreams : 0) << " streams" << std::endl;
            continue;
        }
        else if (err == RS_ERROR_TIMEOUT)
        {
            continue;
        }
        else if (err != RS_ERROR_SUCCESS)
        {
            tcerr << "rs_awaitFrameData returned " << err << std::endl;
            break;
        }

        // Respond to frame request
        const size_t numStreams = header ? header->nStreams : 0;
        for (size_t i = 0; i < numStreams; ++i)
        {
            const StreamDescription& description = header->streams[i];

            CameraResponseData response;
            response.tTracked = frameData.tTracked;
            if (rs_getFrameCamera(description.handle, &response.camera) == RS_ERROR_SUCCESS)
            {
                SenderFrameTypeData data;
                data.cpu.stride = description.width * 4;
                std::vector<uint8_t> pixels;
                pixels.resize(data.cpu.stride * description.height, uint8_t(abs(1.0 - fmod(frameData.tTracked, 2.0)) * 255));
                data.cpu.data = pixels.data();

                if (rs_sendFrame(description.handle, RS_FRAMETYPE_HOST_MEMORY, data, &response) != RS_ERROR_SUCCESS)
                {
                    tcerr << "Failed to send frame" << std::endl;
                    rs_shutdown();
                    return 5;
                }
            }
        }
    }

    if (rs_shutdown() != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to shutdown RenderStream" << std::endl;
        return 99;
    }

    return 0;
}
