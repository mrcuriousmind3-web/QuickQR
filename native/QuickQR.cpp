#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <ZXing/ReadBarcode.h>
#include <ZXing/ReaderOptions.h>
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ImageView.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <new>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kClassName[] = L"QuickQR.Native.Window";
constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT WM_APP_RESULT = WM_APP + 2;
constexpr UINT WM_APP_STATUS = WM_APP + 3;
constexpr UINT WM_APP_CAMERA_DONE = WM_APP + 4;
constexpr int ID_SCAN_IMAGE = 1001;
constexpr int ID_CAMERA = 1002;
constexpr int ID_COPY = 1003;
constexpr int ID_OPEN_LINK = 1004;

HWND g_window = nullptr;
HWND g_scanButton = nullptr;
HWND g_cameraButton = nullptr;
HWND g_copyButton = nullptr;
HWND g_openButton = nullptr;
HWND g_resultEdit = nullptr;
HWND g_statusLabel = nullptr;
HFONT g_font = nullptr;
HFONT g_titleFont = nullptr;
HBRUSH g_windowBrush = nullptr;
HBRUSH g_panelBrush = nullptr;
RECT g_previewRect{};
std::shared_ptr<struct FrameBuffer> g_previewFrame;
std::wstring g_lastResult;
std::atomic_bool g_cameraStop{ false };
std::thread g_cameraThread;
bool g_cameraActive = false;
bool g_linkAvailable = false;
ComPtr<IWICImagingFactory> g_wicFactory;

struct FrameBuffer {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> bgra;
};

