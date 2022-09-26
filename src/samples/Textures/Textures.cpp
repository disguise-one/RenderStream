// A simple RenderStream application that receives and sends back textures using DX11
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <shlwapi.h>
#include <tchar.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <unordered_map>

// auto-generated from hlsl
#include "Generated_Code/VertexShader.h"
#include "Generated_Code/PixelShader.h"

#include "../../include/d3renderstream.h"
#include "../../include/d3helpers.hpp"

#if defined(UNICODE) || defined(_UNICODE)
#define tcout std::wcout
#define tcerr std::wcerr
#else
#define tcout std::cout
#define tcerr std::cerr
#endif

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

DXGI_FORMAT toDxgiFormat(RSPixelFormat format)
{
    switch (format)
    {
    case RS_FMT_BGRA8:
    case RS_FMT_BGRX8:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RS_FMT_RGBA32F:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RS_FMT_RGBA16:
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    default:
        throw std::runtime_error("Unhandled RS pixel format");
    }
}

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;
};

static constexpr Vertex cubeVertices[] =
{
    // -X face
    { DirectX::XMFLOAT3(-0.5f, -0.5f, 0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
    { DirectX::XMFLOAT3(-0.5f, 0.5f, 0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
    { DirectX::XMFLOAT3(-0.5f, 0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
    { DirectX::XMFLOAT3(-0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
    // +X face
    { DirectX::XMFLOAT3(0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
    { DirectX::XMFLOAT3(0.5f, 0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
    { DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
    { DirectX::XMFLOAT3(0.5f, -0.5f, 0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
    // -Y face
    { DirectX::XMFLOAT3(-0.5f, -0.5f, 0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
    { DirectX::XMFLOAT3(-0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
    { DirectX::XMFLOAT3(0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
    { DirectX::XMFLOAT3(0.5f, -0.5f, 0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
    // +Y face
    { DirectX::XMFLOAT3(-0.5f, 0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
    { DirectX::XMFLOAT3(-0.5f, 0.5f, 0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
    { DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
    { DirectX::XMFLOAT3(0.5f, 0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
    // -Z face
    { DirectX::XMFLOAT3(-0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
    { DirectX::XMFLOAT3(-0.5f, 0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
    { DirectX::XMFLOAT3(0.5f, 0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
    { DirectX::XMFLOAT3(0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
    // +Z face
    { DirectX::XMFLOAT3(0.5f, -0.5f, 0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
    { DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
    { DirectX::XMFLOAT3(-0.5f, 0.5f, 0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
    { DirectX::XMFLOAT3(-0.5f, -0.5f, 0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
};

static constexpr uint16_t cubeIndices[] =
{
    // -x
    0,1,2,
    2,3,0,

    // +x
    4,5,6,
    6,7,4,

    // -y
    8,9,10,
    10,11,8,

    // +y
    12,13,14,
    14,15,12,

    // -z
    16,17,18,
    18,19,16,

    // +z
    20,21,22,
    22,23,20,
};

static constexpr UINT cubeDrawCalls[] =
{
    36
};

struct ConstantBufferStruct 
{
    DirectX::XMMATRIX worldViewProjection;
};

struct Texture
{
    uint32_t width = 0;
    uint32_t height = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
};

Texture createTexture(ID3D11Device* device, ImageFrameData image)
{
    Texture texture;
    texture.width = image.width;
    texture.height = image.height;

    D3D11_TEXTURE2D_DESC rtDesc;
    ZeroMemory(&rtDesc, sizeof(D3D11_TEXTURE2D_DESC));
    rtDesc.Width = texture.width;
    rtDesc.Height = texture.height;
    rtDesc.MipLevels = 1;
    rtDesc.ArraySize = 1;
    rtDesc.Format = toDxgiFormat(image.format);
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Usage = D3D11_USAGE_DEFAULT;
    rtDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    rtDesc.CPUAccessFlags = 0;
    rtDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(device->CreateTexture2D(&rtDesc, nullptr, texture.resource.GetAddressOf())))
        throw std::runtime_error("Failed to create texture for image parameter");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    srvDesc.Format = rtDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = rtDesc.MipLevels;
    if (FAILED(device->CreateShaderResourceView(texture.resource.Get(), &srvDesc, texture.srv.GetAddressOf())))
        throw std::runtime_error("Failed to create shader resource view for image parameter");

    return texture;
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
    LOAD_FN(rs_initialiseGpGpuWithDX11Device);
    LOAD_FN(rs_saveSchema);
    LOAD_FN(rs_setSchema);
    LOAD_FN(rs_getStreams);
    LOAD_FN(rs_awaitFrameData);
    LOAD_FN(rs_getFrameParameters);
    LOAD_FN(rs_getFrameImageData);
    LOAD_FN(rs_getFrameImage2);
    LOAD_FN(rs_getFrameText);
    LOAD_FN(rs_getFrameCamera);
    LOAD_FN(rs_sendFrame2);
    LOAD_FN(rs_shutdown);
    LOAD_FN(rs_setNewStatusMessage);

    if (rs_initialise(RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to initialise RenderStream" << std::endl;
        return 3;
    }

#ifdef _DEBUG
    const uint32_t deviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
    const uint32_t deviceFlags = 0;
#endif
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf())))
    {
        tcerr << "Failed to initialise DirectX 11" << std::endl;
        rs_shutdown();
        return 4;
    }

    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    {
        CD3D11_BUFFER_DESC vertexDesc(sizeof(cubeVertices), D3D11_BIND_VERTEX_BUFFER);
        D3D11_SUBRESOURCE_DATA vertexData;
        ZeroMemory(&vertexData, sizeof(D3D11_SUBRESOURCE_DATA));
        vertexData.pSysMem = cubeVertices;
        vertexData.SysMemPitch = 0;
        vertexData.SysMemSlicePitch = 0;
        if (FAILED(device->CreateBuffer(&vertexDesc, &vertexData, vertexBuffer.GetAddressOf())))
        {
            tcerr << "Failed to initialise DirectX 11: vertex buffer" << std::endl;
            rs_shutdown();
            return 41;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    {
        CD3D11_BUFFER_DESC indexDesc(sizeof(cubeIndices), D3D11_BIND_INDEX_BUFFER);
        D3D11_SUBRESOURCE_DATA indexData;
        ZeroMemory(&indexData, sizeof(D3D11_SUBRESOURCE_DATA));
        indexData.pSysMem = cubeIndices;
        indexData.SysMemPitch = 0;
        indexData.SysMemSlicePitch = 0;
        if (FAILED(device->CreateBuffer(&indexDesc, &indexData, indexBuffer.GetAddressOf())))
        {
            tcerr << "Failed to initialise DirectX 11: index buffer" << std::endl;
            rs_shutdown();
            return 42;
        }
    }

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    {
        if (FAILED(device->CreateVertexShader(VertexShaderBlob, std::size(VertexShaderBlob), nullptr, vertexShader.GetAddressOf())))
        {
            tcerr << "Failed to initialise DirectX 11: vertex shader" << std::endl;
            rs_shutdown();
            return 43;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
    {
        D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = { 
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        if (FAILED(device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc), VertexShaderBlob, std::size(VertexShaderBlob), inputLayout.GetAddressOf())))
        {
            tcerr << "Failed to initialise DirectX 11: index buffer" << std::endl;
            rs_shutdown();
            return 44;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
    {
        if (FAILED(device->CreatePixelShader(PixelShaderBlob, std::size(PixelShaderBlob), nullptr, pixelShader.GetAddressOf())))
        {
            tcerr << "Failed to initialise DirectX 11: pixel shader" << std::endl;
            rs_shutdown();
            return 45;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
    {
        CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ConstantBufferStruct), D3D11_BIND_CONSTANT_BUFFER);
        if (FAILED(device->CreateBuffer(&constantBufferDesc, nullptr, constantBuffer.GetAddressOf())))
        {
            tcerr << "Failed to initialise DirectX 11: constant buffer" << std::endl;
            rs_shutdown();
            return 46;
        }
    }

    if (rs_initialiseGpGpuWithDX11Device(device.Get()) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to initialise RenderStream GPGPU interop" << std::endl;
        rs_shutdown();
        return 5;
    }

    ScopedSchema scoped; // C++ helper that cleans up mallocs and strdups
    scoped.schema.engineName = _strdup("Textures sample");
    scoped.schema.engineVersion = _strdup(("RS" + std::to_string(RENDER_STREAM_VERSION_MAJOR) + "." + std::to_string(RENDER_STREAM_VERSION_MINOR)).c_str());
    scoped.schema.info = _strdup("");
    scoped.schema.scenes.nScenes = 1;
    scoped.schema.scenes.scenes = static_cast<RemoteParameters*>(malloc(scoped.schema.scenes.nScenes * sizeof(RemoteParameters)));
    scoped.schema.scenes.scenes[0].name = _strdup("Default");
    scoped.schema.scenes.scenes[0].nParameters = 3;
    scoped.schema.scenes.scenes[0].parameters = static_cast<RemoteParameter*>(malloc(scoped.schema.scenes.scenes[0].nParameters * sizeof(RemoteParameter)));
    // Image parameter
    scoped.schema.scenes.scenes[0].parameters[0].group = _strdup("Cube");
    scoped.schema.scenes.scenes[0].parameters[0].key = _strdup("image_param1");
    scoped.schema.scenes.scenes[0].parameters[0].displayName = _strdup("Texture");
    scoped.schema.scenes.scenes[0].parameters[0].type = RS_PARAMETER_IMAGE;
    scoped.schema.scenes.scenes[0].parameters[0].nOptions = 0;
    scoped.schema.scenes.scenes[0].parameters[0].options = nullptr;
    scoped.schema.scenes.scenes[0].parameters[0].dmxOffset = -1; // Auto
    scoped.schema.scenes.scenes[0].parameters[0].dmxType = RS_DMX_16_BE;
    scoped.schema.scenes.scenes[0].parameters[0].flags = REMOTEPARAMETER_NO_FLAGS;
    // Transform parameter
    scoped.schema.scenes.scenes[0].parameters[1].group = _strdup("Cube");
    scoped.schema.scenes.scenes[0].parameters[1].key = _strdup("transform_param1");
    scoped.schema.scenes.scenes[0].parameters[1].displayName = _strdup("Transform");
    scoped.schema.scenes.scenes[0].parameters[1].type = RS_PARAMETER_TRANSFORM;
    scoped.schema.scenes.scenes[0].parameters[1].nOptions = 0;
    scoped.schema.scenes.scenes[0].parameters[1].options = nullptr;
    scoped.schema.scenes.scenes[0].parameters[1].dmxOffset = -1; // Auto
    scoped.schema.scenes.scenes[0].parameters[1].dmxType = RS_DMX_16_BE;
    scoped.schema.scenes.scenes[0].parameters[0].flags = REMOTEPARAMETER_NO_SEQUENCE;
    // Text parameter
    scoped.schema.scenes.scenes[0].parameters[2].group = _strdup("Workload status");
    scoped.schema.scenes.scenes[0].parameters[2].key = _strdup("text_param1");
    scoped.schema.scenes.scenes[0].parameters[2].displayName = _strdup("Text");
    scoped.schema.scenes.scenes[0].parameters[2].type = RS_PARAMETER_TEXT;
    scoped.schema.scenes.scenes[0].parameters[2].defaults.text.defaultValue = _strdup("All systems operational");
    scoped.schema.scenes.scenes[0].parameters[2].nOptions = 0;
    scoped.schema.scenes.scenes[0].parameters[2].options = nullptr;
    scoped.schema.scenes.scenes[0].parameters[2].dmxOffset = -1; // Auto
    scoped.schema.scenes.scenes[0].parameters[2].dmxType = RS_DMX_16_BE;
    scoped.schema.scenes.scenes[0].parameters[0].flags = REMOTEPARAMETER_NO_FLAGS;
    if (rs_setSchema(&scoped.schema) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to set schema" << std::endl;
        return 6;
    }

    // Saving the schema to disk makes the remote parameters available in d3's UI before the application is launched
    if (rs_saveSchema(argv[0], &scoped.schema) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to save schema" << std::endl;
        return 61;
    }
    std::vector<uint8_t> descMem;
    const StreamDescriptions* header = nullptr;
    struct RenderTarget
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
    };
    std::unordered_map<StreamHandle, RenderTarget> renderTargets;
    Texture texture;
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
                // Create render targets for all streams
                const size_t numStreams = header ? header->nStreams : 0;
                for (size_t i = 0; i < numStreams; ++i)
                {
                    const StreamDescription& description = header->streams[i];
                    RenderTarget& target = renderTargets[description.handle];

                    D3D11_TEXTURE2D_DESC rtDesc;
                    ZeroMemory(&rtDesc, sizeof(D3D11_TEXTURE2D_DESC));
                    rtDesc.Width = description.width;
                    rtDesc.Height = description.height;
                    rtDesc.MipLevels = 1;
                    rtDesc.ArraySize = 1;
                    rtDesc.Format = toDxgiFormat(description.format);
                    rtDesc.SampleDesc.Count = 1;
                    rtDesc.Usage = D3D11_USAGE_DEFAULT;
                    rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                    rtDesc.CPUAccessFlags = 0;
                    rtDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                    if (FAILED(device->CreateTexture2D(&rtDesc, nullptr, target.texture.GetAddressOf())))
                        throw std::runtime_error("Failed to create render target texture for stream");

                    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
                    ZeroMemory(&rtvDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
                    rtvDesc.Format = rtDesc.Format;
                    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    if (FAILED(device->CreateRenderTargetView(target.texture.Get(), &rtvDesc, target.view.GetAddressOf())))
                        throw std::runtime_error("Failed to create render target view for stream");

                    D3D11_TEXTURE2D_DESC dsDesc;
                    ZeroMemory(&dsDesc, sizeof(D3D11_TEXTURE2D_DESC));
                    dsDesc.Width = description.width;
                    dsDesc.Height = description.height;
                    dsDesc.MipLevels = 1;
                    dsDesc.ArraySize = 1;
                    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                    dsDesc.SampleDesc.Count = 1;
                    dsDesc.Usage = D3D11_USAGE_DEFAULT;
                    dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                    dsDesc.CPUAccessFlags = 0;
                    if (FAILED(device->CreateTexture2D(&dsDesc, nullptr, target.depth.GetAddressOf())))
                        throw std::runtime_error("Failed to create depth texture for stream");

                    D3D11_DEPTH_STENCIL_VIEW_DESC  dsvDesc;
                    ZeroMemory(&dsvDesc, sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC));
                    dsvDesc.Format = dsDesc.Format;
                    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                    if (FAILED(device->CreateDepthStencilView(target.depth.Get(), &dsvDesc, target.depthView.GetAddressOf())))
                        throw std::runtime_error("Failed to create depth view for stream");
                }
            }
            catch (const std::exception& e)
            {
                tcerr << e.what() << std::endl;
                rs_shutdown();
                return 7;
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

        ImageFrameData image;
        if (rs_getFrameImageData(scene.hash, &image, 1) != RS_ERROR_SUCCESS)
        {
            tcerr << "Failed to get image parameter data" << std::endl;
            continue;
        }
        if (texture.width != image.width || texture.height != image.height)
        {
            texture = createTexture(device.Get(), image);
        }
        SenderFrame data;
        data.type = RS_FRAMETYPE_DX11_TEXTURE;
        data.dx11.resource = texture.resource.Get();
        if (rs_getFrameImage2(image.imageId, &data) != RS_ERROR_SUCCESS)
        {
            tcerr << "Failed to get image parameter" << std::endl;
            continue;
        }

        DirectX::XMMATRIX transform;
        static_assert(sizeof(transform) == 4 * 4 * sizeof(float), "4x4 matrix");
        if (rs_getFrameParameters(scene.hash, &transform, sizeof(transform)) != RS_ERROR_SUCCESS)
        {
            tcerr << "Failed to get transform parameter" << std::endl;
            continue;
        }

        const char* text = nullptr;
        if (rs_getFrameText(scene.hash, 0, &text) != RS_ERROR_SUCCESS)
        {
            tcerr << "Failed to get text parameter" << std::endl;
            continue;
        }
        if (rs_setNewStatusMessage(text) != RS_ERROR_SUCCESS)
        {
            tcerr << "Failed to set status message" << std::endl;
            continue;
        }

        // Respond to frame request
        const size_t numStreams = header ? header->nStreams : 0;
        for (size_t i = 0; i < numStreams; ++i)
        {
            const StreamDescription& description = header->streams[i];

            CameraResponseData cameraData;
            cameraData.tTracked = frameData.tTracked;
            if (rs_getFrameCamera(description.handle, &cameraData.camera) == RS_ERROR_SUCCESS)
            {
                const RenderTarget& target = renderTargets.at(description.handle);
                context->OMSetRenderTargets(1, target.view.GetAddressOf(), target.depthView.Get());

                const float clearColour[4] = { 0.f, 0.f, 0.f, 0.f };
                context->ClearRenderTargetView(target.view.Get(), clearColour);
                context->ClearDepthStencilView(target.depthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

                D3D11_VIEWPORT viewport;
                ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
                viewport.Width = static_cast<float>(description.width);
                viewport.Height = static_cast<float>(description.height);
                viewport.MinDepth = 0;
                viewport.MaxDepth = 1;
                context->RSSetViewports(1, &viewport);

                ConstantBufferStruct constantBufferData;
                const DirectX::XMMATRIX world = transform;

                const float pitch = -DirectX::XMConvertToRadians(cameraData.camera.rx);
                const float yaw = DirectX::XMConvertToRadians(cameraData.camera.ry);
                const float roll = -DirectX::XMConvertToRadians(cameraData.camera.rz);

                const DirectX::XMMATRIX cameraTranslation = DirectX::XMMatrixTranslation(cameraData.camera.x, cameraData.camera.y, cameraData.camera.z);
                const DirectX::XMMATRIX cameraRotation = DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
                const DirectX::XMMATRIX view = DirectX::XMMatrixInverse(nullptr, cameraTranslation) * DirectX::XMMatrixTranspose(cameraRotation);

                const float throwRatioH = cameraData.camera.focalLength / cameraData.camera.sensorX;
                const float throwRatioV = cameraData.camera.focalLength / cameraData.camera.sensorY;
                const float fovH = 2.0f * atan(0.5f / throwRatioH);
                const float fovV = 2.0f * atan(0.5f / throwRatioV);

                const bool orthographic = cameraData.camera.orthoWidth > 0.0f;
                const float cameraAspect = cameraData.camera.sensorX / cameraData.camera.sensorY;
                float imageHeight, imageWidth;
                if (orthographic)
                {
                    imageHeight = cameraData.camera.orthoWidth / cameraAspect;
                    imageWidth = cameraAspect * imageHeight;
                }
                else
                {
                    imageWidth = 2.0f * tan(0.5f * fovH);
                    imageHeight = 2.0f * tan(0.5f * fovV);
                }

                const DirectX::XMMATRIX overscan = DirectX::XMMatrixTranslation(cameraData.camera.cx, cameraData.camera.cy, 0.f);

                const float nearZ = cameraData.camera.nearZ;
                const float farZ = cameraData.camera.farZ;

                const float l = (-0.5f + description.clipping.left) * imageWidth;
                const float r = (-0.5f + description.clipping.right) * imageWidth;
                const float t = (-0.5f + 1.f - description.clipping.top) * imageHeight;
                const float b = (-0.5f + 1.f - description.clipping.bottom) * imageHeight;

                const DirectX::XMMATRIX projection = orthographic ? DirectX::XMMatrixOrthographicOffCenterLH(l, r, b, t, nearZ, farZ) : DirectX::XMMatrixPerspectiveOffCenterLH(l * nearZ, r * nearZ, b * nearZ, t * nearZ, nearZ, farZ);

                constantBufferData.worldViewProjection = DirectX::XMMatrixTranspose(world * view * projection * overscan);
                context->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &constantBufferData, 0, 0);

                // Draw cube
                UINT stride = sizeof(Vertex);
                UINT offset = 0;
                context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
                context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->IASetInputLayout(inputLayout.Get());
                context->VSSetShader(vertexShader.Get(), nullptr, 0);
                context->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
                context->PSSetShader(pixelShader.Get(), nullptr, 0);
                context->PSSetShaderResources(0, 1, texture.srv.GetAddressOf());
                UINT startIndex = 0;
                for (UINT indexCount : cubeDrawCalls)
                {
                    context->DrawIndexed(indexCount, startIndex, 0);
                    startIndex += indexCount;
                }

                SenderFrame data;
                data.type = RS_FRAMETYPE_DX11_TEXTURE;
                data.dx11.resource = target.texture.Get();

                FrameResponseData response = {};
                response.cameraData = &cameraData;
                if (rs_sendFrame2(description.handle, &data, &response) != RS_ERROR_SUCCESS)
                {
                    tcerr << "Failed to send frame" << std::endl;
                    rs_shutdown();
                    return 8;
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
