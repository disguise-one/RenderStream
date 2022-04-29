// A simple RenderStream application that sends back a 3D scene using an OpenGL texture
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <vector>
#include <windows.h>
#include <shlwapi.h>
#include <tchar.h>
#include <unordered_map>
#include <GL/gl3w.h>
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>

#define BUFFER_OFFSET(i) ((void*)(i))

#include "../../include/d3renderstream.h"

#if defined(UNICODE) || defined(_UNICODE)
#define tcout std::wcout
#define tcerr std::wcerr
#else
#define tcout std::cout
#define tcerr std::cerr
#endif

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Opengl32.lib")

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

GLint toGlInternalFormat(RSPixelFormat format)
{
    switch (format)
    {
    case RS_FMT_BGRA8:
    case RS_FMT_BGRX8:
        return GL_RGBA8;
    case RS_FMT_RGBA32F:
        return GL_RGBA32F;
    case RS_FMT_RGBA16:
        return GL_RGBA16;
    case RS_FMT_RGBA8:
    case RS_FMT_RGBX8:
        return GL_RGBA8;
    default:
        throw std::runtime_error("Unhandled RS pixel format");
    }
}

GLint toGlFormat(RSPixelFormat format)
{
    switch (format)
    {
    case RS_FMT_BGRA8:
    case RS_FMT_BGRX8:
        return GL_BGRA;
    case RS_FMT_RGBA32F:
    case RS_FMT_RGBA16:
    case RS_FMT_RGBA8:
    case RS_FMT_RGBX8:
        return GL_RGBA;
    default:
        throw std::runtime_error("Unhandled RS pixel format");
    }
}

GLenum toGlType(RSPixelFormat format)
{
    switch (format)
    {
    case RS_FMT_BGRA8:
    case RS_FMT_BGRX8:
        return GL_UNSIGNED_BYTE;
    case RS_FMT_RGBA32F:
        return GL_FLOAT;
    case RS_FMT_RGBA16:
        return GL_UNSIGNED_SHORT;
    case RS_FMT_RGBA8:
    case RS_FMT_RGBX8:
        return GL_UNSIGNED_BYTE;
    default:
        throw std::runtime_error("Unhandled RS pixel format");
    }
}

static constexpr GLfloat cubeVertices[] =
{
    -0.5f,-0.5f,-0.5f,
     0.5f,-0.5f,-0.5f,
     0.5f,-0.5f, 0.5f,
    -0.5f,-0.5f, 0.5f,

    -0.5f, 0.5f,-0.5f,
     0.5f, 0.5f,-0.5f,
     0.5f, 0.5f, 0.5f,
    -0.5f, 0.5f, 0.5f,
};

static constexpr GLushort cubeIndices[] =
{

    0, 1, 2, 3, 0, 4, 5, 6, 7, 4, 
    1, 5, 
    2, 6, 
    3, 7
};

static constexpr GLsizei cubeDrawCalls[] =
{
    10,
    2,
    2,
    2
};

const GLchar* vertexShaderSource[] = { R"src(#version 140
in vec3 vert;

uniform mat4 WVP;

void main() { 
	gl_Position = WVP * vec4( vert, 1 ); 
}
)src" };