void SetStatus(const std::wstring& text)
{
    if (g_statusLabel)
        SetWindowTextW(g_statusLabel, text.c_str());
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        return L"QR code found, but its text could not be displayed.";
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::wstring DecodeQr(const FrameBuffer& frame)
{
    if (!frame.width || !frame.height || frame.bgra.empty())
        return {};
    try {
        const ZXing::ImageView image(frame.bgra.data(),
            static_cast<int>(frame.width),
            static_cast<int>(frame.height),
            ZXing::ImageFormat::BGRA,
            static_cast<int>(frame.width * 4));
        ZXing::ReaderOptions options;
        options.setFormats(ZXing::BarcodeFormat::QRCode);
        options.setTryHarder(true);
        options.setTryRotate(true);
        options.setTryInvert(true);
        options.setMaxNumberOfSymbols(8);
        const auto codes = ZXing::ReadBarcodes(image, options);
        std::wstring text;
        for (const auto& code : codes) {
            if (!code.isValid())
                continue;
            if (!text.empty())
                text += L"\r\n\r\n";
            text += Utf8ToWide(code.text());
        }
        return text;
    }
    catch (...) {
        return {};
    }
}

bool IsWebLink(const std::wstring& value)
{
    return value.rfind(L"https://", 0) == 0 || value.rfind(L"http://", 0) == 0;
}

void ShowResult(const std::wstring& value)
{
    g_lastResult = value;
    if (g_resultEdit)
        SetWindowTextW(g_resultEdit, value.c_str());
    g_linkAvailable = IsWebLink(value);
    if (g_copyButton)
        EnableWindow(g_copyButton, !value.empty());
    if (g_openButton)
        EnableWindow(g_openButton, g_linkAvailable);
    if (!value.empty())
        SetStatus(L"QR code decoded successfully. The content stays on this PC.");
}

void PostText(UINT message, const std::wstring& value)
{
    auto* text = new (std::nothrow) std::wstring(value);
    if (!text)
        return;
    if (!PostMessageW(g_window, message, 0, reinterpret_cast<LPARAM>(text)))
        delete text;
}

bool LoadImageFile(const wchar_t* path, FrameBuffer& output, std::wstring& error)
{
    if (!g_wicFactory) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(g_wicFactory.GetAddressOf()));
        if (FAILED(hr)) {
            error = L"Windows image support could not be initialized.";
            return false;
        }
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(path, nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Windows could not open this image file.";
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> sourceFrame;
    hr = decoder->GetFrame(0, sourceFrame.GetAddressOf());
    if (FAILED(hr)) {
        error = L"The image does not contain a readable frame.";
        return false;
    }

    UINT width = 0, height = 0;
    hr = sourceFrame->GetSize(&width, &height);
    constexpr uint64_t kMaximumPixels = 60000000;
    if (FAILED(hr) || width == 0 || height == 0 ||
        static_cast<uint64_t>(width) * height > kMaximumPixels) {
        error = L"This image is invalid or too large to scan (maximum 60 megapixels).";
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = g_wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Could not prepare the image for QR scanning.";
        return false;
    }
    hr = converter->Initialize(sourceFrame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        error = L"This image format is not supported by Windows Imaging.";
        return false;
    }

    const UINT stride = width * 4;
    const UINT size = stride * height;
    output.width = width;
    output.height = height;
    output.bgra.resize(size);
    hr = converter->CopyPixels(nullptr, stride, size, output.bgra.data());
    if (FAILED(hr)) {
        output = {};
        error = L"Could not read the image pixels.";
        return false;
    }
    return true;
}

void LayoutControls(HWND window)
{
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int margin = 24;
    const int gap = 10;
    const int buttonY = 70;
    const int buttonH = 36;
    const int available = width - margin * 2 - gap * 3;
    const int buttonW = std::max(90, available / 4);

    MoveWindow(g_scanButton, margin, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(g_cameraButton, margin + buttonW + gap, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(g_copyButton, margin + (buttonW + gap) * 2, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(g_openButton, margin + (buttonW + gap) * 3, buttonY, buttonW, buttonH, TRUE);

    g_previewRect = { margin, 120, width - margin, height - 170 };
    const int resultLabelY = height - 148;
    MoveWindow(g_resultEdit, margin, resultLabelY + 24, width - margin * 2, 76, TRUE);
    MoveWindow(g_statusLabel, margin, height - 34, width - margin * 2, 22, TRUE);
    InvalidateRect(window, nullptr, TRUE);
}

void SetFont(HWND control, HFONT font)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void DrainQueuedFrames()
{
    MSG message{};
    while (PeekMessageW(&message, g_window, WM_APP_FRAME, WM_APP_FRAME, PM_REMOVE)) {
        delete reinterpret_cast<FrameBuffer*>(message.lParam);
    }
    while (PeekMessageW(&message, g_window, WM_APP_RESULT, WM_APP_STATUS, PM_REMOVE)) {
        delete reinterpret_cast<std::wstring*>(message.lParam);
    }
}

void StopCamera()
{
    g_cameraStop.store(true);
    if (g_cameraThread.joinable())
        g_cameraThread.join();
    g_cameraActive = false;
    if (g_cameraButton)
        SetWindowTextW(g_cameraButton, L"Start camera");
    DrainQueuedFrames();
}

void CameraWorker()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    std::wstring error;

    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;

    if (SUCCEEDED(hr))
        hr = MFCreateAttributes(attributes.GetAddressOf(), 1);
    if (SUCCEEDED(hr))
        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr))
        hr = MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount);

    if (SUCCEEDED(hr) && deviceCount == 0) {
        error = L"No webcam was found. Connect a camera or choose Scan image.";
        hr = MF_E_NOT_FOUND;
    }
    if (SUCCEEDED(hr))
        hr = devices[0]->ActivateObject(IID_PPV_ARGS(source.GetAddressOf()));

    if (devices) {
        for (UINT32 i = 0; i < deviceCount; ++i)
            devices[i]->Release();
        CoTaskMemFree(devices);
    }

    if (SUCCEEDED(hr))
        hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, reader.GetAddressOf());

    ComPtr<IMFMediaType> nativeType;
    ComPtr<IMFMediaType> outputType;
    UINT32 width = 640, height = 480;
    if (SUCCEEDED(hr))
        hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
            nativeType.GetAddressOf());
    if (SUCCEEDED(hr)) {
        UINT32 nativeWidth = 0, nativeHeight = 0;
        if (SUCCEEDED(MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE,
            &nativeWidth, &nativeHeight))) {
            width = nativeWidth;
            height = nativeHeight;
        }
        hr = MFCreateMediaType(outputType.GetAddressOf());
    }
    if (SUCCEEDED(hr))
        hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr))
        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr))
        hr = MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height);
    if (SUCCEEDED(hr))
        hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr))
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            nullptr, outputType.Get());

    ComPtr<IMFMediaType> currentType;
    LONG stride = static_cast<LONG>(width * 4);
    if (SUCCEEDED(hr)) {
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            currentType.GetAddressOf());
        UINT32 currentWidth = 0, currentHeight = 0;
        if (SUCCEEDED(hr) && SUCCEEDED(MFGetAttributeSize(currentType.Get(),
            MF_MT_FRAME_SIZE, &currentWidth, &currentHeight))) {
            width = currentWidth;
            height = currentHeight;
        }
        UINT32 rawStride = 0;
        if (currentType && SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride)))
            stride = static_cast<LONG>(rawStride);
        if (stride == 0)
            stride = static_cast<LONG>(width * 4);
        stride = std::abs(stride);
    }

    auto lastDecode = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    int frameCounter = 0;

    if (FAILED(hr) && error.empty())
        error = L"Could not initialize the webcam. Check camera permissions and try again.";

    while (SUCCEEDED(hr) && !g_cameraStop.load()) {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
            &streamIndex, &flags, &timestamp, sample.GetAddressOf());
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            break;
        if (!sample)
            continue;

        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
        if (FAILED(hr))
            continue;

        BYTE* bytes = nullptr;
        DWORD maxLength = 0, currentLength = 0;
        hr = buffer->Lock(&bytes, &maxLength, &currentLength);
        if (FAILED(hr))
            continue;

        const uint64_t needed = static_cast<uint64_t>(stride) * height;
        if (currentLength >= needed && width && height) {
            auto frame = std::make_unique<FrameBuffer>();
            frame->width = width;
            frame->height = height;
            frame->bgra.resize(static_cast<size_t>(width) * height * 4);
            for (UINT y = 0; y < height; ++y) {
                std::memcpy(frame->bgra.data() + static_cast<size_t>(y) * width * 4,
                    bytes + static_cast<size_t>(y) * stride,
                    static_cast<size_t>(width) * 4);
            }

            ++frameCounter;
            if (frameCounter % 2 == 0) {
                auto* frameMessage = new (std::nothrow) FrameBuffer(*frame);
                if (frameMessage && !PostMessageW(g_window, WM_APP_FRAME, 0,
                    reinterpret_cast<LPARAM>(frameMessage)))
                    delete frameMessage;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - lastDecode >= std::chrono::milliseconds(260)) {
                lastDecode = now;
                const std::wstring decoded = DecodeQr(*frame);
                if (!decoded.empty()) {
                    PostText(WM_APP_RESULT, decoded);
                    break;
                }
            }
        }
        buffer->Unlock();
    }

    if (reader)
        reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
    if (source) {
        source->Shutdown();
        source.Reset();
    }
    reader.Reset();
    MFShutdown();
    CoUninitialize();

    if (!error.empty())
        PostText(WM_APP_STATUS, error);
    PostMessageW(g_window, WM_APP_CAMERA_DONE, 0, 0);
}

