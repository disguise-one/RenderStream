// A RenderStream application that exposes a few scenes with remote parameters
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3

#include <tchar.h>
#include <vector>

#include "../../include/renderstream.hpp"

#if defined(UNICODE) || defined(_UNICODE)
#define tcout std::wcout
#define tcerr std::wcerr
#else
#define tcout std::cout
#define tcerr std::cerr
#endif

void addField(RemoteParameter& parameter, const std::string& key, const std::string& displayName, const std::string& group, float defaultValue, float min = 0, float max = 255, float step = 1, const std::vector<std::string>& options = {}, bool allowSequencing = true, bool readOnly = false)
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
    parameter.type = RS_PARAMETER_NUMBER;
    parameter.defaults.number.defaultValue = defaultValue;
    parameter.defaults.number.min = min;
    parameter.defaults.number.max = max;
    parameter.defaults.number.step = step;
    parameter.nOptions = uint32_t(options.size());
    parameter.options = static_cast<const char**>(malloc(parameter.nOptions * sizeof(const char*)));
    for (size_t j = 0; j < options.size(); ++j)
    {
        parameter.options[j] = _strdup(options[j].c_str());
    }
    parameter.dmxOffset = -1; // Auto
    parameter.dmxType = RS_DMX_16_BE;
    parameter.flags = REMOTEPARAMETER_NO_FLAGS;
    if (!allowSequencing)
        parameter.flags |= REMOTEPARAMETER_NO_SEQUENCE;
    if (readOnly)
        parameter.flags |= REMOTEPARAMETER_READ_ONLY;
}

