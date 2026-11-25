# QuickQR

A compact Windows QR-code scanner. No Electron, no Python, no ads, no telemetry.

## Features

- Scan QR codes using a webcam.
- Scan QR codes from image files.
- Drag and drop an image into the window.
- Copy decoded text with one click.
- Open decoded web links in your default browser only when you click Open.
- The scanner UI and jsQR decoder are embedded in the EXE; QR decoding does not need an internet connection.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- **.NET 10 Desktop Runtime x64**. The plain .NET Runtime and ASP.NET Core Runtime are not sufficient for this Windows Forms application.
- Microsoft Edge WebView2 Runtime. The release ZIP includes Microsoft's small Evergreen Bootstrapper; the app will offer to launch it if WebView2 is missing.

Download the **.NET 10 Desktop Runtime x64** from the official Microsoft page: https://dotnet.microsoft.com/en-us/download/dotnet/10.0

Download the **Microsoft Edge WebView2 Runtime** from: https://developer.microsoft.com/en-us/microsoft-edge/webview2/

## Run from source

```powershell
dotnet restore .\src\QuickQR\QuickQR.csproj
dotnet run --project .\src\QuickQR\QuickQR.csproj
```

## Build a small Windows x64 package

```powershell
dotnet publish .\src\QuickQR\QuickQR.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
```

The application extracts its embedded interface files into `%LOCALAPPDATA%\QuickQR\web` on first launch. It stores its WebView2 browser data under `%LOCALAPPDATA%\QuickQR\WebView2`.

## Privacy

Camera access is requested only after you press **Start camera**. Camera frames and QR decoding stay on your PC. QuickQR does not upload QR content or images. A web address contained in a QR code is never opened automatically.

## License

QuickQR application code: MIT. The bundled jsQR decoder is a separate third-party component licensed under Apache-2.0; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and [licenses/jsQR-LICENSE.txt](licenses/jsQR-LICENSE.txt).
