// A RenderStream application that exposes a few scenes with remote parameters
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

struct ScopedSchema
{
    ScopedSchema()
    {
        clear();
    }
    ~ScopedSchema()
    {
        reset();
    }
    void reset()
    {
        for (size_t i = 0; i < schema.channels.nChannels; ++i)
            free(const_cast<char*>(schema.channels.channels[i]));
        free(schema.channels.channels);
        for (size_t i = 0; i < schema.scenes.nScenes; ++i)
        {
            RemoteParameters& scene = schema.scenes.scenes[i];
            free(const_cast<char*>(scene.name));
            for (size_t j = 0; j < scene.nParameters; ++j)
            {
                RemoteParameter& parameter = scene.parameters[j];
                free(const_cast<char*>(parameter.group));
                free(const_cast<char*>(parameter.displayName));
                free(const_cast<char*>(parameter.key));
                for (size_t k = 0; k < parameter.nOptions; ++k)
                {
                    free(const_cast<char*>(parameter.options[k]));
                }
                free(parameter.options);
            }
            free(scene.parameters);
        }
        free(schema.scenes.scenes);
        clear();
    }

    ScopedSchema(const ScopedSchema&) = delete;
    ScopedSchema(ScopedSchema&& other)
    {
        schema = std::move(other.schema);
        other.reset();
    }
    ScopedSchema& operator=(const ScopedSchema&) = delete;
    ScopedSchema& operator=(ScopedSchema&& other)
    {
        schema = std::move(other.schema);
        other.reset();
        return *this;
    }

    Schema schema;

private:
    void clear()
    {
        schema.channels.nChannels = 0;
        schema.channels.channels = nullptr;
        schema.scenes.nScenes = 0;
        schema.scenes.scenes = nullptr;
    }
};

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

// Load schema into (schemaMem) buffer and return a pointer into it
const Schema* loadSchema(decltype(rs_loadSchema)* rs_loadSchema, const char* assetPath, std::vector<uint8_t>& schemaMem)
{
    uint32_t nBytes = 0;
    rs_loadSchema(assetPath, nullptr, &nBytes);

    const static int MAX_TRIES = 3;
    int iterations = 0;

    RS_ERROR res = RS_ERROR_BUFFER_OVERFLOW;
    do
    {
        schemaMem.resize(nBytes);
        res = rs_loadSchema(assetPath, reinterpret_cast<Schema*>(schemaMem.data()), &nBytes);

        if (res == RS_ERROR_SUCCESS)
            break;

        ++iterations;
    } while (res == RS_ERROR_BUFFER_OVERFLOW && iterations < MAX_TRIES);

    if (res != RS_ERROR_SUCCESS)
        throw std::runtime_error("Failed to load schema");

    if (nBytes < sizeof(Schema))
        throw std::runtime_error("Invalid schema");

    return reinterpret_cast<const Schema*>(schemaMem.data());
}

void addField(RemoteParameter& parameter, const std::string& key, const std::string& displayName, const std::string& group, float defaultValue, float min = 0, float max = 255, float step = 1, const std::vector<std::string>& options = {})
{
    if (!options.empty())
    {
        min = 0;
        max = float(options.size() - 1);
        step = 1;
    }

    parameter.group = _strdup(group.c_str());
    parameter.displayName = _strdup(displayName.c_str());
    parameter.key = _strdup(key.c_str());
    parameter.defaultValue = defaultValue;
    parameter.min = min;
    parameter.max = max;
    parameter.step = step;
    parameter.nOptions = uint32_t(options.size());
    parameter.options = static_cast<const char**>(malloc(parameter.nOptions * sizeof(const char*)));
    for (size_t j = 0; j < options.size(); ++j)
    {
        parameter.options[j] = _strdup(options[j].c_str());
    }
    parameter.dmxOffset = -1; // Auto
    parameter.dmxType = 2; // Dmx8 = 0, Dmx16BigEndian = 2
}

