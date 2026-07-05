#pragma once
#include "../common.h"
#include <d3d11.h>

namespace UI
{
    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd);
    void Render();
    void OnResize(UINT width, UINT height);
    void Shutdown();

    bool IsMenuOpen();
    void SetMenuOpen(bool open);
    void ToggleMenu();
    void SetMenuHotkey(UINT vk);

    // WndProc hook for input handling
    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
}
