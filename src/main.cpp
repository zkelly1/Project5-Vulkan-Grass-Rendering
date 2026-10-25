#include <vulkan/vulkan.h>
#include "Instance.h"
#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "Image.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <sstream>

Device* device;
SwapChain* swapChain;
Renderer* renderer;
Camera* camera;
Scene* scene;

namespace {
    void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_1) scene->ToggleCulling(1);
        if (key == GLFW_KEY_2) scene->ToggleCulling(2);
        if (key == GLFW_KEY_3) scene->ToggleCulling(4);
        if (key == GLFW_KEY_SPACE) scene->ToggleSimulation();
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
        const Time& settings = scene->GetSettings();
        printf("Culling: orientation %s, frustum %s, distance %s; simulation %s\n",
            settings.culling & 1 ? "on" : "off", settings.culling & 2 ? "on" : "off",
            settings.culling & 4 ? "on" : "off", settings.simulate ? "on" : "paused");
    }

    void resizeCallback(GLFWwindow* window, int width, int height) {
        if (width == 0 || height == 0) return;

        vkDeviceWaitIdle(device->GetVkDevice());
        swapChain->Recreate();
        renderer->RecreateFrameResources();
    }

    bool leftMouseDown = false;
    bool rightMouseDown = false;
    double previousX = 0.0;
    double previousY = 0.0;

    void mouseDownCallback(GLFWwindow* window, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                leftMouseDown = true;
                glfwGetCursorPos(window, &previousX, &previousY);
            }
            else if (action == GLFW_RELEASE) {
                leftMouseDown = false;
            }
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                rightMouseDown = true;
                glfwGetCursorPos(window, &previousX, &previousY);
            }
            else if (action == GLFW_RELEASE) {
                rightMouseDown = false;
            }
        }
    }

    void mouseMoveCallback(GLFWwindow* window, double xPosition, double yPosition) {
        if (leftMouseDown) {
            double sensitivity = 0.5;
            float deltaX = static_cast<float>((previousX - xPosition) * sensitivity);
            float deltaY = static_cast<float>((previousY - yPosition) * sensitivity);

            camera->UpdateOrbit(deltaX, deltaY, 0.0f);

            previousX = xPosition;
            previousY = yPosition;
        } else if (rightMouseDown) {
            float deltaZ = static_cast<float>((previousY - yPosition) * 0.05);

            camera->UpdateOrbit(0.0f, 0.0f, deltaZ);

            previousY = yPosition;
        }
    }
}