int main(int argc, char** argv)
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
    LOAD_FN(rs_saveSchema);
    LOAD_FN(rs_loadSchema);
    LOAD_FN(rs_setSchema);
    LOAD_FN(rs_getStreams);
    LOAD_FN(rs_awaitFrameData);
    LOAD_FN(rs_getFrameParameters);
    LOAD_FN(rs_getFrameCamera);
    LOAD_FN(rs_sendFrame);
    LOAD_FN(rs_shutdown);

    if (rs_initialise(RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to initialise RenderStream" << std::endl;
        return 3;
    }

    {
        // Loading a schema from disk is useful if some parts of it cannot be generated during runtime (ie. it is exported from an editor) 
        // or if you want it to be user-editable
        std::vector<uint8_t> schemaMem;
        const Schema* schema = loadSchema(rs_loadSchema, argv[0], schemaMem);
        if (schema && schema->scenes.nScenes > 0)
            tcout << "A schema existed on disk" << std::endl;
    }

    ScopedSchema scoped; // C++ helper that cleans up mallocs and strdups
    scoped.schema.scenes.nScenes = 2;
    scoped.schema.scenes.scenes = static_cast<RemoteParameters*>(malloc(scoped.schema.scenes.nScenes * sizeof(RemoteParameters)));
    scoped.schema.scenes.scenes[0].name = _strdup("Strobe");
    scoped.schema.scenes.scenes[0].nParameters = 5;
    scoped.schema.scenes.scenes[0].parameters = static_cast<RemoteParameter*>(malloc(scoped.schema.scenes.scenes[0].nParameters * sizeof(RemoteParameter)));
    addField(scoped.schema.scenes.scenes[0].parameters[0], "stable_shared_key_speed", "Strobe speed", "Shared properties", 1.f, 0.f, 4.f, 0.01f);
    addField(scoped.schema.scenes.scenes[0].parameters[1], "stable_key_colour_r", "Colour R", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[2], "stable_key_colour_g", "Colour G", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[3], "stable_key_colour_b", "Colour B", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[4], "stable_key_colour_a", "Colour A", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    scoped.schema.scenes.scenes[1].name = _strdup("Radar");
    scoped.schema.scenes.scenes[1].nParameters = 3;
    scoped.schema.scenes.scenes[1].parameters = static_cast<RemoteParameter*>(malloc(scoped.schema.scenes.scenes[1].nParameters * sizeof(RemoteParameter)));
    addField(scoped.schema.scenes.scenes[1].parameters[0], "stable_shared_key_speed", "Radar speed", "Shared properties", 1.f, 0.f, 4.f, 0.01f);
    addField(scoped.schema.scenes.scenes[1].parameters[1], "stable_key_length", "Length", "Radar properties", 0.25f, 0.f, 1.f, 0.01f);
    addField(scoped.schema.scenes.scenes[1].parameters[2], "stable_key_direction", "Direction", "Radar properties", 1, 0, 1, 1, { "Left", "Right" });
    if (rs_setSchema(&scoped.schema) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to set schema" << std::endl;
        return 4;
    }

    // Saving the schema to disk makes the remote parameters available in d3's UI before the application is launched
    if (rs_saveSchema(argv[0], &scoped.schema) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to save schema" << std::endl;
        return 41;
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
                return 5;
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

        if (frameData.scene >= scoped.schema.scenes.nScenes)
        {
            tcerr << "Scene out of bounds" << std::endl;
            continue;
        }

        const auto& scene = scoped.schema.scenes.scenes[frameData.scene];
        std::vector<float> parameters(scene.nParameters);
        if (rs_getFrameParameters(scene.hash, parameters.data(), parameters.size() * sizeof(float)) != RS_ERROR_SUCCESS)
        {
            tcerr << "Failed to get frame parameters" << std::endl;
            continue;
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
                struct Colour
                {
                    uint8_t b, g, r, a;
                };
                static_assert(sizeof(Colour) == 4, "32-bit Colour struct");
                std::vector<Colour> pixels;

                switch (frameData.scene)
                {
                    case 0: // "Strobe"
                    {
                        const float speed = parameters[0];
                        const float r = parameters[1];
                        const float g = parameters[2];
                        const float b = parameters[3];
                        const float a = parameters[4];
                        const double strobe = abs(1.0 - fmod(frameData.tTracked * speed, 2.0));
                        const Colour colour = { 
                            uint8_t(b * strobe * 255), 
                            uint8_t(g * strobe * 255), 
                            uint8_t(r * strobe * 255), 
                            uint8_t(a * strobe * 255) 
                        };
                        pixels.resize(description.width * description.height, colour);
                        break;
                    }
                    case 1: // "Radar"
                    {
                        const float speed = parameters[0];
                        const float length = parameters[1];
                        const bool left = !parameters[2];
                        const Colour clear = { 0, 0, 0, 0 };
                        pixels.resize(description.width * description.height, clear);
                        // Our stream may be sub-stream of a larger canvas, work out full canvas width
                        const float canvasWidth = float(description.width) / (description.clipping.right - description.clipping.left);
                        const int xOffset = int(description.clipping.left * canvasWidth);
                        int xCanvas = int(frameData.tTracked * speed * canvasWidth);
                        if (left)
                            xCanvas = -xCanvas;
                        const size_t lengthPixels = size_t(length * canvasWidth);
                        for (size_t y = 0; y < description.height; ++y)
                        {
                            for (int offset = int(lengthPixels); offset >= 0; --offset)
                            {
                                const uint8_t fade = uint8_t(255.f*(lengthPixels-offset)/lengthPixels);
                                const Colour colour = { fade, fade, fade, fade };
                                const size_t x = (left ? (xCanvas + offset) : (xCanvas - offset)) % size_t(canvasWidth);
                                const int xLocal = int(x) - xOffset;
                                if (xLocal >= 0 && xLocal < int(description.width))
                                    pixels[xLocal + (y * description.width)] = colour;
                            }
                        }
                        break;
                    }
                }

                SenderFrameTypeData data;
                data.cpu.stride = description.width * sizeof(Colour);
                data.cpu.data = reinterpret_cast<uint8_t*>(pixels.data());

                if (rs_sendFrame(description.handle, RS_FRAMETYPE_HOST_MEMORY, data, &response) != RS_ERROR_SUCCESS)
                {
                    tcerr << "Failed to send frame" << std::endl;
                    rs_shutdown();
                    return 6;
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
