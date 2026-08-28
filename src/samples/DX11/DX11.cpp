// A RenderStream application that sends back a 3D scene using a DX11 texture, with an audio test
// signal alongside it for identifying which output is which
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3
//
// Each channel gets its own slot and its own tone, so only one channel is ever sounding: an output
// is identified by when it beeps, by its pitch, and by the channel number drawn on the frame while
// its slot is active. Paced for a person walking the outputs - the tone for the first part of a slot
// and silence for the rest, leaving a clear gap before the next channel.
//
// The channel count and the sample rate are remote parameters, so both can be changed from d3 while
// running. Note this changes only what the engine sends: d3 fixes the stream's own channel count and
// rate when the stream is created, from the audio output device the workload is pointed at, so
// deliberately mismatching them is part of what this asset is for.

#include <vector>
#include <d3d11.h>
#include <d3d11_1.h>   // ID3D11DeviceContext1::ClearView, used to draw the channel number
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdint>

// auto-generated from hlsl
#include "Generated_Code/VertexShader.h"
#include "Generated_Code/PixelShader.h"

#include "../../include/renderstream.hpp"

#define LOG(streamexpr) std::cerr << streamexpr << std::endl

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Get streams into (descMem) buffer and return a pointer into it
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

namespace audio
{
    constexpr uint32_t channelsDefault = 4;
    // ffmpeg's AAC encoder carries up to 16, but this test caps at 8: the cue draws the channel
    // number as a single digit, and a full pass stays 4 seconds at 0.5s slots.
    constexpr uint32_t channelsMax = 8;
    // Rates offered by the sample rate parameter; its value is an index into this list, so the order
    // is what d3 stores and must not be shuffled. AAC carries all three.
    constexpr uint32_t sampleRates[] = { 44100, 48000, 96000 };
    constexpr uint32_t sampleRateDefaultIndex = 1;
    // Time given to each channel. A fixed length, so every channel gets the same tone and the same gap.
    constexpr double slotSeconds = 0.5;
    constexpr double beepSeconds = 0.2;  // nominal tone length within a slot
    static_assert(beepSeconds < slotSeconds, "the beep must leave a gap before the next channel");
    constexpr double toneBaseHz = 440.0;
    constexpr double toneStepHz = 220.0;
    // Tone level in int16 sample units: 8000 of 32767 full scale, about -12 dBFS. Loud enough to hear
    // on a desk monitor and clear of clipping - but hotter than a line-up tone (-18 dBFS EBU), so not
    // a level reference.
    constexpr int16_t toneAmplitude{ 8000 };

    // Slots are measured in samples, so the audio and the visual cue index them the same way. Runtime
    // rather than constant now the rate is a parameter, and a whole number of samples at every rate
    // offered - slotAt divides by it, and a fractional slot would drift the tone against the flash.
    static uint64_t samplesPerSlot(uint32_t sampleRate)
    {
        return uint64_t(sampleRate * slotSeconds);
    }

    constexpr bool slotsAreWholeSamples()
    {
        for (uint32_t rate : sampleRates)
        {
            if (double(uint64_t(rate * slotSeconds)) != rate * slotSeconds)
                return false;
        }
        return true;
    }
    static_assert(slotsAreWholeSamples(), "every offered rate must give a whole number of samples per slot");

    // The state of the test signal at one sample. Slots are a fixed length, tile the timeline and are
    // handed out round-robin, so this is the single place both the tone and the visual cue come from.
    struct Slot
    {
        uint32_t channel;    // the one channel sounding in this slot
        double hz;           // its tone
        double timeInSlot;   // elapsed time since the slot began
        double beepSeconds;  // tone length within the slot; silence after this

        bool sounding() const { return timeInSlot < beepSeconds; }
    };

    static Slot slotAt(uint64_t sample, uint32_t channels, uint32_t sampleRate)
    {
        const uint64_t perSlot = samplesPerSlot(sampleRate);
        const uint32_t channel = uint32_t(sample / perSlot % channels);
        const double hz = toneBaseHz + toneStepHz * channel;
        // The nominal beepSeconds rounded down to a whole number of cycles of this channel's own tone,
        // so it starts and ends on a zero crossing rather than a click. Every channel is given the same
        // nominal length; only this rounding differs between them.
        const double beep = std::max<double>(1.0, std::floor(beepSeconds * hz)) / hz;
        return { channel, hz, double(sample % perSlot) / sampleRate, beep };
    }
}