int mainImpl(int argc, char** argv)
{
    RenderStream rs;

    rs.initialise();
    rs.initialiseGpGpuWithoutInterop();

    {
        // Loading a schema from disk is useful if some parts of it cannot be generated during runtime (ie. it is exported from an editor) 
        // or if you want it to be user-editable
        const Schema* schema = rs.loadSchema(argv[0]);
        if (schema && schema->scenes.nScenes > 0)
            tcout << "A schema existed on disk" << std::endl;
    }
    
    std::vector<const char*> channels;
    channels.push_back("Default");

    ScopedSchema scoped; // C++ helper that cleans up mallocs and strdups
    scoped.schema.engineName = _strdup("Schema sample");
    scoped.schema.engineVersion = _strdup(("RS" + std::to_string(RENDER_STREAM_VERSION_MAJOR) + "." + std::to_string(RENDER_STREAM_VERSION_MINOR)).c_str());
    scoped.schema.pluginVersion = _strdup(("RS" + std::to_string(RENDER_STREAM_VERSION_MAJOR) + "." + std::to_string(RENDER_STREAM_VERSION_MINOR) + "-Samples").c_str());
    scoped.schema.info = _strdup("");
    scoped.schema.channels.nChannels = static_cast<uint32_t>(channels.size());
    scoped.schema.channels.channels = channels.data();
    scoped.schema.scenes.nScenes = 2;
    scoped.schema.scenes.scenes = static_cast<RemoteParameters*>(malloc(scoped.schema.scenes.nScenes * sizeof(RemoteParameters)));
    scoped.schema.scenes.scenes[0].name = _strdup("Strobe");
    scoped.schema.scenes.scenes[0].nParameters = 6;
    scoped.schema.scenes.scenes[0].parameters = static_cast<RemoteParameter*>(malloc(scoped.schema.scenes.scenes[0].nParameters * sizeof(RemoteParameter)));
    addField(scoped.schema.scenes.scenes[0].parameters[0], "stable_shared_key_speed", "Strobe speed", "Shared properties", 1.f, 0.f, 4.f, 0.01f, {}, false);
    addField(scoped.schema.scenes.scenes[0].parameters[1], "stable_key_colour_r", "Colour R", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[2], "stable_key_colour_g", "Colour G", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[3], "stable_key_colour_b", "Colour B", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[4], "stable_key_colour_a", "Colour A", "Strobe properties", 1.f, 0.f, 1.f, 0.001f);
    addField(scoped.schema.scenes.scenes[0].parameters[5], "stable_key_strobe_ro", "Strobe", "Strobe properties", 1.f, 0.f, 1.f, 0.001f, {}, false, true);
    scoped.schema.scenes.scenes[1].name = _strdup("Radar");
    scoped.schema.scenes.scenes[1].nParameters = 3;
    scoped.schema.scenes.scenes[1].parameters = static_cast<RemoteParameter*>(malloc(scoped.schema.scenes.scenes[1].nParameters * sizeof(RemoteParameter)));
    addField(scoped.schema.scenes.scenes[1].parameters[0], "stable_shared_key_speed", "Radar speed", "Shared properties", 1.f, 0.f, 4.f, 0.01f, {}, false);
    addField(scoped.schema.scenes.scenes[1].parameters[1], "stable_key_length", "Length", "Radar properties", 0.25f, 0.f, 1.f, 0.01f);
    addField(scoped.schema.scenes.scenes[1].parameters[2], "stable_key_direction", "Direction", "Radar properties", 1, 0, 1, 1, { "Left", "Right" });
    rs.setSchema(&scoped.schema);

    // Saving the schema to disk makes the remote parameters available in d3's UI before the application is launched
    rs.saveSchema(argv[0], &scoped.schema);

    const StreamDescriptions* header = nullptr;
    while (true)
    {
        // Wait for a frame request
        auto awaitResult = rs.awaitFrameData(5000);
        if (std::holds_alternative<RS_ERROR>(awaitResult))
        {
            RS_ERROR err = std::get<RS_ERROR>(awaitResult);
            if (err == RS_ERROR_STREAMS_CHANGED)
            {
                header = rs.getStreams();
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
        }

        const FrameData& frameData = std::get<FrameData>(awaitResult);
        if (frameData.scene >= scoped.schema.scenes.nScenes)
        {
            tcerr << "Scene out of bounds" << std::endl;
            continue;
        }

        const RemoteParameters& scene = scoped.schema.scenes.scenes[frameData.scene];
        ParameterValues values = rs.getFrameParameters(scene);

        // Respond to frame request
        const size_t numStreams = header ? header->nStreams : 0;
        for (size_t i = 0; i < numStreams; ++i)
        {
            const StreamDescription& description = header->streams[i];

            CameraResponseData cameraData;
            cameraData.tTracked = frameData.tTracked;
            try
            {
                cameraData.camera = rs.getFrameCamera(description.handle);
            }
            catch (const RenderStreamError& e)
            {
                if (e.error == RS_ERROR_NOTFOUND)
                    continue;
                throw;
            }

            {
                if (description.format != RSPixelFormat::RS_FMT_BGRA8 && description.format != RSPixelFormat::RS_FMT_BGRX8)
                {
                    tcerr << "Unsupported pixel format" << std::endl;
                    continue;
                }
                struct Colour
                {
                    uint8_t b, g, r, a;
                };
                static_assert(sizeof(Colour) == 4, "32-bit Colour struct");
                std::vector<Colour> pixels;
                std::vector<float> outParameters;
                std::vector<const char*> outTexts;

                switch (frameData.scene)
                {
                    case 0: // "Strobe"
                    {
                        const float speed = values.get<float>("stable_shared_key_speed");
                        const float r = values.get<float>("stable_key_colour_r");
                        const float g = values.get<float>("stable_key_colour_g");
                        const float b = values.get<float>("stable_key_colour_b");
                        const float a = values.get<float>("stable_key_colour_a");
                        const double strobe = abs(1.0 - fmod(frameData.tTracked * speed, 2.0));
                        const Colour colour = { 
                            uint8_t(b * strobe * 255), 
                            uint8_t(g * strobe * 255), 
                            uint8_t(r * strobe * 255), 
                            uint8_t(a * strobe * 255) 
                        };
                        pixels.resize(description.width * description.height, colour);
                        outParameters.resize(1, float(strobe));
                        break;
                    }
                    case 1: // "Radar"
                    {
                        const float speed = values.get<float>("stable_shared_key_speed");
                        const float length = values.get<float>("stable_key_length");
                        const bool left = !values.get<float>("stable_key_direction");
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

                SenderFrame data;
                data.type = RS_FRAMETYPE_HOST_MEMORY;
                data.cpu.stride = description.width * sizeof(Colour);
                data.cpu.data = reinterpret_cast<uint8_t*>(pixels.data());
                data.cpu.format = RS_FMT_BGRA8;

                FrameResponseData response = {};
                response.cameraData = &cameraData;
                response.schemaHash = scene.hash;
                response.parameterDataSize = uint32_t(outParameters.size() * sizeof(float));
                response.parameterData = outParameters.data();
                response.textDataCount = uint32_t(outTexts.size());
                response.textData = outTexts.data();
                rs.sendFrame(description.handle, data, response);
            }
        }
    }

    return 0;
}

int main(int argc, char** argv)
{
    try
    {
        return mainImpl(argc, argv);
    }
    catch (const std::exception& e)
    {
        tcerr << "Error: " << e.what() << std::endl;
        return 99;
    }
}
