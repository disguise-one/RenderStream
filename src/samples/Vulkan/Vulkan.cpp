// A simple RenderStream application that sends back a 3D scene using a Vulkan texture
//
// Usage: Compile, copy the executable into your RenderStream Projects folder and launch via d3

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <vector>
#include <windows.h>
#include <shlwapi.h>
#include <tchar.h>
#include <unordered_map>
#include <sstream>
#include <optional>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
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

RS_ERROR(*_rs_logToD3)(const char*) = nullptr;

#define LOG(streamexpr) { \
    std::ostringstream s; \
    s << streamexpr; \
    std::string message = s.str(); \
    std::cerr << message << std::endl; \
    if (_rs_logToD3) \
        _rs_logToD3(message.c_str()); \
}

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "vulkan-1.lib")

// Load renderstream DLL from disguise software's install path
HMODULE loadRenderStream()
{
    HKEY hKey;
    if (FAILED(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\d3 Technologies\\d3 Production Suite"), 0, KEY_READ, &hKey)))
    {
        LOG("Failed to open 'Software\\d3 Technologies\\d3 Production Suite' registry key");
        return nullptr;
    }

    TCHAR buffer[512];
    DWORD bufferSize = sizeof(buffer);
    if (FAILED(RegQueryValueEx(hKey, TEXT("exe path"), 0, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize)))
    {
        LOG("Failed to query value of 'exe path'");
        return nullptr;
    }

    if (!PathRemoveFileSpec(buffer))
    {
        LOG("Failed to remove file spec from path: " << buffer);
        return nullptr;
    }

    if (_tcscat_s(buffer, bufferSize, TEXT("\\d3renderstream.dll")) != 0)
    {
        LOG("Failed to append filename to path: " << buffer);
        return nullptr;
    }

    HMODULE hLib = ::LoadLibraryEx(buffer, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!hLib)
    {
        LOG("Failed to load dll: " << buffer);
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

VkFormat toVkFormat(RSPixelFormat format)
{
    switch (format)
    {
    case RS_FMT_BGRA8:
    case RS_FMT_BGRX8:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case RS_FMT_RGBA32F:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RS_FMT_RGBA16:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case RS_FMT_RGBA8:
    case RS_FMT_RGBX8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        throw std::runtime_error("Unhandled RS pixel format");
    }
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) 
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) 
        {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanReportFunc(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData)
{
    LOG("VULKAN VALIDATION: " << msg);
    return VK_FALSE;
}

static constexpr float cubeVertices[] =
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

static constexpr uint16_t cubeIndices[] =
{

    0, 1, 2, 3, 0, 4, 5, 6, 7, 4, 
    1, 5, 
    2, 6, 
    3, 7
};

static constexpr uint32_t cubeDrawCalls[] =
{
    10,
    2,
    2,
    2
};

const uint8_t vertexShaderSource[] = { // output of "glslc vert.frag -o vert.spv"
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x0D, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4C, 0x53, 0x4C, 0x2E, 0x73, 0x74, 0x64, 0x2E, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6D, 0x61, 0x69, 0x6E, 0x00, 0x00, 0x00, 0x00,
    0x0D, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x02, 0x00, 0x00, 0x00, 0xC2, 0x01, 0x00, 0x00, 0x04, 0x00, 0x0A, 0x00,
    0x47, 0x4C, 0x5F, 0x47, 0x4F, 0x4F, 0x47, 0x4C, 0x45, 0x5F, 0x63, 0x70,
    0x70, 0x5F, 0x73, 0x74, 0x79, 0x6C, 0x65, 0x5F, 0x6C, 0x69, 0x6E, 0x65,
    0x5F, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x00,
    0x04, 0x00, 0x08, 0x00, 0x47, 0x4C, 0x5F, 0x47, 0x4F, 0x4F, 0x47, 0x4C,
    0x45, 0x5F, 0x69, 0x6E, 0x63, 0x6C, 0x75, 0x64, 0x65, 0x5F, 0x64, 0x69,
    0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6D, 0x61, 0x69, 0x6E, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x06, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x67, 0x6C, 0x5F, 0x50,
    0x65, 0x72, 0x56, 0x65, 0x72, 0x74, 0x65, 0x78, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x06, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x67, 0x6C, 0x5F, 0x50, 0x6F, 0x73, 0x69, 0x74, 0x69, 0x6F, 0x6E, 0x00,
    0x06, 0x00, 0x07, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x67, 0x6C, 0x5F, 0x50, 0x6F, 0x69, 0x6E, 0x74, 0x53, 0x69, 0x7A, 0x65,
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x07, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x67, 0x6C, 0x5F, 0x43, 0x6C, 0x69, 0x70, 0x44,
    0x69, 0x73, 0x74, 0x61, 0x6E, 0x63, 0x65, 0x00, 0x06, 0x00, 0x07, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x67, 0x6C, 0x5F, 0x43,
    0x75, 0x6C, 0x6C, 0x44, 0x69, 0x73, 0x74, 0x61, 0x6E, 0x63, 0x65, 0x00,
    0x05, 0x00, 0x03, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x03, 0x00, 0x11, 0x00, 0x00, 0x00, 0x55, 0x42, 0x4F, 0x00,
    0x06, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x57, 0x56, 0x50, 0x00, 0x05, 0x00, 0x03, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x75, 0x62, 0x6F, 0x00, 0x05, 0x00, 0x04, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x76, 0x65, 0x72, 0x74, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x05, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x47, 0x00, 0x03, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x48, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x03, 0x00, 0x11, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x13, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2B, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x04, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x06, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x04, 0x00, 0x0C, 0x00, 0x00, 0x00,
    0x0D, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x15, 0x00, 0x04, 0x00,
    0x0E, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x2B, 0x00, 0x04, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x03, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x3B, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2B, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F,
    0x20, 0x00, 0x04, 0x00, 0x21, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xF8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x16, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x04, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00,
    0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
    0x1C, 0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00,
    0x1B, 0x00, 0x00, 0x00, 0x91, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
    0x41, 0x00, 0x05, 0x00, 0x21, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x0D, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x03, 0x00,
    0x22, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x01, 0x00,
    0x38, 0x00, 0x01, 0x00
};

const uint8_t fragmentShaderSource[] = { // output of "glslc vert.frag -o vert.spv"
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x0D, 0x00,
    0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4C, 0x53, 0x4C, 0x2E, 0x73, 0x74, 0x64, 0x2E, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x06, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6D, 0x61, 0x69, 0x6E, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x10, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00,
    0xC2, 0x01, 0x00, 0x00, 0x04, 0x00, 0x0A, 0x00, 0x47, 0x4C, 0x5F, 0x47,
    0x4F, 0x4F, 0x47, 0x4C, 0x45, 0x5F, 0x63, 0x70, 0x70, 0x5F, 0x73, 0x74,
    0x79, 0x6C, 0x65, 0x5F, 0x6C, 0x69, 0x6E, 0x65, 0x5F, 0x64, 0x69, 0x72,
    0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00,
    0x47, 0x4C, 0x5F, 0x47, 0x4F, 0x4F, 0x47, 0x4C, 0x45, 0x5F, 0x69, 0x6E,
    0x63, 0x6C, 0x75, 0x64, 0x65, 0x5F, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74,
    0x69, 0x76, 0x65, 0x00, 0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x6D, 0x61, 0x69, 0x6E, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x46, 0x72, 0x61, 0x67, 0x43, 0x6F, 0x6C, 0x6F,
    0x72, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x3B, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x2B, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x2C, 0x00, 0x07, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x02, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x03, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00
};


int main()
{
    HMODULE hLib = loadRenderStream();
    if (!hLib)
    {
        LOG("Failed to load RenderStream DLL");
        return 1;
    }

#define LOAD_FN(FUNC_NAME) \
    decltype(FUNC_NAME)* FUNC_NAME = reinterpret_cast<decltype(FUNC_NAME)>(GetProcAddress(hLib, #FUNC_NAME)); \
    if (!FUNC_NAME) { \
        LOG("Failed to get function " #FUNC_NAME " from DLL"); \
        return 2; \
    }

    LOAD_FN(rs_initialise);
    LOAD_FN(rs_initialiseGpGpuWithVulkanDevice);
    LOAD_FN(rs_getStreams);
    LOAD_FN(rs_awaitFrameData);
    LOAD_FN(rs_getFrameCamera);
    LOAD_FN(rs_sendFrame);
    LOAD_FN(rs_shutdown);
    _rs_logToD3 = reinterpret_cast<decltype(_rs_logToD3)>(GetProcAddress(hLib, "rs_logToD3"));

    if (rs_initialise(RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR) != RS_ERROR_SUCCESS)
    {
        LOG("Failed to initialise RenderStream");
        return 3;
    }

    VkInstance instance = VK_NULL_HANDLE;
    {
        VkApplicationInfo appInfo = {
            VK_STRUCTURE_TYPE_APPLICATION_INFO,
            nullptr,
            "RenderStream sample",
            VK_MAKE_VERSION(1, 0, 0),
            "RenderStream",
            VK_MAKE_VERSION(1, 0, 0),
            VK_API_VERSION_1_0
        };
        std::vector<const char*> extensions = { 
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
#ifdef _DEBUG
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
        };
        const std::vector<const char*> validationLayers = {
#ifdef _DEBUG
            "VK_LAYER_KHRONOS_validation"
#endif
        };
        VkInstanceCreateInfo createInfo = {
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            nullptr,
            0,
            &appInfo,
            uint32_t(validationLayers.size()),
            validationLayers.data(),
            uint32_t(extensions.size()),
            extensions.data()
        };
        if (vkCreateInstance(&createInfo, 0, &instance) != VK_SUCCESS)
        {
            LOG("Failed to create instance");
            rs_shutdown();
            return 40;
        }
    }
#if _DEBUG
    PFN_vkCreateDebugReportCallbackEXT vkpfn_CreateDebugReportCallbackEXT =
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
    PFN_vkDestroyDebugReportCallbackEXT vkpfn_DestroyDebugReportCallbackEXT =
        (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (vkpfn_CreateDebugReportCallbackEXT && vkpfn_DestroyDebugReportCallbackEXT)
    {
            VkDebugReportCallbackCreateInfoEXT debugCallbackCreateInfo = {};
            debugCallbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debugCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
            debugCallbackCreateInfo.pfnCallback = VulkanReportFunc;
            VkDebugReportCallbackEXT debugCallback = VK_NULL_HANDLE;
            if (vkpfn_CreateDebugReportCallbackEXT(instance, &debugCallbackCreateInfo, 0, &debugCallback) != VK_SUCCESS)
            {
                LOG("Failed to create debug report callback");
                rs_shutdown();
                return 40;
            }
    }
#endif

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::optional<uint32_t> queueFamilyIndex;
    {
        std::vector<const char*> extensions = { 
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, 
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME            
        };
        VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineFeature = {};
        timelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
        timelineFeature.timelineSemaphore = true;

        VkPhysicalDeviceFeatures2KHR features2 = {};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
        features2.pNext = &timelineFeature;

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &features2;
        deviceCreateInfo.enabledExtensionCount = uint32_t(extensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = extensions.data();

        uint32_t physicalDeviceCount;
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, 0);
        std::vector<VkPhysicalDevice> deviceHandles(physicalDeviceCount);
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, deviceHandles.data());

        for (VkPhysicalDevice deviceHandle : deviceHandles)
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(deviceHandle, &queueFamilyCount, NULL);
            std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(deviceHandle, &queueFamilyCount, queueFamilyProperties.data());

            for (uint32_t j = 0; j < queueFamilyCount; ++j) {

                if (queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    queueFamilyIndex = j;
                    physicalDevice = deviceHandle;
                    break;
                }
            }
        }

        if (!queueFamilyIndex.has_value())
        {
            LOG("Failed to find queue family with graphics bit set");
            rs_shutdown();
            return 41;
        }

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex.value();
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        if (vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device) != VK_SUCCESS)
        {
            LOG("Failed to create device");
            rs_shutdown();
            return 42;
        }
        vkGetDeviceQueue(device, queueFamilyIndex.value(), 0, &queue);
    }

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    {
        VkCommandPoolCreateInfo commandPoolCreateInfo = {};
        commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex = queueFamilyIndex.value();

        if (vkCreateCommandPool(device, &commandPoolCreateInfo, 0, &commandPool) != VK_SUCCESS)
        {
            LOG("Failed to create command pool");
            rs_shutdown();
            return 43;
        }

        VkCommandBufferAllocateInfo commandBufferAllocInfo = {};
        commandBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocInfo.commandPool = commandPool;
        commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &commandBufferAllocInfo, &commandBuffer) != VK_SUCCESS)
        {
            LOG("Failed to allocate command buffer");
            rs_shutdown();
            return 43;
        }
    }

    VkDescriptorSetLayout descriptorSetLayout;
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) 
        {
            LOG("Failed to descriptor set layout");
            rs_shutdown();
            return 44;
        }
    }

    VkPipelineLayout pipelineLayout;
    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
        if (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, 0, &pipelineLayout) != VK_SUCCESS)
        {
            LOG("Failed to create pipeline layout");
            rs_shutdown();
            return 45;
        }
    }

    std::unordered_map<VkFormat, VkPipeline> pipelines;
    for (VkFormat format : { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_UNORM})
    {
        VkPipeline& pipeline = pipelines[format];

        VkShaderModule vertexShader;
        VkShaderModuleCreateInfo vsCreateInfo = {};
        vsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsCreateInfo.codeSize = std::extent<decltype(vertexShaderSource)>::value;
        vsCreateInfo.pCode = reinterpret_cast<const uint32_t*>(vertexShaderSource);
        if (vkCreateShaderModule(device, &vsCreateInfo, nullptr, &vertexShader) != VK_SUCCESS)
        {
            LOG("Failed to vertex shader module");
            rs_shutdown();
            return 45;
        }

        VkShaderModule fragmentShader;
        VkShaderModuleCreateInfo fsCreateInfo = {};
        fsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fsCreateInfo.codeSize = std::extent<decltype(fragmentShaderSource)>::value;
        fsCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fragmentShaderSource);
        if (vkCreateShaderModule(device, &fsCreateInfo, nullptr, &fragmentShader) != VK_SUCCESS)
        {
            LOG("Failed to fragment shader module");
            rs_shutdown();
            return 45;
        }

        VkPipelineShaderStageCreateInfo vsStage = {};
        vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vsStage.module = vertexShader;
        vsStage.pName = "main";
        VkPipelineShaderStageCreateInfo fsStage = {};
        fsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fsStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fsStage.module = fragmentShader;
        fsStage.pName = "main";
        const VkPipelineShaderStageCreateInfo stages[] = { vsStage, fsStage };

        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(float) * 3;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attributeDescription = {};
        attributeDescription.binding = 0;
        attributeDescription.location = 0;
        attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescription.offset = 0;
        VkPipelineVertexInputStateCreateInfo vertexInputState = {};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &bindingDescription;
        vertexInputState.vertexAttributeDescriptionCount = 1;
        vertexInputState.pVertexAttributeDescriptions = &attributeDescription;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        inputAssemblyState.primitiveRestartEnable = VK_FALSE;
        VkPipelineRasterizationStateCreateInfo rasterizationState = {};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.depthClampEnable = VK_FALSE;
        rasterizationState.rasterizerDiscardEnable = VK_FALSE;
        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizationState.depthBiasEnable = VK_FALSE;
        rasterizationState.depthBiasConstantFactor = 0.0f;
        rasterizationState.depthBiasClamp = 0.0f;
        rasterizationState.depthBiasSlopeFactor = 0.0f;
        rasterizationState.lineWidth = 1.0f;
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = 0;
        viewportState.scissorCount = 1;
        viewportState.pScissors = 0;
        VkPipelineMultisampleStateCreateInfo multisampleState = {};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampleState.sampleShadingEnable = VK_FALSE;
        multisampleState.minSampleShading = 1.0f;
        multisampleState.pSampleMask = 0;
        multisampleState.alphaToCoverageEnable = VK_FALSE;
        multisampleState.alphaToOneEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
        colorBlendAttachmentState.blendEnable = VK_FALSE;
        colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlendState = {};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.logicOpEnable = VK_FALSE;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &colorBlendAttachmentState;
        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        dynamicState.dynamicStateCount = 2,
        dynamicState.pDynamicStates = dynamicStates;

        VkRenderPass renderPass;
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) 
        {
            LOG("Failed to create pipeline render pass");
            rs_shutdown();
            return 45;
        }

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = stages;
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.renderPass = renderPass;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            LOG("Failed to create graphics pipeline");
            rs_shutdown();
            return 45;
        }

        vkDestroyShaderModule(device, vertexShader, 0);
        vkDestroyShaderModule(device, fragmentShader, 0);
    }

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(cubeVertices);
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) 
        {
            LOG("Failed to create vertex buffer");
            rs_shutdown();
            return 46;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) 
        {
            LOG("Failed to allocate vertex buffer memory");
            rs_shutdown();
            return 46;
        }

        vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

        void* data;
        vkMapMemory(device, vertexBufferMemory, 0, bufferInfo.size, 0, &data);
        memcpy(data, cubeVertices, (size_t)bufferInfo.size);
        vkUnmapMemory(device, vertexBufferMemory);
    }

    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(cubeIndices);
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &indexBuffer) != VK_SUCCESS)
        {
            LOG("Failed to create index buffer");
            rs_shutdown();
            return 46;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, indexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &indexBufferMemory) != VK_SUCCESS)
        {
            LOG("Failed to allocate index buffer memory");
            rs_shutdown();
            return 46;
        }

        vkBindBufferMemory(device, indexBuffer, indexBufferMemory, 0);

        void* data;
        vkMapMemory(device, indexBufferMemory, 0, bufferInfo.size, 0, &data);
        memcpy(data, cubeIndices, (size_t)bufferInfo.size);
        vkUnmapMemory(device, indexBufferMemory);
    }

    if (rs_initialiseGpGpuWithVulkanDevice(device) != RS_ERROR_SUCCESS)
    {
        LOG("Failed to initialise RenderStream GPGPU interop");
        rs_shutdown();
        return 5;
    }

    std::vector<uint8_t> descMem;
    const StreamDescriptions* header = nullptr;
    struct RenderTarget
    {
        VkImage image;
        VkDeviceMemory mem;
        VkDeviceSize offset;
        VkDeviceSize size;
        VkImageView view;
        VkRenderPass renderPass;
        VkFramebuffer frameBuffer;
        VkFence fence;
        VkDescriptorSet descriptorSet;
        VkBuffer uniformBuffer;
        VkDeviceMemory uniformBufferMemory;
        VkSemaphore semaphore;
        uint64_t semaphoreValue = 0;
    };
    std::unordered_map<StreamHandle, RenderTarget> renderTargets;
    FrameData frameData;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;
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
                renderTargets.clear();
                VkDescriptorPoolSize poolSize = {};
                poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                poolSize.descriptorCount = uint32_t(numStreams);
                VkDescriptorPoolCreateInfo poolInfo = {};
                poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                poolInfo.poolSizeCount = 1;
                poolInfo.pPoolSizes = &poolSize;
                poolInfo.maxSets = uint32_t(numStreams);
                if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) 
                    throw std::runtime_error("Failed to create descriptor pool!");
                std::vector<VkDescriptorSetLayout> layouts(numStreams, descriptorSetLayout);
                VkDescriptorSetAllocateInfo allocInfo = {};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = descriptorPool;
                allocInfo.descriptorSetCount = uint32_t(numStreams);
                allocInfo.pSetLayouts = layouts.data();
                descriptorSets.resize(numStreams);
                if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS)
                    throw std::runtime_error("Failed to allocate descriptor sets!");
                for (size_t i = 0; i < numStreams; ++i)
                {
                    const StreamDescription& description = header->streams[i];
                    RenderTarget& target = renderTargets[description.handle];
                    target.descriptorSet = descriptorSets.at(i);

                    {
                        VkBufferCreateInfo bufferInfo = {};
                        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                        bufferInfo.size = sizeof(float) * 4 * 4;
                        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                        if (vkCreateBuffer(device, &bufferInfo, nullptr, &target.uniformBuffer) != VK_SUCCESS)
                            throw std::runtime_error("Failed to create uniform buffer");

                        VkMemoryRequirements memRequirements;
                        vkGetBufferMemoryRequirements(device, target.uniformBuffer, &memRequirements);

                        VkMemoryAllocateInfo allocInfo{};
                        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                        allocInfo.allocationSize = memRequirements.size;
                        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                        if (vkAllocateMemory(device, &allocInfo, nullptr, &target.uniformBufferMemory) != VK_SUCCESS)
                            throw std::runtime_error("Failed to allocate uniform buffer memory");

                        vkBindBufferMemory(device, target.uniformBuffer, target.uniformBufferMemory, 0);
                    }

                    VkDescriptorBufferInfo bufferInfo = {};
                    bufferInfo.buffer = target.uniformBuffer;
                    bufferInfo.offset = 0;
                    bufferInfo.range = sizeof(float) * 4 * 4;

                    VkWriteDescriptorSet descriptorWrite = {};
                    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrite.dstSet = target.descriptorSet;
                    descriptorWrite.dstBinding = 0;
                    descriptorWrite.dstArrayElement = 0;
                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    descriptorWrite.descriptorCount = 1;
                    descriptorWrite.pBufferInfo = &bufferInfo;
                    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

                    VkImageCreateInfo image = {};
                    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                    image.imageType = VK_IMAGE_TYPE_2D;
                    image.format = toVkFormat(description.format);
                    image.extent.width = description.width;
                    image.extent.height = description.height;
                    image.extent.depth = 1;
                    image.mipLevels = 1;
                    image.arrayLayers = 1;
                    image.samples = VK_SAMPLE_COUNT_1_BIT;
                    image.tiling = VK_IMAGE_TILING_OPTIMAL;
                    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    VkExternalMemoryImageCreateInfoKHR extImageCreateInfo = {};
                    extImageCreateInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
                    extImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
                    image.pNext = &extImageCreateInfo;

                    if (vkCreateImage(device, &image, nullptr, &target.image) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create render target image for stream");

                    VkMemoryRequirements memReqs;
                    vkGetImageMemoryRequirements(device, target.image, &memReqs);

                    VkExportMemoryAllocateInfoKHR exportInfo = {};
                    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
                    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
                    VkMemoryAllocateInfo memAlloc = {};
                    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    memAlloc.pNext = &exportInfo;
                    target.size = memAlloc.allocationSize = memReqs.size;
                    memAlloc.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    if (vkAllocateMemory(device, &memAlloc, nullptr, &target.mem) != VK_SUCCESS)
                        throw std::runtime_error("Failed to allocate memory for stream");
                    if (vkBindImageMemory(device, target.image, target.mem, target.offset) != VK_SUCCESS)
                        throw std::runtime_error("Failed to bind image memory for stream");

                    VkImageViewCreateInfo colorImageView = {};
                    colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    colorImageView.format = image.format;
                    colorImageView.subresourceRange = {};
                    colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    colorImageView.subresourceRange.baseMipLevel = 0;
                    colorImageView.subresourceRange.levelCount = 1;
                    colorImageView.subresourceRange.baseArrayLayer = 0;
                    colorImageView.subresourceRange.layerCount = 1;
                    colorImageView.image = target.image;
                    if (vkCreateImageView(device, &colorImageView, nullptr, &target.view) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create image view for stream");

                    VkAttachmentDescription attachment = {};
                    attachment.format = image.format;
                    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

                    VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

                    VkSubpassDescription subpassDescription = {};
                    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    subpassDescription.colorAttachmentCount = 1;
                    subpassDescription.pColorAttachments = &colorReference;

                    VkRenderPassCreateInfo renderPassInfo = {};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                    renderPassInfo.attachmentCount = 1;
                    renderPassInfo.pAttachments = &attachment;
                    renderPassInfo.subpassCount = 1;
                    renderPassInfo.pSubpasses = &subpassDescription;

                    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &target.renderPass) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create render pass for stream");

                    VkFramebufferCreateInfo fbufCreateInfo = {};
                    fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbufCreateInfo.renderPass = target.renderPass;
                    fbufCreateInfo.attachmentCount = 1;
                    fbufCreateInfo.pAttachments = &target.view;
                    fbufCreateInfo.width = description.width;
                    fbufCreateInfo.height = description.height;
                    fbufCreateInfo.layers = 1;

                    if (vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &target.frameBuffer) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create frame buffer for stream");

                    VkFenceCreateInfo fenceCreateInfo = {};
                    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                    if (vkCreateFence(device, &fenceCreateInfo, 0, &target.fence) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create fence for stream");

                    VkExportSemaphoreCreateInfo exportSemaphoreCreateInfo = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
                    exportSemaphoreCreateInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

                    VkSemaphoreTypeCreateInfo timelineCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
                    timelineCreateInfo.pNext = &exportSemaphoreCreateInfo;
                    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
                    timelineCreateInfo.initialValue = target.semaphoreValue;

                    VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
                    semaphoreCreateInfo.pNext = &timelineCreateInfo;
                    if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &target.semaphore) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create semaphore for stream");

                }
            }
            catch (const std::exception& e)
            {
                LOG(e.what());
                rs_shutdown();
                return 6;
            }
            LOG("Found " << (header ? header->nStreams : 0) << " streams");
            continue;
        }
        else if (err == RS_ERROR_TIMEOUT)
        {
            continue;
        }
        else if (err != RS_ERROR_SUCCESS)
        {
            LOG("rs_awaitFrameData returned " << err);
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
                RenderTarget& target = renderTargets.at(description.handle);
                VkPipeline pipeline = pipelines[toVkFormat(description.format)];

                vkWaitForFences(device, 1, &target.fence, VK_TRUE, UINT64_MAX);
                vkResetFences(device, 1, &target.fence);
                vkResetCommandBuffer(commandBuffer, 0);

                VkCommandBufferBeginInfo beginInfo = {};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer(commandBuffer, &beginInfo);

                VkClearValue clearValue = {};
                clearValue.color = { { 0.f, 0.f, 0.f, 0.f } };
                VkRenderPassBeginInfo renderPassInfo = {};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = target.renderPass;
                renderPassInfo.framebuffer = target.frameBuffer;
                renderPassInfo.renderArea.extent.width = description.width;
                renderPassInfo.renderArea.extent.height = description.height;
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearValue;

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

                VkViewport viewport = {};
                viewport.width = float(description.width);
                viewport.height = float(description.height);
                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.extent.width = description.width;
                scissor.extent.height = description.height;
                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

                vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

                {
                    void* uboPtr;
                    vkMapMemory(device, target.uniformBufferMemory, 0, sizeof(float) * 4 * 4, 0, &uboPtr);
                    memcpy(uboPtr, glm::value_ptr(worldViewProjection), sizeof(float) * 4 * 4);
                    vkUnmapMemory(device, target.uniformBufferMemory);
                }

                ////// Draw cube
                VkBuffer vertexBuffers[] = { vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &target.descriptorSet, 0, nullptr);

                uint32_t startIndex = 0;
                for (uint32_t indexCount : cubeDrawCalls)
                {
                    vkCmdDrawIndexed(commandBuffer, indexCount, 1, startIndex, 0, 0);
                    startIndex += indexCount;
                }
                    
                vkCmdEndRenderPass(commandBuffer);

                vkEndCommandBuffer(commandBuffer);

                uint64_t waitValue = target.semaphoreValue;
                ++target.semaphoreValue;
                VkTimelineSemaphoreSubmitInfo timelineInfo = {};
                timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
                timelineInfo.waitSemaphoreValueCount = 1;
                timelineInfo.pWaitSemaphoreValues = &waitValue;
                timelineInfo.signalSemaphoreValueCount = 1;
                timelineInfo.pSignalSemaphoreValues = &target.semaphoreValue;

                VkPipelineStageFlags flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.pNext = &timelineInfo;
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pWaitSemaphores = &target.semaphore;
                submitInfo.pWaitDstStageMask = &flags;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &target.semaphore;
                vkQueueSubmit(queue, 1, &submitInfo, target.fence);

                SenderFrameTypeData data;
                data.vk.memory = target.mem;
                data.vk.size = target.offset + target.size;
                data.vk.format = description.format;
                data.vk.width = description.width;
                data.vk.height = description.height;
                data.vk.waitSemaphore = target.semaphore;
                data.vk.waitSemaphoreValue = target.semaphoreValue;
                data.vk.signalSemaphore = target.semaphore;
                data.vk.signalSemaphoreValue = ++target.semaphoreValue;

                FrameResponseData response = {};
                response.colourFrameType = RS_FRAMETYPE_VULKAN_TEXTURE;
                response.colourFrameData = data;
                response.cameraData = &cameraData;
                if (rs_sendFrame(description.handle, response) != RS_ERROR_SUCCESS)
                {
                    LOG("Failed to send frame");
                    rs_shutdown();
                    return 7;
                }
            }
        }
    }

    if (rs_shutdown() != RS_ERROR_SUCCESS)
    {
        LOG("Failed to shutdown RenderStream");
        return 99;
    }

    return 0;
}