// Remote parameter keys. A key is the identity d3 binds stored values, sequencing and DMX mappings
// to, so it must not change once a project uses it - display names can be reworded freely.
namespace keys
{
    constexpr const char* audioChannels = "stable_key_audio_channels";
    constexpr const char* sampleRate = "stable_key_sample_rate";
}

// The frame's background colour.
static constexpr float backgroundGreen[4] = { 0.f, 0.2f, 0.f, 0.f };

static void buildSchema(ScopedSchema& scoped)
{
    scoped.schema.engineName = _strdup("DX11 audio sample");
    scoped.schema.engineVersion = _strdup(("RS" + std::to_string(RENDER_STREAM_VERSION_MAJOR) + "." + std::to_string(RENDER_STREAM_VERSION_MINOR)).c_str());
    scoped.schema.info = _strdup("");

    scoped.schema.channels.nChannels = 1;
    scoped.schema.channels.channels = static_cast<const char**>(malloc(sizeof(const char*)));
    scoped.schema.channels.channels[0] = _strdup("Default");

    scoped.schema.scenes.nScenes = 1;
    scoped.schema.scenes.scenes = static_cast<RemoteParameters*>(malloc(sizeof(RemoteParameters)));
    RemoteParameters& scene = scoped.schema.scenes.scenes[0];
    scene.name = _strdup("Audio test");
    scene.nParameters = 2;
    scene.parameters = static_cast<RemoteParameter*>(malloc(scene.nParameters * sizeof(RemoteParameter)));

    RemoteParameter& channels = scene.parameters[0];
    channels.group = _strdup("Audio");
    channels.displayName = _strdup("Audio channels");
    channels.key = _strdup(keys::audioChannels);
    channels.type = RS_PARAMETER_NUMBER;
    channels.defaults.number.defaultValue = float(audio::channelsDefault);
    channels.defaults.number.min = 1.f;
    channels.defaults.number.max = float(audio::channelsMax);
    channels.defaults.number.step = 1.f;
    channels.nOptions = 0;
    channels.options = nullptr;
    channels.dmxOffset = -1; // Auto
    channels.dmxType = RS_DMX_16_BE;
    channels.flags = REMOTEPARAMETER_NO_FLAGS;

    // A list parameter: what d3 sends back is an index into audio::sampleRates, which is why the
    // options are the rates as text.
    RemoteParameter& rate = scene.parameters[1];
    rate.group = _strdup("Audio");
    rate.displayName = _strdup("Sample rate");
    rate.key = _strdup(keys::sampleRate);
    rate.type = RS_PARAMETER_NUMBER;
    rate.defaults.number.defaultValue = float(audio::sampleRateDefaultIndex);
    rate.defaults.number.min = 0.f;
    rate.defaults.number.max = float(std::size(audio::sampleRates) - 1);
    rate.defaults.number.step = 1.f;
    rate.nOptions = uint32_t(std::size(audio::sampleRates));
    rate.options = static_cast<const char**>(malloc(rate.nOptions * sizeof(const char*)));
    for (uint32_t i = 0; i < rate.nOptions; ++i)
        rate.options[i] = _strdup(std::to_string(audio::sampleRates[i]).c_str());
    rate.dmxOffset = -1; // Auto
    rate.dmxType = RS_DMX_16_BE;
    rate.flags = REMOTEPARAMETER_NO_FLAGS;
}

// The on-frame cue: the number of the channel that is sounding, and a flash while it sounds.
namespace cue
{
    // Used for both the beep flash and the channel number.
    constexpr float white[4] = { 1.f, 1.f, 1.f, 1.f };
    // Side of the flash square as a fraction of the frame's shorter edge.
    constexpr double flashSquareFraction = 0.25;