const GLchar* fragmentShaderSource[] = { R"src(#version 140
out vec4 FragColor;

void main() { 
    FragColor = vec4( 1.0f, 1.0f, 1.0f, 1.0f ); 
}
)src" };


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
    LOAD_FN(rs_initialiseGpGpuWithOpenGlContexts);
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

    const auto& className = TEXT("RS_WINDOW");

    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(nullptr));
    wc.lpszClassName = className;

    if (!RegisterClass(&wc))
    {
        tcerr << "Failed to register window class" << std::endl;
        rs_shutdown();
        return 4;
    }

    HWND hWnd = CreateWindow(className, TEXT("RS_OFFSCREEN"), WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_CLIPCHILDREN, 0, 0, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, nullptr, nullptr);
    if (hWnd == nullptr)
    {
        tcerr << "Failed to create window" << std::endl;
        rs_shutdown();
        return 41;
    }

    HDC hDc = GetDC(hWnd);
    if (hDc == nullptr)
    {
        tcerr << "Failed to get DC" << std::endl;
        rs_shutdown();
        return 42;
    }
    
    PIXELFORMATDESCRIPTOR pix_fmt;
    memset(&pix_fmt, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pix_fmt.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pix_fmt.nVersion = 1;
    pix_fmt.dwFlags = PFD_DOUBLEBUFFER | PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pix_fmt.iPixelType = PFD_TYPE_RGBA;
    pix_fmt.cColorBits = 32;
    pix_fmt.cDepthBits = 24;
    pix_fmt.cStencilBits = 8;
    pix_fmt.iLayerType = PFD_MAIN_PLANE;

    // See if there is a pixel format that will match our requests
    int nPixelFormat = ChoosePixelFormat(hDc, &pix_fmt);

    if (!nPixelFormat)
    { // Did Windows Find A Matching Pixel Format?
        tcerr << "Failed to choose pixel format" << std::endl;
        rs_shutdown();
        return 43;
    }

    if (!SetPixelFormat(hDc, nPixelFormat, &pix_fmt))
    { // Are We Able To Set The Pixel Format?
        tcerr << "Failed to set pixel format" << std::endl;
        rs_shutdown();
        return 44;
    }

    HGLRC context = wglCreateContext(hDc);
    if (context == nullptr)
    {
        tcerr << "Failed to create context" << std::endl;
        rs_shutdown();
        return 45;
    }

    if (!wglMakeCurrent(hDc, context))
    {
        tcerr << "Failed to make context current" << std::endl;
        rs_shutdown();
        return 46;
    }

    gl3wInit();
    if (glGetError() != GL_NO_ERROR)
    {
        tcerr << "Failed to init gl3w" << std::endl;
        rs_shutdown();
        return 47;
    }

    GLuint program = glCreateProgram();
    {
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, vertexShaderSource, NULL);
        glCompileShader(vertexShader);
        GLint vShaderCompiled = GL_FALSE;
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &vShaderCompiled);
        if (vShaderCompiled != GL_TRUE)
        {
            tcerr << "Failed to compile vertex shader" << std::endl;
            rs_shutdown();
            return 48;
        }
        glAttachShader(program, vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);
        GLint fShaderCompiled = GL_FALSE;
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fShaderCompiled);
        if (fShaderCompiled != GL_TRUE)
        {
            tcerr << "Failed to compile fragment shader" << std::endl;
            rs_shutdown();
            return 48;
        }
        glAttachShader(program, fragmentShader);
    }
    glLinkProgram(program);
    GLint programSuccess = GL_TRUE;
    glGetProgramiv(program, GL_LINK_STATUS, &programSuccess);
    if (programSuccess != GL_TRUE)
    {
        tcerr << "Failed to link OpenGL pogram" << std::endl;
        rs_shutdown();
        return 48;
    }

    GLuint vertexArray;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    {
        GLuint vertexBuffer;
        glGenBuffers(1, &vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        GLuint indexBuffer;
        glGenBuffers(1, &indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

        GLint att_vert = glGetAttribLocation(program, "vert");
        glEnableVertexAttribArray(att_vert);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glVertexAttribPointer(
            att_vert, // attribute
            3,        // number of elements per vertex, here (x,y,z)
            GL_FLOAT, // the type of each element
            GL_FALSE, // take our values as-is
            0,        // no extra data between each position
            0         // offset of first element
        );

    }
    glBindVertexArray(0);

    if (rs_initialiseGpGpuWithOpenGlContexts(context, hDc) != RS_ERROR_SUCCESS)
    {
        tcerr << "Failed to initialise RenderStream GPGPU interop" << std::endl;
        rs_shutdown();
        return 5;
    }

    std::vector<uint8_t> descMem;
    const StreamDescriptions* header = nullptr;
    struct RenderTarget
    {
        GLuint texture;
        GLuint frameBuffer;
    };
    std::unordered_map<StreamHandle, RenderTarget> renderTargets;
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

                    glGenTextures(1, &target.texture);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to generate render target texture for stream");

                    glBindTexture(GL_TEXTURE_2D, target.texture);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to bind render target texture for stream");

                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to setup render target texture parameters for stream");

                    glTexImage2D(GL_TEXTURE_2D, 0, toGlInternalFormat(description.format), description.width, description.height, 0, toGlFormat(description.format), toGlType(description.format), nullptr);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to create render target texture for stream");
                    glBindTexture(GL_TEXTURE_2D, 0);

                    glGenFramebuffers(1, &target.frameBuffer);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to create render target framebuffer for stream");

                    glBindFramebuffer(GL_FRAMEBUFFER, target.frameBuffer);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to bind render target framebuffer for stream");

                    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target.texture, 0);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to attach render target texture for stream");

                    GLenum buffers[] = { GL_COLOR_ATTACHMENT0 };
                    glDrawBuffers(1, buffers);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error("Failed to set draw buffers for stream");

                    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                        throw std::runtime_error("Failed fame buffer status check");

                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                }
            }
            catch (const std::exception& e)
            {
                tcerr << e.what() << std::endl;
                rs_shutdown();
                return 6;
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

            CameraResponseData cameraData;
            cameraData.tTracked = frameData.tTracked;
            if (rs_getFrameCamera(description.handle, &cameraData.camera) == RS_ERROR_SUCCESS)
            {
                const RenderTarget& target = renderTargets.at(description.handle);
                glBindFramebuffer(GL_FRAMEBUFFER, target.frameBuffer);

                glClearColor(0.f, 0.f, 0.f, 0.f);
                glClear(GL_COLOR_BUFFER_BIT);

                glViewport(0, 0, description.width, description.height);

                const glm::mat4 world = glm::identity<glm::mat4>();

                const float pitch = glm::radians(cameraData.camera.rx);
                const float yaw = glm::radians(cameraData.camera.ry);
                const float roll = glm::radians(cameraData.camera.rz);

                const glm::mat4 cameraTranslation = glm::translate(glm::identity<glm::mat4>(), glm::vec3(cameraData.camera.x, -cameraData.camera.y, cameraData.camera.z));
                const glm::mat4 cameraRotation = glm::eulerAngleYXZ(yaw, pitch, roll);
                const glm::mat4 view = glm::transpose(cameraRotation) * glm::inverse(cameraTranslation);

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

                const glm::mat4 overscan = glm::translate(glm::identity<glm::mat4>(), glm::vec3(cameraData.camera.cx, cameraData.camera.cy, 0.f));

                const float nearZ = cameraData.camera.nearZ;
                const float farZ = cameraData.camera.farZ;

                const float l = (-0.5f + description.clipping.left) * imageWidth;
                const float r = (-0.5f + description.clipping.right) * imageWidth;
                const float t = (-0.5f + 1.f - description.clipping.top) * imageHeight;
                const float b = (-0.5f + 1.f - description.clipping.bottom) * imageHeight;

                const glm::mat4 projection = orthographic ? glm::ortho(l, r, b, t, nearZ, farZ) : glm::frustum(l * nearZ, r * nearZ, b * nearZ, t * nearZ, nearZ, farZ);

                const glm::mat4 worldViewProjection = overscan * projection * view * world;

                glUseProgram(program);
                GLint wvpLoc = glGetUniformLocation(program, "WVP");
                glUniformMatrix4fv(wvpLoc, 1, false, glm::value_ptr(worldViewProjection));

                //// Draw cube
                glBindVertexArray(vertexArray);
                GLint startIndex = 0;
                for (GLsizei indexCount : cubeDrawCalls)
                {
                    glDrawElements(GL_LINE_STRIP, indexCount, GL_UNSIGNED_SHORT, BUFFER_OFFSET(startIndex * sizeof(GLushort)));
                    startIndex += indexCount;
                }

                glFinish();

                SenderFrameTypeData data;
                data.gl.texture = target.texture;

                FrameResponseData response = {};
                response.cameraData = &cameraData;
                if (rs_sendFrame(description.handle, RS_FRAMETYPE_OPENGL_TEXTURE, data, &response) != RS_ERROR_SUCCESS)
                {
                    tcerr << "Failed to send frame" << std::endl;
                    rs_shutdown();
                    return 7;
                }

                glBindVertexArray(0);
                glUseProgram(0);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