int runRenderer(int argc, char** argv) {
    int frameLimit = 0;
    int warmup = 60;
    uint32_t bladeCount = NUM_BLADES;
    uint32_t culling = 7;
    bool simulate = true;
    bool verify = false;
    bool orbit = false;
    bool offscreen = false;
    bool lod = true;
    std::string captureDirectory;
    int captureEvery = 4;
    float wind = 6.0f;
    float fixedDelta = 0.0f;
    std::string view = "overview";
    std::string benchmarkPath, dumpPath;
    auto usage = []() {
        printf("Usage: vulkan_grass_rendering [--frames N] [--blades N]\n"
            "  [--culling none|orientation|frustum|distance|all] [--static] [--wind strength]\n"
            "  [--view overview|close|distant|away] [--fixed-dt seconds] [--orbit] [--no-lod]\n"
            "  [--benchmark output.csv] [--warmup N] [--verify] [--dump-blades output.bin] [--offscreen] [--capture-dir directory] [--capture-every N]\n");
    };
    auto number = [](const char* value, double minimum, double maximum) {
        char* end;
        errno = 0;
        double result = std::strtod(value, &end);
        if (errno || end == value || *end || !std::isfinite(result) || result < minimum || result > maximum) {
            throw std::runtime_error("Invalid numeric argument");
        }
        return result;
    };
    try {
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--help") { usage(); return 0; }
            if (option == "--no-lod") { lod = false; continue; }
            if (option == "--static") { simulate = false; continue; }
            if (option == "--verify") { verify = true; continue; }
            if (option == "--orbit") { orbit = true; continue; }
            if (option == "--offscreen") { offscreen = true; continue; }
            if (++i >= argc) throw std::runtime_error("Missing option value");
            const char* value = argv[i];
            if (option == "--frames" || option == "--warmup" || option == "--blades" || option == "--capture-every") {
                double count = number(value, option == "--warmup" ? 0 : 1, 1000000);
                if (count != std::floor(count)) throw std::runtime_error("Expected an integer count");
                if (option == "--frames") frameLimit = static_cast<int>(count);
                if (option == "--warmup") warmup = static_cast<int>(count);
                if (option == "--capture-every") captureEvery = static_cast<int>(count);
                if (option == "--blades") bladeCount = static_cast<uint32_t>(count);
            } else if (option == "--wind") wind = static_cast<float>(number(value, 0, 100));
            else if (option == "--fixed-dt") fixedDelta = static_cast<float>(number(value, 0.0001, 1.0 / 30.0));
            else if (option == "--benchmark") benchmarkPath = value;
            else if (option == "--capture-dir") captureDirectory = value;
            else if (option == "--dump-blades") dumpPath = value;
            else if (option == "--view") {
                view = value;
                if (view != "overview" && view != "close" && view != "distant" && view != "away") throw std::runtime_error("Unknown view");
            } else if (option == "--culling") {
                std::string mode = value;
                if (mode == "none") culling = 0;
                else if (mode == "orientation") culling = 1;
                else if (mode == "frustum") culling = 2;
                else if (mode == "distance") culling = 4;
                else if (mode == "all") culling = 7;
                else throw std::runtime_error("Unknown culling mode");
            } else throw std::runtime_error("Unknown option");
        }
        if (!captureDirectory.empty()) {
            if (!benchmarkPath.empty()) throw std::runtime_error("Capture and benchmark must run separately");
            offscreen = true;
            if (frameLimit == 0) frameLimit = 180;
            if (fixedDelta == 0.0f) fixedDelta = 1.0f / 60.0f;
        }
        if (!benchmarkPath.empty()) {
            if (frameLimit == 0) frameLimit = 240;
            if (frameLimit <= warmup) throw std::runtime_error("Frame count must exceed benchmark warmup");
            if (fixedDelta == 0.0f) fixedDelta = 1.0f / 60.0f;
        }
        if (offscreen && frameLimit == 0) throw std::runtime_error("Offscreen runs require --frames or --benchmark");
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        usage();
        return EXIT_FAILURE;
    }
    std::ofstream benchmark;
    if (!benchmarkPath.empty()) {
        benchmark.open(benchmarkPath);
        if (!benchmark) { fprintf(stderr, "Cannot open benchmark output\n"); return EXIT_FAILURE; }
        benchmark << "blades,culling,view,wind,fixed_dt,simulate,lod,frame,simulation_seconds,compute_ms,graphics_ms,frame_ms\n";
        benchmark << std::fixed << std::setprecision(6);
    }
    static constexpr const char* applicationName = "Vulkan Grass Rendering";
    InitializeWindow(640, 480, applicationName, offscreen);

    unsigned int glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    Instance* instance = new Instance(applicationName, glfwExtensionCount, glfwExtensions);

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance->GetVkInstance(), GetGLFWWindow(), nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }

    instance->PickPhysicalDevice({ VK_KHR_SWAPCHAIN_EXTENSION_NAME }, QueueFlagBit::GraphicsBit | QueueFlagBit::TransferBit | QueueFlagBit::ComputeBit | QueueFlagBit::PresentBit, surface);

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.tessellationShader = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    device = instance->CreateDevice(QueueFlagBit::GraphicsBit | QueueFlagBit::TransferBit | QueueFlagBit::ComputeBit | QueueFlagBit::PresentBit, deviceFeatures);

    swapChain = device->CreateSwapChain(surface, 5);

    camera = new Camera(device, 640.f / 480.f);
    camera->SetView(view);

    VkCommandPoolCreateInfo transferPoolInfo = {};
    transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    transferPoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Graphics];
    transferPoolInfo.flags = 0;

    VkCommandPool transferCommandPool;
    if (vkCreateCommandPool(device->GetVkDevice(), &transferPoolInfo, nullptr, &transferCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    VkImage grassImage;
    VkDeviceMemory grassImageMemory;
    Image::FromFile(device,
        transferCommandPool,
        "images/grass.jpg",
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        grassImage,
        grassImageMemory
    );

    float planeDim = 15.f;
    float halfWidth = planeDim * 0.5f;
    Model* plane = new Model(device, transferCommandPool,
        {
            { { -halfWidth, 0.0f, halfWidth }, { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f } },
            { { halfWidth, 0.0f, halfWidth }, { 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
            { { halfWidth, 0.0f, -halfWidth }, { 0.0f, 0.0f, 1.0f },{ 0.0f, 1.0f } },
            { { -halfWidth, 0.0f, -halfWidth }, { 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f } }
        },
        { 0, 1, 2, 2, 3, 0 }
    );
    plane->SetTexture(grassImage);
    
    std::srand(1);
    Blades* blades = new Blades(device, transferCommandPool, planeDim, bladeCount);

    vkDestroyCommandPool(device->GetVkDevice(), transferCommandPool, nullptr);

    scene = new Scene(device);
    scene->SetSettings(culling, simulate, wind);
    scene->AddModel(plane);
    scene->AddBlades(blades);

    renderer = new Renderer(device, swapChain, scene, camera, !benchmarkPath.empty(), offscreen, lod);

    glfwSetKeyCallback(GetGLFWWindow(), keyCallback);
    glfwSetWindowSizeCallback(GetGLFWWindow(), resizeCallback);
    glfwSetMouseButtonCallback(GetGLFWWindow(), mouseDownCallback);
    glfwSetCursorPosCallback(GetGLFWWindow(), mouseMoveCallback);

    int frameCount = 0;
    while (!ShouldQuit() && (frameLimit == 0 || frameCount < frameLimit)) {
        if (!offscreen) glfwPollEvents();
        if (orbit) camera->UpdateOrbit(0.25f, 0.0f, 0.0f);
        scene->UpdateTime(fixedDelta);
        auto start = std::chrono::steady_clock::now();
        if (!renderer->Frame()) continue;
        double frameMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!benchmarkPath.empty() && frameCount >= warmup) {
            const Time& settings = scene->GetSettings();
            benchmark << bladeCount << ',' << settings.culling << ',' << view << ',' << settings.windStrength << ','
                << fixedDelta << ',' << settings.simulate << ',' << lod << ',' << frameCount << ',' << settings.totalTime << ','
                << renderer->GetComputeMilliseconds() << ',' << renderer->GetGraphicsMilliseconds() << ',' << frameMilliseconds << '\n';
        }
        if (!captureDirectory.empty() && frameCount >= warmup && (frameCount - warmup) % captureEvery == 0) {
            renderer->SaveFrame(captureDirectory + "/" + std::to_string(frameCount) + ".ppm");
        }
        ++frameCount;
    }

    vkDeviceWaitIdle(device->GetVkDevice());

    int result = EXIT_SUCCESS;
    try {
        if (verify || !dumpPath.empty() || !benchmarkPath.empty()) {
            uint32_t visible = renderer->ReadBackBlades(verify, dumpPath);
            printf("Final visible blades: %u / %u\n", visible, bladeCount);
        }
        if (!benchmarkPath.empty() && !benchmark) throw std::runtime_error("Failed to write benchmark");
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        result = EXIT_FAILURE;
    }

    delete renderer;
    delete scene;
    delete plane;
    delete blades;
    delete camera;
    vkDestroyImage(device->GetVkDevice(), grassImage, nullptr);
    vkFreeMemory(device->GetVkDevice(), grassImageMemory, nullptr);
    delete swapChain;
    delete device;
    vkDestroySurfaceKHR(instance->GetVkInstance(), surface, nullptr);
    delete instance;
    DestroyWindow();
    return result;
}


int main(int argc, char** argv) {
    int result = EXIT_SUCCESS;
    try {
        if (argc == 3 && std::strcmp(argv[1], "--batch") == 0) {
            // Each line contains stdout path, stderr path, then arguments, separated
            // by tabs. Keeping GLFW alive avoids repeating desktop initialization.
            std::ifstream batch(argv[2]);
            if (!batch) throw std::runtime_error("Cannot open batch file");
            std::ofstream report(std::string(argv[2]) + ".results");
            std::string line;
            while (std::getline(batch, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                std::istringstream stream(line);
                std::string stdoutPath, stderrPath, argument;
                std::getline(stream, stdoutPath, '\t');
                std::getline(stream, stderrPath, '\t');
                if (stdoutPath.empty() || stderrPath.empty()) throw std::runtime_error("Invalid batch line");
                if (!std::freopen(stdoutPath.c_str(), "w", stdout) || !std::freopen(stderrPath.c_str(), "w", stderr)) {
                    throw std::runtime_error("Cannot open batch logs");
                }
                std::vector<std::string> arguments = { argv[0] };
                while (std::getline(stream, argument, '\t')) arguments.push_back(argument);
                std::vector<char*> pointers;
                for (auto& value : arguments) pointers.push_back(&value[0]);
                result = runRenderer(static_cast<int>(pointers.size()), pointers.data());
                fflush(stdout);
                fflush(stderr);
                report << stdoutPath << '\t' << result << std::endl;
                if (result != EXIT_SUCCESS) break;
            }
        } else {
            result = runRenderer(argc, argv);
        }
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        result = EXIT_FAILURE;
    }
    glfwTerminate();
    return result;
}
