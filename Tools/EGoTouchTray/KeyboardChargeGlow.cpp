#include "KeyboardChargeGlow.h"
#include <d2d1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>
#include <shellscalingapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shcore.lib")

namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t kClassName[] = L"OpenEGoHub.KeyboardChargeGlow";
constexpr UINT_PTR kCleanupTimer = 1;
constexpr UINT kCleanupMs = 1100;
constexpr UINT kStopMessage = WM_APP + 0x7b1;
constexpr float kBandDip = 36.0f;
constexpr float kPeakAlpha = 0.72f;

struct Slice { D2D1_RECT_F source, destination; };
std::array<Slice, 8> MakeSlices(float width, float height, float band) {
    const float b = band, twice = b * 2.0f, atlas = b * 3.0f;
    return {{
        {{0,0,b,b}, {0,0,b,b}},
        {{twice,0,atlas,b}, {width-b,0,width,b}},
        {{0,twice,b,atlas}, {0,height-b,b,height}},
        {{twice,twice,atlas,atlas}, {width-b,height-b,width,height}},
        {{b,0,twice,b}, {b,0,width-b,b}},
        {{b,twice,twice,atlas}, {b,height-b,width-b,height}},
        {{0,b,b,twice}, {0,b,b,height-b}},
        {{twice,b,atlas,twice}, {width-b,b,width,height-b}},
    }};
}

struct PrimaryMonitor { HMONITOR monitor = nullptr; RECT bounds{}; };
BOOL CALLBACK FindPrimary(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info) && (info.dwFlags & MONITORINFOF_PRIMARY)) {
        auto& result = *reinterpret_cast<PrimaryMonitor*>(context);
        result.monitor = monitor;
        result.bounds = info.rcMonitor; // Full screen, including the taskbar.
        return FALSE;
    }
    return TRUE;
}

HRESULT DrawAtlas(IDCompositionSurface* surface, ID2D1Factory* factory, float band) {
    ComPtr<IDXGISurface> texture;
    POINT offset{};
    HRESULT hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&texture), &offset);
    if (FAILED(hr)) return hr;
    // EndDraw must pair even with a failed render-target/brush creation.
    const auto draw = [&]() -> HRESULT {
        ComPtr<ID2D1RenderTarget> target;
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        HRESULT result = factory->CreateDxgiSurfaceRenderTarget(texture.Get(), &properties, &target);
        if (FAILED(result)) return result;
        D2D1_GRADIENT_STOP edgeStops[]{
            {0.0f, D2D1::ColorF(1,1,1,kPeakAlpha)},
            {0.25f,D2D1::ColorF(1,1,1,0.22f)},
            {0.60f,D2D1::ColorF(1,1,1,0.06f)},
            {1.0f, D2D1::ColorF(1,1,1,0.0f)},
        };
        D2D1_GRADIENT_STOP cornerStops[]{
            {0.0f, D2D1::ColorF(1,1,1,0.0f)},
            {0.40f,D2D1::ColorF(1,1,1,0.06f)},
            {0.75f,D2D1::ColorF(1,1,1,0.22f)},
            {1.0f, D2D1::ColorF(1,1,1,kPeakAlpha)},
        };
        ComPtr<ID2D1GradientStopCollection> edges, corners;
        result = target->CreateGradientStopCollection(edgeStops, 4, D2D1_GAMMA_1_0,
            D2D1_EXTEND_MODE_CLAMP, &edges);
        if (FAILED(result)) return result;
        result = target->CreateGradientStopCollection(cornerStops, 4, D2D1_GAMMA_1_0,
            D2D1_EXTEND_MODE_CLAMP, &corners);
        if (FAILED(result)) return result;
        ComPtr<ID2D1LinearGradientBrush> edge;
        ComPtr<ID2D1RadialGradientBrush> corner;
        result = target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0,0), D2D1::Point2F(0,band)), edges.Get(), &edge);
        if (FAILED(result)) return result;
        result = target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(band,band), D2D1::Point2F(0,0), band,band), corners.Get(), &corner);
        if (FAILED(result)) return result;
        const float b = band, twice = b*2, size = b*3;
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
        target->PushAxisAlignedClip(D2D1::RectF(0,0,size,size), D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(D2D1::ColorF(0,0,0,0));
        const auto strip = [&](D2D1_RECT_F rectangle, D2D1_POINT_2F start, D2D1_POINT_2F end) {
            edge->SetStartPoint(start); edge->SetEndPoint(end); target->FillRectangle(rectangle, edge.Get());
        };
        strip(D2D1::RectF(b,0,twice,b), D2D1::Point2F(0,0), D2D1::Point2F(0,b));
        strip(D2D1::RectF(b,twice,twice,size), D2D1::Point2F(0,size), D2D1::Point2F(0,twice));
        strip(D2D1::RectF(0,b,b,twice), D2D1::Point2F(0,0), D2D1::Point2F(b,0));
        strip(D2D1::RectF(twice,b,size,twice), D2D1::Point2F(size,0), D2D1::Point2F(twice,0));
        // Four radial tiles join the strips continuously, giving rounded inner
        // corners without double-blending overlapping edge rectangles.
        const auto rounded = [&](D2D1_RECT_F rectangle, D2D1_POINT_2F center) {
            corner->SetCenter(center); target->FillRectangle(rectangle, corner.Get());
        };
        rounded(D2D1::RectF(0,0,b,b), D2D1::Point2F(b,b));
        rounded(D2D1::RectF(twice,0,size,b), D2D1::Point2F(twice,b));
        rounded(D2D1::RectF(0,twice,b,size), D2D1::Point2F(b,twice));
        rounded(D2D1::RectF(twice,twice,size,size), D2D1::Point2F(twice,twice));
        target->PopAxisAlignedClip();
        return target->EndDraw();
    };
    hr = draw();
    const HRESULT ended = surface->EndDraw();
    return FAILED(hr) ? hr : ended;
}
}

