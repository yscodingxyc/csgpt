#include <iostream>
#include <thread>
#include <chrono>
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <dxgi1_2.h>


#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "classes/utils.h"
#include "memory/memory.hpp"
#include "classes/vector.hpp"
#include "hacks/reader.hpp"
#include "hacks/hack.hpp"
#include "classes/globals.hpp"
#include "classes/render.hpp"
#include "classes/auto_updater.hpp"

// DirectX globals
static ID3D11BlendState* g_pBlendState = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Test Vars 
ImVec4 clear_color = ImVec4(0.424f, 0.000f, 0.000f, 0.000f);

// Menu variables
bool show_menu = true;
bool finish = false;

// Forward declarations
//bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
//void RenderImGuiMenu();

// ImGui WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_CREATE:
    {
        // Set window to layered, transparent, and click-through
        SetWindowLong(hWnd, GWL_EXSTYLE,
            GetWindowLong(hWnd, GWL_EXSTYLE) |
            WS_EX_LAYERED |
            WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE);

        // Set transparency color key to black
        if (!SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY)) {
            std::cout << "[error] SetLayeredWindowAttributes failed: " << GetLastError() << std::endl;
        }

        std::cout << "[overlay] Window created successfully" << std::endl;
        Beep(500, 100);
        break;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_DESTROY:
        CleanupDeviceD3D();
        PostQuitMessage(0);
        break;
    case WM_KEYDOWN:
        if (wParam == VK_INSERT)
            show_menu = !show_menu;
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    // Set up the swap chain description
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Important for transparency
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;

    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hrs = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hrs))
    {
        std::cout << "[error] Failed to create D3D11 device and swap chain: " << hrs << std::endl;
        return false;
    }

    // Create a proper blend state for transparent rendering
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = g_pd3dDevice->CreateBlendState(&blendDesc, &g_pBlendState);
    if (FAILED(hr))
    {
        std::cout << "[error] Failed to create blend state: " << hr << std::endl;
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pBlendState) { g_pBlendState->Release(); g_pBlendState = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr))
    {
        std::cout << "[error] Failed to get back buffer: " << hr << std::endl;
        return;
    }

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr))
    {
        std::cout << "[error] Failed to create render target view: " << hr << std::endl;
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void RenderImGuiMenu()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (GetForegroundWindow() == g_game.process->hwnd_)
    {
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        draw_list->AddText(ImVec2(10, 10), IM_COL32(75, 175, 175, 255), "cs2 | ESP");
        hack::loop();
    }

    if (show_menu)
    {

        
        ImVec2 windowSize = ImVec2(500.0f, 300.0f);
        ImGui::SetNextWindowSize(windowSize);

        ImGui::Begin("##window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        ImGui::Text("0x");
        ImGui::SameLine();                                                  // Using sameline ´for the title
        ImGui::TextColored(ImVec4(0.749f, 0.259f, 0.259f, 1.0f), "985722"); // TextColored and ImVec4 so we can use rgb || TODO  add colorpicker for the last word (world)
        ImGui::Checkbox("ESP", &);
        //ImGui::Checkbox("DebugMenu", &g_ShowDebugConsole);

        //ImGui::SliderFloat("float", &f, 0.0f, 1.0f);                        // Edit 1 float using a slider from 0.0f to 1.0f                                                                     
        ImGui::ColorEdit4("clear color", (float*)&clear_color);             //using coloredit4 cuz with 3 there is no alpha box lol     Edit 3 floats representing a color and  alpha 

        if (ImGui::Button("Button"))                                        // Buttons return true when clicked (most widgets return true when edited/activated)
            
        ImGui::SameLine();
        
        ImGui::Separator();
        if (ImGui::Checkbox("Box ESP", &config::show_box_esp)) config::save();
        ImGui::End();
    }

    ImGui::Render();

    // Set the clear color to pure black with 0 alpha (this black will be made transparent by LWA_COLORKEY)
    const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);

    // Apply blend state with proper blend factors
    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_pd3dDeviceContext->OMSetBlendState(g_pBlendState, blendFactor, 0xFFFFFFFF);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = g_pSwapChain->Present(1, 0);
    if (FAILED(hr))
        std::cout << "[error] Swap chain present failed: " << hr << std::endl;
}

void read_thread() {
    while (!finish) {
        g_game.loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

int main() {
    utils.update_console_title();

    std::cout << "[config] Reading configuration." << std::endl;
    config::read();

#ifndef _UC
    updater::check_and_update(config::automatic_update);
#endif

    std::cout << "[updater] Reading offsets from file offsets.json." << std::endl;
    updater::read();

    g_game.init();

    if (g_game.buildNumber != updater::build_number) {
        std::cout << "[cs2] Build number mismatch - game may have been updated." << std::endl;
        std::cout << "[warn] ESP might not work properly." << std::endl;
    }

    std::cout << "[overlay] Waiting for game focus..." << std::endl;
    while (GetForegroundWindow() != g_game.process->hwnd_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        g_game.process->UpdateHWND();
    }

    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = NULL; // No background brush - IMPORTANT
    wc.lpszClassName = "cs2-external-esp";
    RegisterClassExA(&wc);

    GetClientRect(g_game.process->hwnd_, &g::gameBounds);
    std::cout << "[debug] Game bounds: " << g::gameBounds.left << "," << g::gameBounds.top << " - " << g::gameBounds.right << "," << g::gameBounds.bottom << std::endl;

    // Create the window with proper styles
    HWND hWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        "cs2-external-esp", "cs2-external-esp", WS_POPUP,
        g::gameBounds.left, g::gameBounds.top,
        g::gameBounds.right - g::gameBounds.left,
        g::gameBounds.bottom - g::gameBounds.top,
        NULL, NULL, wc.hInstance, NULL);

    if (!hWnd) {
        std::cout << "[error] Failed to create window: " << GetLastError() << std::endl;
        return 0;
    }

    // Set the black color as transparent using color keying
    if (!SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY)) {
        std::cout << "[error] SetLayeredWindowAttributes failed: " << GetLastError() << std::endl;
    }

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        return 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Configure ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 1.0f; // Full opacity for ImGui elements
    style.Colors[ImGuiCol_WindowBg].w = 0.9f; // Slightly transparent window background

    if (!ImGui_ImplWin32_Init(hWnd) || !ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext))
    {
        std::cout << "[error] Failed to initialize ImGui" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "[initialize ImGui] working" << std::endl;
    }

    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);

    std::thread read(read_thread);
    std::cout << "[menu] Press INSERT to toggle menu" << std::endl;

    MSG msg = {};
    auto last_frame = std::chrono::steady_clock::now();

    while (!finish)
    {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                finish = true;
        }

        auto now = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame).count();

        if (delta >= 16) // ~60 FPS
        {
            if (GetForegroundWindow() == g_game.process->hwnd_)
            {
                RECT newRect;
                GetClientRect(g_game.process->hwnd_, &newRect);
                if (memcmp(&newRect, &g::gameBounds, sizeof(RECT)) != 0)
                {
                    GetClientRect(g_game.process->hwnd_, &g::gameBounds);
                    SetWindowPos(hWnd, HWND_TOPMOST,
                        g::gameBounds.left, g::gameBounds.top,
                        g::gameBounds.right - g::gameBounds.left,
                        g::gameBounds.bottom - g::gameBounds.top,
                        SWP_NOACTIVATE);
                }
                RenderImGuiMenu();
            }
            last_frame = now;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    read.join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    g_game.close();

#ifdef NDEBUG
    std::cout << "[cs2] Press any key to close" << std::endl;
    std::cin.get();
#endif

    return 0;
}