#pragma once

#include "d3renderstream.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX

#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#include <vector>
#include <variant>
#include <string>
#include <array>

#pragma comment(lib, "Shlwapi.lib")

#ifndef RS_LOG
#define RS_LOG(streamexpr) std::cerr << streamexpr << std::endl
#endif

#define DECL_FN(FUNC_NAME) decltype(rs_ ## FUNC_NAME)* m_ ## FUNC_NAME = nullptr

#define LOAD_FN(FUNC_NAME) \
    m_ ## FUNC_NAME = reinterpret_cast<decltype(rs_ ## FUNC_NAME) *>(GetProcAddress(m_rsDll, "rs_" #FUNC_NAME)); \
    if (!m_ ## FUNC_NAME) { \
        throw std::runtime_error("Failed to get function " #FUNC_NAME " from DLL"); \
    }

class RenderStreamError : public std::runtime_error
{
public:
    RenderStreamError(RS_ERROR err, const std::string& what) : std::runtime_error(what), error(err) { }

    RS_ERROR error;
};

inline void checkRs(RS_ERROR err, const char* context)
{
    if (err != RS_ERROR_SUCCESS)
        throw RenderStreamError(err, std::string("Error calling ") + context + " - " + std::to_string(err));
}


class ParameterValues
{
public:
    inline ParameterValues(class RenderStream& rs, const RemoteParameters& scene);

    template <typename T>
    T get(const std::string& key);

private:
    inline std::tuple<size_t, RemoteParameterType> iKey(const std::string& key);

    class RenderStream* m_rs;
    const RemoteParameters* m_parameters;
    std::vector<float> m_floatValues;
    std::vector<ImageFrameData> m_imageValues;
    std::vector<const char*> m_textValues;
};


// RenderStream wrapper class to load and interact with disguise RenderStream.
class RenderStream
{
public:
    inline RenderStream();
    inline ~RenderStream();

    inline void initialise();

    inline void initialiseGpGpuWithDX11Device(ID3D11Device* device);
    inline void initialiseGpGpuWithDX11Resource(ID3D11Resource* resource);
    inline void initialiseGpGpuWithDX12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    inline void initialiseGpGpuWithOpenGlContexts(HGLRC glContext, HDC deviceContext);
    inline void initialiseGpGpuWithoutInterop();

    inline const Schema* loadSchema(const char* assetPath);
    inline void saveSchema(const char* assetPath, Schema* schema);
    inline void setSchema(Schema* schema);

    inline ParameterValues getFrameParameters(const RemoteParameters& scene);
    inline void getFrameImage(int64_t imageId, const SenderFrame& frame);

    inline std::variant<FrameData, RS_ERROR> awaitFrameData(int timeoutMs);

    inline const StreamDescriptions* getStreams();

    inline CameraData getFrameCamera(StreamHandle stream);

    inline void sendFrame(StreamHandle stream, const SenderFrame& frame, const FrameResponseData& response);

    inline void setNewStatusMessage(const char* message);

private:
    friend class ParameterValues; // uses the various low level parameter accessors
    HMODULE m_rsDll;
    std::vector<uint8_t> m_streamDescriptionsMemory;
    std::vector<uint8_t> m_schemaMemory;

    DECL_FN(initialise);
    DECL_FN(initialiseGpGpuWithDX11Device);
    DECL_FN(initialiseGpGpuWithDX11Resource);
    DECL_FN(initialiseGpGpuWithDX12DeviceAndQueue);
    DECL_FN(initialiseGpGpuWithOpenGlContexts);
    DECL_FN(initialiseGpGpuWithoutInterop);
    DECL_FN(loadSchema);
    DECL_FN(saveSchema);
    DECL_FN(setSchema);
    DECL_FN(getStreams);
    DECL_FN(getFrameParameters);
    DECL_FN(getFrameImageData);
    DECL_FN(getFrameText);
    DECL_FN(getFrameImage2);
    DECL_FN(awaitFrameData);
    DECL_FN(getFrameCamera);
    DECL_FN(sendFrame2);
    DECL_FN(setNewStatusMessage);
    DECL_FN(shutdown);
};

RenderStream::RenderStream()
    : m_rsDll(nullptr)
{
}

RenderStream::~RenderStream()
{
    checkRs(m_shutdown(), __FUNCTION__);
}

void RenderStream::initialise()
{
    HKEY hKey;
    if (FAILED(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\d3 Technologies\\d3 Production Suite", 0, KEY_READ, &hKey)))
    {
        throw std::runtime_error("Failed to open 'Software\\d3 Technologies\\d3 Production Suite' registry key");
    }

    CHAR buffer[512];
    DWORD bufferSize = sizeof(buffer);
    if (FAILED(RegQueryValueExA(hKey, "exe path", 0, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize)))
    {
        throw std::runtime_error("Failed to query value of 'exe path'");
    }

    if (!PathRemoveFileSpecA(buffer))
    {
        throw std::runtime_error(std::string("Failed to remove file spec from path: '") + buffer + "'");
    }

    if (strcat_s(buffer, "\\d3renderstream.dll") != 0)
    {
        throw std::runtime_error(std::string("Failed to append filename to path: '") + buffer + "'");
    }

    m_rsDll = ::LoadLibraryExA(buffer, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!m_rsDll)
    {
        throw std::runtime_error(std::string("Failed to load dll: '") + buffer + "'");
    }

    LOAD_FN(initialise);
    LOAD_FN(initialiseGpGpuWithDX11Device);
    LOAD_FN(initialiseGpGpuWithDX11Resource);
    LOAD_FN(initialiseGpGpuWithDX12DeviceAndQueue);
    LOAD_FN(initialiseGpGpuWithOpenGlContexts);
    LOAD_FN(initialiseGpGpuWithoutInterop);
    LOAD_FN(loadSchema);
    LOAD_FN(saveSchema);
    LOAD_FN(setSchema);
    LOAD_FN(getFrameParameters);
    LOAD_FN(getFrameImageData);
    LOAD_FN(getFrameText);
    LOAD_FN(getFrameImage2);
    LOAD_FN(getStreams);
    LOAD_FN(awaitFrameData);
    LOAD_FN(getFrameCamera);
    LOAD_FN(sendFrame2);
    LOAD_FN(setNewStatusMessage);
    LOAD_FN(shutdown);

    checkRs(m_initialise(RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR), "initialise");
}

void RenderStream::initialiseGpGpuWithDX11Device(ID3D11Device* device)
{
    checkRs(m_initialiseGpGpuWithDX11Device(device), __FUNCTION__);
}

void RenderStream::initialiseGpGpuWithDX11Resource(ID3D11Resource* resource)
{
    checkRs(m_initialiseGpGpuWithDX11Resource(resource), __FUNCTION__);
}

void RenderStream::initialiseGpGpuWithDX12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    checkRs(m_initialiseGpGpuWithDX12DeviceAndQueue(device, queue), __FUNCTION__);
}

void RenderStream::initialiseGpGpuWithOpenGlContexts(HGLRC glContext, HDC deviceContext)
{
    checkRs(m_initialiseGpGpuWithOpenGlContexts(glContext, deviceContext), __FUNCTION__);
}

void RenderStream::initialiseGpGpuWithoutInterop()
{
    // the parameter to this method was a mistake in the ABI.
    checkRs(m_initialiseGpGpuWithoutInterop(nullptr), __FUNCTION__);
}

const Schema* RenderStream::loadSchema(const char* assetPath)
{
    uint32_t nBytes = 0;
    m_loadSchema(assetPath, nullptr, &nBytes);

    const static int MAX_TRIES = 3;
    int iterations = 0;

    RS_ERROR res = RS_ERROR_BUFFER_OVERFLOW;
    do
    {
        m_schemaMemory.resize(nBytes);
        res = m_loadSchema(assetPath, reinterpret_cast<Schema*>(m_schemaMemory.data()), &nBytes);

        if (res == RS_ERROR_SUCCESS)
            break;

        ++iterations;
    } while (res == RS_ERROR_BUFFER_OVERFLOW && iterations < MAX_TRIES);

    if (res != RS_ERROR_SUCCESS)
        throw std::runtime_error("Failed to load schema");

    if (nBytes < sizeof(Schema))
        throw std::runtime_error("Invalid schema");

    return reinterpret_cast<const Schema*>(m_schemaMemory.data());
}

void RenderStream::saveSchema(const char* assetPath, Schema* schema)
{
    checkRs(m_saveSchema(assetPath, schema), __FUNCTION__);
}

void RenderStream::setSchema(Schema* schema)
{
    checkRs(m_setSchema(schema), __FUNCTION__);
}

ParameterValues RenderStream::getFrameParameters(const RemoteParameters& scene)
{
    return ParameterValues(*this, scene);
}

void RenderStream::getFrameImage(int64_t imageId, const SenderFrame& frame)
{
    checkRs(m_getFrameImage2(imageId, &frame), __FUNCTION__);
}

std::variant<FrameData, RS_ERROR> RenderStream::awaitFrameData(int timeoutMs)
{
    FrameData out;
    RS_ERROR err = m_awaitFrameData(timeoutMs, &out);
    if (err == RS_ERROR_SUCCESS)
        return out;
    else
        return err;
}

const StreamDescriptions* RenderStream::getStreams()
{
    uint32_t nBytes = 0;
    m_getStreams(nullptr, &nBytes);

    const static int MAX_TRIES = 3;
    int iterations = 0;

    RS_ERROR res = RS_ERROR_BUFFER_OVERFLOW;
    do
    {
        m_streamDescriptionsMemory.resize(nBytes);
        res = m_getStreams(reinterpret_cast<StreamDescriptions*>(m_streamDescriptionsMemory.data()), &nBytes);

        if (res == RS_ERROR_SUCCESS)
            break;

        ++iterations;
    } while (res == RS_ERROR_BUFFER_OVERFLOW && iterations < MAX_TRIES);

    if (res != RS_ERROR_SUCCESS)
        throw std::runtime_error("Failed to get streams");

    if (nBytes < sizeof(StreamDescriptions))
        throw std::runtime_error("Invalid stream descriptions");

    return reinterpret_cast<const StreamDescriptions*>(m_streamDescriptionsMemory.data());
}

CameraData RenderStream::getFrameCamera(StreamHandle stream)
{
    CameraData out;
    checkRs(m_getFrameCamera(stream, &out), __FUNCTION__);
    return out;
}

void RenderStream::sendFrame(StreamHandle stream, const SenderFrame& frame, const FrameResponseData& response)
{
    checkRs(m_sendFrame2(stream, &frame, &response), __FUNCTION__);
}

void RenderStream::setNewStatusMessage(const char* message)
{
    checkRs(m_setNewStatusMessage(message), __FUNCTION__);
}

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
                if (parameter.type == RS_PARAMETER_TEXT)
                    free(const_cast<char*>(parameter.defaults.text.defaultValue));
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

ParameterValues::ParameterValues(RenderStream& rs, const RemoteParameters& scene)
{
    m_rs = &rs;
    m_parameters = &scene;

    size_t nFloats = 0, nImages = 0, nTexts = 0;
    for (uint32_t iParam = 0; iParam < m_parameters->nParameters; ++iParam)
    {
        const RemoteParameter& param = m_parameters->parameters[iParam];

        if (param.flags & REMOTEPARAMETER_READ_ONLY)
            continue;

        if (param.type == RS_PARAMETER_NUMBER)
            nFloats++;
        else if (param.type == RS_PARAMETER_IMAGE)
            nImages++;
        else if (param.type == RS_PARAMETER_POSE)
            nFloats += 16;
        else if (param.type == RS_PARAMETER_TRANSFORM)
            nFloats += 16;
        else if (param.type == RS_PARAMETER_TEXT)
            nTexts++;
        else
            throw std::logic_error("Unhandled parameter type");
    }

    m_floatValues.resize(nFloats);
    checkRs(rs.m_getFrameParameters(m_parameters->hash, m_floatValues.data(), m_floatValues.size() * sizeof(float)), "get frame float data");

    m_imageValues.resize(nImages);
    checkRs(m_rs->m_getFrameImageData(m_parameters->hash, m_imageValues.data(), nImages), "get frame image data");
}

std::tuple<size_t, RemoteParameterType> ParameterValues::iKey(const std::string& key)
{
    size_t iFloat = 0, iImage = 0, iText = 0;
    for (uint32_t iParam = 0; iParam < m_parameters->nParameters; ++iParam)
    {
        const RemoteParameter& param = m_parameters->parameters[iParam];

        if (param.flags & REMOTEPARAMETER_READ_ONLY)
            continue;

        if (key == param.key)
        {
            if (param.type == RS_PARAMETER_NUMBER)
                return { iFloat, RS_PARAMETER_NUMBER };
            else if (param.type == RS_PARAMETER_IMAGE)
                return { iImage, RS_PARAMETER_IMAGE };
            else if (param.type == RS_PARAMETER_POSE)
                return { iFloat, RS_PARAMETER_POSE };
            else if (param.type == RS_PARAMETER_TRANSFORM)
                return { iFloat, RS_PARAMETER_TRANSFORM };
            else if (param.type == RS_PARAMETER_TEXT)
                return { iText, RS_PARAMETER_TEXT };
            else
                throw std::logic_error("Unhandled parameter type");
        }

        if (param.type == RS_PARAMETER_NUMBER)
            iFloat++;
        else if (param.type == RS_PARAMETER_IMAGE)
            iImage++;
        else if (param.type == RS_PARAMETER_POSE)
            iFloat += 16;
        else if (param.type == RS_PARAMETER_TRANSFORM)
            iFloat += 16;
        else if (param.type == RS_PARAMETER_TEXT)
            iText++;
        else
            throw std::logic_error("Unhandled parameter type");
    }

    throw std::runtime_error("Unknown key");
}


// No generic implementation - only specialisations
//template <typename T>
//T ParameterValues::get(const std::string& key)
//{
//}

template <>
inline float ParameterValues::get(const std::string& key)
{
    auto [index, type] = iKey(key);
    if (type != RS_PARAMETER_NUMBER)
        throw std::runtime_error("Key is not a number");
    return m_floatValues[index];
}

template <>
inline std::array<float, 16> ParameterValues::get(const std::string& key)
{
    auto [index, type] = iKey(key);
    if (type != RS_PARAMETER_TRANSFORM && type != RS_PARAMETER_POSE)
        throw std::runtime_error("Key is not a transform or pose");
    std::array<float, 16> out;
    std::copy(&m_floatValues[index], &m_floatValues[index + 16], out.begin());
    return out;
}

template <>
inline ImageFrameData ParameterValues::get(const std::string& key)
{
    auto [index, type] = iKey(key);
    if (type != RS_PARAMETER_IMAGE)
        throw std::runtime_error("Key is not an image");

    return m_imageValues[index];
}

template <>
inline const char* ParameterValues::get(const std::string& key)
{
    auto [index, type] = iKey(key);
    if (type != RS_PARAMETER_TEXT)
        throw std::runtime_error("Key is not a text param");

    const char* out;
    checkRs(m_rs->m_getFrameText(m_parameters->hash, uint32_t(index), &out), "getting text parameter");
    return out;
}