    // 3x5 glyphs for the digits 0 to 9, one byte per row, low 3 bits being the pixels left to right.
    constexpr uint8_t digitGlyphs[10][5] =
    {
        { 0b111, 0b101, 0b101, 0b101, 0b111 },
        { 0b010, 0b110, 0b010, 0b010, 0b111 },
        { 0b111, 0b001, 0b111, 0b100, 0b111 },
        { 0b111, 0b001, 0b111, 0b001, 0b111 },
        { 0b101, 0b101, 0b111, 0b001, 0b001 },
        { 0b111, 0b100, 0b111, 0b001, 0b111 },
        { 0b111, 0b100, 0b111, 0b101, 0b111 },
        { 0b111, 0b001, 0b001, 0b001, 0b001 },
        { 0b111, 0b101, 0b111, 0b101, 0b111 },
        { 0b111, 0b101, 0b111, 0b001, 0b111 },
    };

    static_assert(audio::channelsMax < 10, "the channel number is drawn as a single digit");

    // Draw the frame's cue over the scene: the sounding channel's number in the top-left corner, and
    // while it is sounding a square in the middle of the frame
    static void draw(ID3D11DeviceContext1* context, ID3D11RenderTargetView* view, uint32_t channel,
                     bool sounding, uint32_t frameWidth, uint32_t frameHeight)
    {
        if (!context)
            return;

        // Scaled off the shorter edge, not the height: on a portrait mapping a height-derived glyph
        // grows wide enough to reach the flash square in the middle of the frame.
        const LONG shorterEdge = LONG(std::min<uint32_t>(frameWidth, frameHeight));
        const LONG pixel = std::max<LONG>(2, shorterEdge / 16); // one glyph pixel
        const LONG margin = pixel;

        std::vector<D3D11_RECT> rects;
        for (LONG row = 0; row < 5; ++row)
        {
            for (LONG col = 0; col < 3; ++col)
            {
                if ((digitGlyphs[channel][row] & (0b100 >> col)) == 0)
                    continue;

                rects.push_back({ margin + col * pixel, margin + row * pixel,
                                  margin + (col + 1) * pixel, margin + (row + 1) * pixel });
            }
        }

        const LONG side = LONG(shorterEdge * flashSquareFraction);
        if (sounding && side > 0)
        {
            const LONG left = LONG(frameWidth) / 2 - side / 2;
            const LONG top = LONG(frameHeight) / 2 - side / 2;
            rects.push_back({ left, top, left + side, top + side });
        }

        context->ClearView(view, white, rects.data(), UINT(rects.size()));
    }
}

static constexpr DirectX::XMFLOAT3 cubeVertices[] =
{
    DirectX::XMFLOAT3(-0.5f, 0.5f,-0.5f),
    DirectX::XMFLOAT3(0.5f, 0.5f,-0.5f),
    DirectX::XMFLOAT3(-0.5f,-0.5f,-0.5f),
    DirectX::XMFLOAT3( 0.5f,-0.5f,-0.5f),

    DirectX::XMFLOAT3(-0.5f, 0.5f, 0.5f),
    DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f),
    DirectX::XMFLOAT3(-0.5f,-0.5f, 0.5f),
    DirectX::XMFLOAT3( 0.5f,-0.5f, 0.5f),
};

static constexpr uint16_t cubeIndices[] =
{
    0, 1, 2,    // side 1
    2, 1, 3,
    4, 0, 6,    // side 2
    6, 0, 2,
    7, 5, 6,    // side 3
    6, 5, 4,
    3, 1, 7,    // side 4
    7, 1, 5,
    4, 5, 0,    // side 5
    0, 5, 1,
    3, 7, 2,    // side 6
    2, 7, 6,
};


static constexpr UINT cubeDrawCalls[] =
{
    36
};



struct ConstantBufferStruct 
{
    DirectX::XMMATRIX worldViewProjection;
};

