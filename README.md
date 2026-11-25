# QuickQR

A tiny Windows QR-code scanner. No Electron, no Python, no ads, no telemetry.

## Features

- Scan QR codes using a webcam.
- Scan QR codes from PNG, JPG, GIF, BMP and WebP images.
- Drag and drop an image into the window.
- Copy decoded text with one click.
- Open decoded web links in your default browser (only when you click Open).
- Compact dark interface and low idle activity.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- .NET 10 Desktop Runtime.
- Microsoft Edge WebView2 Runtime.
- Internet access on the first run to load the small `jsQR` decoder from jsDelivr. WebView2 caches it for later use when possible.

The application does not upload camera frames or scanned content. QR decoding is performed locally in the WebView. The decoder script is served from jsDelivr (jsQR 1.4.0, Apache-2.0).

## Run from source

```powershell
dotnet restore .\src\QuickQR\QuickQR.csproj
dotnet run --project .\src\QuickQR\QuickQR.csproj
```

## Build a small Windows x64 package

```powershell
dotnet publish .\src\QuickQR\QuickQR.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
```

The result is written to:

`src/QuickQR/bin/Release/net10.0-windows/win-x64/publish/`

Keep the `web` folder next to `QuickQR.exe`.

## Privacy

Camera access is requested only after you press **Start camera**. Camera frames are processed on your PC. QuickQR does not transmit QR content, images, or camera frames to a server. A web address contained in a QR code is never opened automatically.

## License

QuickQR application code: MIT. The jsQR decoder is a separate third-party component licensed under Apache-2.0; see the decoder's license at <https://www.apache.org/licenses/LICENSE-2.0>.
