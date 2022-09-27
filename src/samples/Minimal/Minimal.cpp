// A minimal RenderStream application that sends back a strobe using host memory
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3

#include <tchar.h>

#include "../../include/renderstream.hpp"

#if defined(UNICODE) || defined(_UNICODE)
#define tcout std::wcout
#define tcerr std::wcerr
#else
#define tcout std::cout
#define tcerr std::cerr
#endif

int mainImpl()
{
    RenderStream rs;
    rs.initialise();
    rs.initialiseGpGpuWithoutInterop();

    const StreamDescriptions* header = nullptr;
    while (true)
    {
        // Wait for a frame request
        std::variant<FrameData, RS_ERROR> awaitResult = rs.awaitFrameData(5000);
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

        // Respond to frame request
        const FrameData& frameData = std::get<FrameData>(awaitResult);
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
            catch(const RenderStreamError& e)
            {
                if (e.error == RS_ERROR_NOTFOUND)
                    continue;
                throw;
            }
            
            {
                const float strobe = float(abs(1.0 - fmod(frameData.tTracked, 2.0)));
                std::vector<uint8_t> pixel;
                switch (description.format)
                {
                case RS_FMT_BGRA8:
                case RS_FMT_BGRX8:
                case RS_FMT_RGBA8:
                case RS_FMT_RGBX8:
                {
                    pixel.resize(4 * sizeof(uint8_t), uint8_t(strobe * std::numeric_limits<uint8_t>::max()));
                    break;
                }
                case RS_FMT_RGBA32F:
                {
                    pixel.resize(4 * sizeof(float));
                    for (size_t i = 0; i < 4; ++i)
                        std::memcpy(pixel.data() + i * sizeof(float), &strobe, sizeof(strobe));
                    break;
                }
                case RS_FMT_RGBA16:
                {
                    const uint16_t strobe16 = uint16_t(strobe * std::numeric_limits<uint16_t>::max());
                    pixel.resize(4 * sizeof(uint16_t));
                    for (size_t i = 0; i < 4; ++i)
                        std::memcpy(pixel.data() + i * sizeof(uint16_t), &strobe16, sizeof(strobe16));
                    break;
                }
                default:
                    tcerr << "Unsupported pixel format" << std::endl;
                    continue;
                }

                SenderFrameTypeData data;
                data.cpu.stride = description.width * uint32_t(pixel.size());
                std::vector<uint8_t> pixels;
                pixels.reserve(data.cpu.stride * description.height);
                // Fill canvas with variable-sized pixels
                for (size_t i = 0; i < description.width * description.height; ++i)
                {
                    pixels.insert(pixels.end(), pixel.begin(), pixel.end());
                }

                data.cpu.data = pixels.data();

                FrameResponseData response = {};
                response.cameraData = &cameraData;
                rs.sendFrame(description.handle, RS_FRAMETYPE_HOST_MEMORY, data, &response);
            }
        }
    }

    return 0;
}

int main()
{
    try
    {
        return mainImpl();
    }
    catch (const std::exception& e)
    {
        tcerr << "Error: " << e.what() << std::endl;
        return 99;
    }
}
