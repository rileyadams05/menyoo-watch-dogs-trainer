#include "dx11_hook.h"
#include "../memory/memory.h"
#include "../ui/ui.h"

#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

// -----------------------------------------------------------------------
// We hook IDXGISwapChain::Present by:
// 1. Creating a throw-away swap chain to read its vtable
// 2. Overwriting vtable slot 8 (Present) with our hook
// -----------------------------------------------------------------------

static ID3D11Device*           g_pd3dDevice    = nullptr;
static ID3D11DeviceContext*    g_pContext       = nullptr;
static ID3D11RenderTargetView* g_pRTV          = nullptr;
static IDXGISwapChain*         g_pSwapChain    = nullptr;
static HWND                    g_hGameWnd      = nullptr;
static bool                    g_initialized   = false;
static bool                    g_ready         = false;
static bool                    g_hooksAttached = false;

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn  = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static PresentFn g_originalPresent = nullptr;
static ResizeFn  g_originalResize  = nullptr;

// -----------------------------------------------------------------------
static HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!g_initialized)
    {
        // First call — grab device + context from the swap chain
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device),
            reinterpret_cast<void**>(&g_pd3dDevice))))
        {
            g_pd3dDevice->GetImmediateContext(&g_pContext);
            g_pSwapChain = pSwapChain;

            // Find game window
            DXGI_SWAP_CHAIN_DESC desc{};
            pSwapChain->GetDesc(&desc);
            g_hGameWnd = desc.OutputWindow;

            // Create RTV
            ID3D11Texture2D* pBackBuf = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&pBackBuf));
            if (pBackBuf)
            {
                g_pd3dDevice->CreateRenderTargetView(pBackBuf, nullptr, &g_pRTV);
                pBackBuf->Release();
            }

            // Initialize UI
            UI::Initialize(g_pd3dDevice, g_pContext, g_hGameWnd);

            g_initialized = true;
            g_ready       = true;
            LOG("DX11 hook initialized on first Present call");
        }
    }

    if (g_initialized && g_ready)
    {
        g_pContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
        UI::Render();
    }

    return g_originalPresent(pSwapChain, SyncInterval, Flags);
}

// -----------------------------------------------------------------------
static HRESULT __stdcall HookedResize(IDXGISwapChain* pSwapChain,
    UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT Flags)
{
    // Release old RTV before resize
    if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }

    HRESULT hr = g_originalResize(pSwapChain, BufferCount, Width, Height, NewFormat, Flags);

    // Recreate RTV after resize
    ID3D11Texture2D* pBackBuf = nullptr;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&pBackBuf))))
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuf, nullptr, &g_pRTV);
        pBackBuf->Release();
    }

    UI::OnResize(Width, Height);
    return hr;
}

// -----------------------------------------------------------------------
bool DX11Hook::Install()
{
    if (g_hooksAttached)
        return true;

    // Create a dummy device + swap chain to read the vtable pointers
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcA;
    wc.lpszClassName = "WDTrainerDummy";
    RegisterClassExA(&wc);
    HWND hDummy = CreateWindowExA(0, "WDTrainerDummy", "", WS_OVERLAPPED,
        0, 0, 1, 1, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount       = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow      = hDummy;
    scd.SampleDesc.Count  = 1;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device*        pDev   = nullptr;
    IDXGISwapChain*      pSC    = nullptr;
    ID3D11DeviceContext* pCtx   = nullptr;
    D3D_FEATURE_LEVEL    fl     = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &fl, 1, D3D11_SDK_VERSION, &scd,
        &pSC, &pDev, nullptr, &pCtx);

    if (FAILED(hr))
    {
        DestroyWindow(hDummy);
        LOG("DX11 dummy device creation failed: 0x%08X", hr);
        return false;
    }

    // Read vtable
    void** vtable = *reinterpret_cast<void***>(pSC);
    void* presentPtr = vtable[8];   // Present = slot 8
    void* resizePtr  = vtable[13];  // ResizeBuffers = slot 13

    pSC->Release();
    pDev->Release();
    pCtx->Release();
    DestroyWindow(hDummy);

    // Hook via MinHook
    MH_Initialize();

    if (MH_CreateHook(presentPtr, &HookedPresent,
        reinterpret_cast<void**>(&g_originalPresent)) != MH_OK)
    {
        LOG("Failed to create Present hook");
        MH_Uninitialize();
        return false;
    }
    if (MH_CreateHook(resizePtr, &HookedResize,
        reinterpret_cast<void**>(&g_originalResize)) != MH_OK)
    {
        LOG("Failed to create ResizeBuffers hook");
        MH_RemoveHook(presentPtr);
        MH_Uninitialize();
        return false;
    }

    MH_EnableHook(presentPtr);
    MH_EnableHook(resizePtr);
    g_hooksAttached = true;

    LOG("DX11 Present/ResizeBuffers hooks installed");
    return true;
}

void DX11Hook::Uninstall()
{
    g_ready = false;
    g_initialized = false;
    g_hooksAttached = false;
    if (g_originalPresent) MH_DisableHook(reinterpret_cast<void*>(g_originalPresent));
    if (g_originalResize)  MH_DisableHook(reinterpret_cast<void*>(g_originalResize));
    MH_Uninitialize();

    if (g_pRTV)     { g_pRTV->Release();     g_pRTV = nullptr; }
    if (g_pContext) { g_pContext->Release();  g_pContext = nullptr; }
    if (g_pd3dDevice){ g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

bool DX11Hook::IsReady() { return g_ready; }
