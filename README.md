# QuickQR

**A native, ultra-lightweight Windows QR scanner.** Download one EXE and run it. No .NET, WebView2, Python, Java, browser, or separate runtime installer is required.

## Features

- Scan QR codes from your webcam.
- Scan PNG, JPG, BMP, GIF, TIFF and other Windows Imaging Component image formats.
- Drag and drop an image into the app.
- Copy decoded text to the clipboard.
- Open a decoded HTTP/HTTPS link only when you click **Open link**.
- QR decoding runs locally using the statically linked ZXing-C++ engine.
- Native Win32 interface with Windows Media Foundation camera capture and Windows Imaging Component image loading.

## Download and run

1. Open the [QuickQR Releases page](https://github.com/mrcuriousmind3-web/QuickQR/releases).
2. Download **QuickQR.exe**.
3. Double-click it. That's it.

Windows 10/11 x64 is required. QuickQR is statically linked with the MSVC C/C++ runtime. Its only runtime dependencies are Windows system components (Win32, Windows Imaging Component and Media Foundation).

## Build from source

The build requires Visual Studio 2022 C++ tools, CMake 3.24+, and an internet connection to download ZXing-C++ v2.3.0 during the build. The resulting app does not need internet access for QR scanning.

```powershell
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The executable is written to `build/Release/QuickQR.exe`.

## Camera troubleshooting

If Windows blocks camera access, enable **Settings → Privacy & security → Camera → Let desktop apps access your camera**. QuickQR does not need a separate camera driver or plugin.

## Privacy

Camera frames and image files are processed locally. QR text is not uploaded. QR links are not launched automatically.

## Licenses

- QuickQR application code: MIT.
- ZXing-C++: Apache-2.0. It is linked statically into the executable; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