struct KeyboardChargeGlow::Impl : std::enable_shared_from_this<Impl> {
    HWND owner = nullptr;
    std::atomic<HWND> window{nullptr};
    DWORD thread = 0;
    bool playing = false;
    std::shared_ptr<Impl> windowLifetime;
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> root;
    ComPtr<IDCompositionSurface> surface;
    ComPtr<IDCompositionAnimation> animation;
    ComPtr<IDCompositionEffectGroup> opacity;

    void ReleaseGraphics() {
        if (opacity) opacity->SetOpacity(0.0f);
        if (root) root->SetContent(nullptr);
        if (target) target->SetRoot(nullptr);
        if (composition) composition->Commit();
        animation.Reset(); opacity.Reset(); root.Reset(); target.Reset(); surface.Reset(); composition.Reset(); d3d.Reset();
    }
    void Finish() {
        playing = false;
        if (!window) { ReleaseGraphics(); return; }
        const HWND closing = window;
        KillTimer(closing, kCleanupTimer);
        ShowWindow(closing, SW_HIDE);
        ReleaseGraphics();
        DestroyWindow(closing);
    }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        auto self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        // This local reference also protects a reentrant WM_NCDESTROY during Finish.
        const auto lifetime = self ? self->windowLifetime : nullptr;
        if (self) switch (message) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(hwnd,&paint); EndPaint(hwnd,&paint); return 0; }
        case WM_TIMER: if (wParam == kCleanupTimer) { self->Finish(); return 0; } break;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (self->playing) self->Finish();
            return 0;
        case WM_CLOSE:
        case kStopMessage: self->Finish(); return 0;
        case WM_NCDESTROY:
            KillTimer(hwnd, kCleanupTimer);
            self->playing = false; self->window = nullptr;
            self->ReleaseGraphics();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            self->windowLifetime.reset();
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    bool Compose(float width, float height, float band) {
        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
        if (FAILED(hr)) return false; // Optional decoration: no software rendering loop fallback.
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d.As(&dxgi))) return false;
        if (FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition)))) return false;
        if (FAILED(composition->CreateTargetForHwnd(window, TRUE, &target))) return false;
        if (FAILED(composition->CreateVisual(&root))) return false;
        ComPtr<ID2D1Factory> factory;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return false;
        const UINT textureSize = static_cast<UINT>(band)*3;
        if (FAILED(composition->CreateSurface(textureSize, textureSize, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
        if (FAILED(DrawAtlas(surface.Get(), factory.Get(), band))) return false;
        for (const auto& slice : MakeSlices(width, height, band)) {
            ComPtr<IDCompositionVisual> visual;
            if (FAILED(composition->CreateVisual(&visual))) return false;
            const auto& s = slice.source; const auto& d = slice.destination;
            const float sx = (d.right-d.left)/(s.right-s.left), sy = (d.bottom-d.top)/(s.bottom-s.top);
            if (FAILED(visual->SetContent(surface.Get())) || FAILED(visual->SetClip(s)) ||
                FAILED(visual->SetTransform(D2D1::Matrix3x2F(sx,0,0,sy,d.left-s.left*sx,d.top-s.top*sy))) ||
                FAILED(root->AddVisual(visual.Get(), FALSE, nullptr))) return false;
        }
        if (FAILED(composition->CreateAnimation(&animation))) return false;
        if (FAILED(composition->CreateEffectGroup(&opacity))) return false;
        constexpr float rise = 0.18f, fall = 0.70f;
        if (FAILED(animation->AddCubic(0,0,0,3/(rise*rise),-2/(rise*rise*rise))) ||
            FAILED(animation->AddCubic(0.18,1,0,0,0)) ||
            FAILED(animation->AddCubic(0.30,1,0,-3/(fall*fall),2/(fall*fall*fall))) ||
            FAILED(animation->End(1.0,0)) || FAILED(opacity->SetOpacity(animation.Get())) ||
            FAILED(root->SetEffect(opacity.Get())) ||
            FAILED(target->SetRoot(root.Get()))) return false;
        return SUCCEEDED(composition->Commit());
    }
    void Play() {
        if (playing || GetCurrentThreadId() != thread || !IsWindow(owner)) return;
        PrimaryMonitor primary;
        EnumDisplayMonitors(nullptr,nullptr,FindPrimary,reinterpret_cast<LPARAM>(&primary));
        const LONG width = primary.bounds.right-primary.bounds.left, height = primary.bounds.bottom-primary.bounds.top;
        if (!primary.monitor || width < 4 || height < 4) return;
        UINT dpiX = 96, dpiY = 96;
        if (FAILED(GetDpiForMonitor(primary.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
        const float band = std::max(1.0f, std::min(std::ceil(kBandDip*static_cast<float>(dpiX)/96.0f),
            std::floor(static_cast<float>(std::min(width,height))/2.0f)));
        windowLifetime = shared_from_this();
        // WS_EX_TRANSPARENT by itself only controls painting order; the
        // documented cross-process mouse pass-through contract requires a
        // layered window. DirectComposition supports layered target HWNDs.
        // https://learn.microsoft.com/windows/win32/winmsg/window-features#layered-windows
        // https://learn.microsoft.com/windows/win32/directcomp/basic-concepts#composition-target-window
        window = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP,
            kClassName,L"",WS_POPUP|WS_DISABLED,primary.bounds.left,primary.bounds.top,width,height,
            owner,nullptr,GetModuleHandleW(nullptr),this);
        if (!window) { windowLifetime.reset(); return; }
        // Initialize the layered HWND once at full global alpha. The small
        // premultiplied surface and all fade animation remain in DirectComposition.
        if (!SetLayeredWindowAttributes(window,0,255,LWA_ALPHA)) { Finish(); return; }
        if (!Compose(static_cast<float>(width),static_cast<float>(height),band)) { Finish(); return; }
        playing = true;
        // A single cleanup timer, never a per-frame redraw/opacity CPU loop.
        if (!SetTimer(window,kCleanupTimer,kCleanupMs,nullptr)) { Finish(); return; }
        SetWindowPos(window,HWND_TOPMOST,primary.bounds.left,primary.bounds.top,width,height,
            SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
    }
};

KeyboardChargeGlow::KeyboardChargeGlow() = default;
KeyboardChargeGlow::~KeyboardChargeGlow() { Stop(); }
bool KeyboardChargeGlow::Start(HWND owner) {
    Stop();
    DWORD process = 0;
    if (!IsWindow(owner) || GetWindowThreadProcessId(owner,&process) != GetCurrentThreadId() || process != GetCurrentProcessId()) return false;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc=Impl::WndProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=kClassName;
    if (!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
    try { m_impl=std::make_shared<Impl>(); } catch (...) { return false; }
    m_impl->owner=owner; m_impl->thread=GetCurrentThreadId();
    return true;
}
void KeyboardChargeGlow::Play() { if (m_impl) m_impl->Play(); }
void KeyboardChargeGlow::Stop() {
    auto state=std::move(m_impl);
    if (!state) return;
    if (GetCurrentThreadId()==state->thread) { state->Finish(); return; }
    if (const HWND closing=state->window.load()) {
        DWORD_PTR ignored=0;
        // A private message cannot accidentally close a different window if a
        // timer destroyed this HWND just before an off-thread stop was posted.
        if (!SendMessageTimeoutW(closing,kStopMessage,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&ignored))
            PostMessageW(closing,kStopMessage,0,0);
    }
}