int mainImpl()
{
    RenderStream rs;

    rs.initialise();
    LOG("RenderStream initialised - program starting");

#ifdef _DEBUG
    const uint32_t deviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
    const uint32_t deviceFlags = 0;
#endif
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf())))
    {
        LOG("Failed to initialise DirectX 11");
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
            LOG("Failed to initialise DirectX 11: vertex buffer");
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
            LOG("Failed to initialise DirectX 11: index buffer");
            return 42;
        }
    }

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    {
        if (FAILED(device->CreateVertexShader(VertexShaderBlob, std::size(VertexShaderBlob), nullptr, vertexShader.GetAddressOf())))
        {
            LOG("Failed to initialise DirectX 11: vertex shader");
            return 43;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
    {
        D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };

        if (FAILED(device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc), VertexShaderBlob, std::size(VertexShaderBlob), inputLayout.GetAddressOf())))
        {
            LOG("Failed to initialise DirectX 11: index buffer");
            return 44;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
    {
        if (FAILED(device->CreatePixelShader(PixelShaderBlob, std::size(PixelShaderBlob), nullptr, pixelShader.GetAddressOf())))
        {
            LOG("Failed to initialise DirectX 11: pixel shader");
            return 45;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
    {
        CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ConstantBufferStruct), D3D11_BIND_CONSTANT_BUFFER);
        if (FAILED(device->CreateBuffer(&constantBufferDesc, nullptr, constantBuffer.GetAddressOf())))
        {
            LOG("Failed to initialise DirectX 11: constant buffer");
            return 46;
        }
    }

    ScopedSchema scoped;
    buildSchema(scoped);
    rs.setSchema(&scoped.schema);
    {
        // Saving the schema to schema json: makes the parameter visible in d3's UI before the workload is launched.
        char assetPath[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, assetPath, MAX_PATH) > 0)
            rs.saveSchema(assetPath, &scoped.schema);
    }

    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context.As(&context1)))
        LOG("ID3D11DeviceContext1 unavailable; frames will carry no channel number");

    rs.initialiseGpGpuWithDX11Device(device.Get());

    const StreamDescriptions* header = nullptr;
    struct RenderTarget
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
    };
    std::unordered_map<StreamHandle, RenderTarget> renderTargets;

    uint32_t audioChannels = audio::channelsDefault;
    uint32_t audioSampleRate = audio::sampleRates[audio::sampleRateDefaultIndex];
    uint64_t audioSampleCount = 0;  // how many samples have been emitted so far
    std::vector<int16_t> audioBuffer;

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
                    rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
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
                }
                LOG("Found " << (header ? header->nStreams : 0) << " streams");
                continue;
            }
            else if (err == RS_ERROR_TIMEOUT)
            {
                continue;
            }
            else if (err == RS_ERROR_QUIT)
            {
                LOG("Exiting due to quit request.");
                return 0;
            }
        }

        // Respond to frame request
        const FrameData& frameData = std::get<FrameData>(awaitResult);

        // Remote parameter: how many channels to send. Guarded because d3 can request a frame before
        // it has picked up the schema, which would otherwise throw; the last known count is kept.
        if (frameData.scene < scoped.schema.scenes.nScenes)
        {
            try
            {
                ParameterValues values = rs.getFrameParameters(scoped.schema.scenes.scenes[frameData.scene]);

                const long channelsRounded = std::lround(values.get<float>(keys::audioChannels));
                const uint32_t requestedChannels = uint32_t(std::clamp<long>(channelsRounded, 1, audio::channelsMax));
                if (requestedChannels != audioChannels)
                {
                    LOG("Audio channels " << audioChannels << " -> " << requestedChannels
                        << " (cycle " << audio::slotSeconds * requestedChannels << "s)");
                    audioChannels = requestedChannels;
                }

                // A list parameter, so the value is an index into audio::sampleRates.
                const long rateIndex = std::lround(values.get<float>(keys::sampleRate));
                const uint32_t requestedRate = audio::sampleRates[std::clamp<long>(rateIndex, 0, long(std::size(audio::sampleRates)) - 1)];
                if (requestedRate != audioSampleRate)
                {
                    LOG("Audio sample rate " << audioSampleRate << " -> " << requestedRate);
                    audioSampleRate = requestedRate;
                    // The clock below counts in samples, so its history means nothing at a new rate:
                    // restart it from this frame rather than converting.
                    audioSampleCount = static_cast<uint64_t>(frameData.localTime * audioSampleRate);
                }
            }
            catch (const RenderStreamError& e)
            {
                if (e.error != RS_ERROR_INCORRECTSCHEMA)
                    throw;
            }
        }

        // Where the audio stream should have reached by this frame.
        const uint64_t targetSample = static_cast<uint64_t>(frameData.localTime * audioSampleRate);

        // A first frame, a seek, or a stall resyncs rather than trying to fill the whole gap.
        const uint64_t audioMaxCatchUpSamples = audioSampleRate / 10; // 100 ms
        const bool seekHappened = targetSample < audioSampleCount || targetSample - audioSampleCount > audioMaxCatchUpSamples;
        if (seekHappened)
            audioSampleCount = targetSample;

        const uint32_t audioFrames = static_cast<uint32_t>(targetSample - audioSampleCount);
        // Always allocated at the maximum width, whatever the current count - see audio::channelsMax.
        audioBuffer.assign(static_cast<size_t>(audioFrames) * audio::channelsMax, 0);
        for (uint32_t s = 0; s < audioFrames; ++s)
        {
            const audio::Slot slot = audio::slotAt(audioSampleCount + s, audioChannels, audioSampleRate);
            if (!slot.sounding())
                continue; // between beeps: leave the silence assign() already wrote

            // Only the slot's own channel carries a tone; every other channel stays silent.
            audioBuffer[static_cast<size_t>(s) * audioChannels + slot.channel] =
                static_cast<int16_t>(std::sin(DirectX::XM_2PI * slot.hz * slot.timeInSlot) * audio::toneAmplitude);
        }
        audioSampleCount = targetSample;

        // Visual cue from the same clock and the same slots as the audio: the number is the channel
        // currently sounding, held for its whole slot so it stays readable, and the square in the
        // middle of the frame flashes for exactly the length of that channel's beep.
        const audio::Slot frameSlot = audio::slotAt(targetSample, audioChannels, audioSampleRate);

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
                // It's possible to race here and be processing a request
                // which uses data from before streams changed.
                // TODO: Fix this in the API dll
                if (e.error == RS_ERROR_NOTFOUND)
                    continue;

                throw;
            }

            {
                const RenderTarget& target = renderTargets.at(description.handle);
                context->OMSetRenderTargets(1, target.view.GetAddressOf(), nullptr);

                context->ClearRenderTargetView(target.view.Get(), backgroundGreen);

                D3D11_VIEWPORT viewport;
                ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
                viewport.Width = static_cast<float>(description.width);
                viewport.Height = static_cast<float>(description.height);
                viewport.MinDepth = 0;
                viewport.MaxDepth = 1;
                context->RSSetViewports(1, &viewport);

                ConstantBufferStruct constantBufferData;
                const float angleDeg = float(frameData.localTime * 40);
                const float angleRad = DirectX::XMConvertToRadians(angleDeg);
                const DirectX::XMMATRIX world = DirectX::XMMatrixRotationRollPitchYaw(angleRad, angleRad, angleRad);

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
                UINT stride = sizeof(DirectX::XMFLOAT3);
                UINT offset = 0;
                context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
                context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->IASetInputLayout(inputLayout.Get());
                context->VSSetShader(vertexShader.Get(), nullptr, 0);
                context->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
                context->PSSetShader(pixelShader.Get(), nullptr, 0);
                UINT startIndex = 0;
                for (UINT indexCount : cubeDrawCalls)
                {
                    context->DrawIndexed(indexCount, startIndex, 0);
                    startIndex += indexCount;
                }

                // Drawn over the cube: the channel number for the whole slot, the flash square only
                // while that channel is sounding.
                cue::draw(context1.Get(), target.view.Get(), frameSlot.channel, frameSlot.sounding(),
                        description.width, description.height);

                SenderFrame data;
                data.type = RS_FRAMETYPE_DX11_TEXTURE;
                data.dx11.resource = target.texture.Get();

                FrameResponseData response = {};
                response.cameraData = &cameraData;
                rs.sendFrame(description.handle, data, response);

                if (audioFrames > 0)
                    rs.sendAudio(description.handle, audioBuffer.data(), audioFrames, audioSampleRate, audioChannels);
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
        LOG("Error: " << e.what());
        return 99;
    }
}