void StartCamera()
{
    if (g_cameraActive)
        return;
    g_cameraStop.store(false);
    g_cameraActive = true;
    SetWindowTextW(g_cameraButton, L"Stop camera");
    SetStatus(L"Opening webcam…");
    try {
        g_cameraThread = std::thread(CameraWorker);
    }
    catch (...) {
        g_cameraActive = false;
        SetWindowTextW(g_cameraButton, L"Start camera");
        SetStatus(L"Could not start the camera worker thread.");
    }
}

void ChooseImage()
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.wdp;*.jfif\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Choose an image containing a QR code";
    if (!GetOpenFileNameW(&dialog))
        return;

    SetStatus(L"Loading image and looking for QR codes…");
    FrameBuffer image;
    std::wstring error;
    if (!LoadImageFile(path, image, error)) {
        SetStatus(error);
        return;
    }
    g_previewFrame = std::make_shared<FrameBuffer>(image);
    InvalidateRect(g_window, &g_previewRect, TRUE);
    const std::wstring decoded = DecodeQr(image);
    if (decoded.empty()) {
        ShowResult(L"");
        SetStatus(L"No QR code found in this image. Try a clearer image.");
    } else {
        ShowResult(decoded);
    }
}

void CopyResult()
{
    if (g_lastResult.empty() || !OpenClipboard(g_window))
        return;
    EmptyClipboard();
    const size_t bytes = (g_lastResult.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* data = GlobalLock(memory);
        if (data) {
            memcpy(data, g_lastResult.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    SetStatus(L"QR content copied to clipboard.");
}

void OpenResultLink()
{
    if (g_linkAvailable)
        ShellExecuteW(g_window, L"open", g_lastResult.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void CreateControls(HWND window)
{
    g_scanButton = CreateWindowExW(0, L"BUTTON", L"Scan image",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
        window, reinterpret_cast<HMENU>(ID_SCAN_IMAGE), nullptr, nullptr);
    g_cameraButton = CreateWindowExW(0, L"BUTTON", L"Start camera",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
        window, reinterpret_cast<HMENU>(ID_CAMERA), nullptr, nullptr);
    g_copyButton = CreateWindowExW(0, L"BUTTON", L"Copy result",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
        window, reinterpret_cast<HMENU>(ID_COPY), nullptr, nullptr);
    g_openButton = CreateWindowExW(0, L"BUTTON", L"Open link",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
        window, reinterpret_cast<HMENU>(ID_OPEN_LINK), nullptr, nullptr);
    g_resultEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, 0, 10, 10, window, nullptr, nullptr, nullptr);
    g_statusLabel = CreateWindowExW(0, L"STATIC", L"Ready. Scan an image or start your camera.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, window, nullptr, nullptr, nullptr);

    EnableWindow(g_copyButton, FALSE);
    EnableWindow(g_openButton, FALSE);
    SetFont(g_scanButton, g_font);
    SetFont(g_cameraButton, g_font);
    SetFont(g_copyButton, g_font);
    SetFont(g_openButton, g_font);
    SetFont(g_resultEdit, g_font);
    SetFont(g_statusLabel, g_font);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateControls(window);
        DragAcceptFiles(window, TRUE);
        return 0;

    case WM_SIZE:
        LayoutControls(window);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 560;
        info->ptMinTrackSize.y = 620;
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_SCAN_IMAGE:
            ChooseImage();
            return 0;
        case ID_CAMERA:
            if (g_cameraActive) {
                StopCamera();
                SetStatus(L"Camera stopped.");
            } else {
                StartCamera();
            }
            return 0;
        case ID_COPY:
            CopyResult();
            return 0;
        case ID_OPEN_LINK:
            OpenResultLink();
            return 0;
        }
        break;

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH] = {};
        if (DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
            FrameBuffer image;
            std::wstring error;
            if (LoadImageFile(path, image, error)) {
                g_previewFrame = std::make_shared<FrameBuffer>(image);
                InvalidateRect(window, &g_previewRect, TRUE);
                const auto decoded = DecodeQr(image);
                if (decoded.empty()) {
                    ShowResult(L"");
                    SetStatus(L"No QR code found in this image.");
                } else {
                    ShowResult(decoded);
                }
            } else {
                SetStatus(error);
            }
        }
        DragFinish(drop);
        return 0;
    }

    case WM_APP_FRAME: {
        std::unique_ptr<FrameBuffer> frame(reinterpret_cast<FrameBuffer*>(lParam));
        if (frame) {
            g_previewFrame = std::make_shared<FrameBuffer>(std::move(*frame));
            InvalidateRect(window, &g_previewRect, FALSE);
        }
        return 0;
    }

    case WM_APP_RESULT: {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring*>(lParam));
        if (result)
            ShowResult(*result);
        return 0;
    }

    case WM_APP_STATUS: {
        std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lParam));
        if (status)
            SetStatus(*status);
        return 0;
    }

    case WM_APP_CAMERA_DONE:
        if (g_cameraThread.joinable())
            g_cameraThread.join();
        g_cameraActive = false;
        if (g_cameraButton)
            SetWindowTextW(g_cameraButton, L"Start camera");
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(220, 228, 239));
        SetBkColor(dc, RGB(18, 23, 33));
        return reinterpret_cast<LRESULT>(g_windowBrush);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(232, 239, 248));
        SetBkColor(dc, RGB(24, 31, 43));
        return reinterpret_cast<LRESULT>(g_panelBrush);
    }

    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, g_windowBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 248, 252));
        SelectObject(dc, g_titleFont);
        RECT titleRect{ 24, 17, client.right - 24, 48 };
        DrawTextW(dc, L"QuickQR", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, g_font);
        SetTextColor(dc, RGB(151, 165, 185));
        RECT subtitle{ 25, 43, client.right - 24, 64 };
        DrawTextW(dc, L"Lightweight QR scanner  •  Local decoding  •  No sign-in",
            -1, &subtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT label{ 24, 105, client.right - 24, 120 };
        SetTextColor(dc, RGB(164, 179, 199));
        DrawTextW(dc, L"CAMERA / IMAGE PREVIEW", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HBRUSH previewBrush = CreateSolidBrush(RGB(9, 13, 19));
        FillRect(dc, &g_previewRect, previewBrush);
        DeleteObject(previewBrush);
        HPEN border = CreatePen(PS_SOLID, 1, RGB(54, 66, 84));
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, g_previewRect.left, g_previewRect.top,
            g_previewRect.right, g_previewRect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(border);

        if (g_previewFrame && !g_previewFrame->bgra.empty()) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(g_previewFrame->width);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(g_previewFrame->height);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            const int targetW = g_previewRect.right - g_previewRect.left - 8;
            const int targetH = g_previewRect.bottom - g_previewRect.top - 8;
            const double scale = std::min(
                static_cast<double>(targetW) / g_previewFrame->width,
                static_cast<double>(targetH) / g_previewFrame->height);
            const int drawW = static_cast<int>(g_previewFrame->width * scale);
            const int drawH = static_cast<int>(g_previewFrame->height * scale);
            const int x = g_previewRect.left + 4 + (targetW - drawW) / 2;
            const int y = g_previewRect.top + 4 + (targetH - drawH) / 2;
            StretchDIBits(dc, x, y, drawW, drawH, 0, 0,
                g_previewFrame->width, g_previewFrame->height,
                g_previewFrame->bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        } else {
            SetTextColor(dc, RGB(112, 128, 151));
            RECT empty = g_previewRect;
            DrawTextW(dc, L"Start your camera or choose an image to scan",
                -1, &empty, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SetTextColor(dc, RGB(164, 179, 199));
        RECT resultLabel{ 24, client.bottom - 148, client.right - 24, client.bottom - 126 };
        DrawTextW(dc, L"DECODED CONTENT", -1, &resultLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(window, &ps);
        return 0;
    }

    case WM_CLOSE:
        StopCamera();
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_windowBrush = CreateSolidBrush(RGB(18, 23, 33));
    g_panelBrush = CreateSolidBrush(RGB(24, 31, 43));
    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = g_windowBrush;
    RegisterClassExW(&wc);

    g_window = CreateWindowExW(0, kClassName, L"QuickQR",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 820,
        nullptr, nullptr, instance, nullptr);

    if (!g_window) {
        MessageBoxW(nullptr, L"QuickQR could not create its main window.",
            L"QuickQR", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DrainQueuedFrames();
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_font) DeleteObject(g_font);
    if (g_panelBrush) DeleteObject(g_panelBrush);
    if (g_windowBrush) DeleteObject(g_windowBrush);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
