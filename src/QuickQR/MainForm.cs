using System.Diagnostics;
using System.Reflection;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace QuickQR;

internal sealed class MainForm : Form
{
    private readonly WebView2 browser = new()
    {
        Dock = DockStyle.Fill,
        AllowExternalDrop = false,
        DefaultBackgroundColor = System.Drawing.Color.FromArgb(12, 15, 22)
    };

    private bool initialized;

    public MainForm()
    {
        Text = "QuickQR";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new System.Drawing.Size(430, 620);
        Size = new System.Drawing.Size(540, 760);
        BackColor = System.Drawing.Color.FromArgb(12, 15, 22);
        Controls.Add(browser);
        Shown += InitializeBrowser;
    }

    private async void InitializeBrowser(object? sender, EventArgs e)
    {
        if (initialized) return;
        initialized = true;

        try
        {
            var appData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "QuickQR");
            var userData = Path.Combine(appData, "WebView2");
            var webDirectory = Path.Combine(appData, "web");

            Directory.CreateDirectory(userData);
            ExtractWebAssets(webDirectory);

            string? runtimeVersion;
            try
            {
                runtimeVersion = CoreWebView2Environment.GetAvailableBrowserVersionString();
            }
            catch
            {
                runtimeVersion = null;
            }

            if (string.IsNullOrWhiteSpace(runtimeVersion))
            {
                OfferWebView2Install();
                Close();
                return;
            }

            var environment = await CoreWebView2Environment.CreateAsync(
                browserExecutableFolder: null,
                userDataFolder: userData);
            await browser.EnsureCoreWebView2Async(environment);

            var core = browser.CoreWebView2;
            core.Settings.AreDefaultContextMenusEnabled = false;
            core.Settings.AreDevToolsEnabled = false;
            core.Settings.IsStatusBarEnabled = false;
            core.Settings.IsZoomControlEnabled = false;

            core.PermissionRequested += (_, args) =>
            {
                if (args.PermissionKind == CoreWebView2PermissionKind.Camera &&
                    Uri.TryCreate(args.Uri, UriKind.Absolute, out var origin) &&
                    origin.Host.Equals("quickqr.local", StringComparison.OrdinalIgnoreCase))
                {
                    args.State = CoreWebView2PermissionState.Allow;
                }
            };

            core.NewWindowRequested += (_, args) =>
            {
                args.Handled = true;
                OpenExternalUrl(args.Uri);
            };

            core.NavigationStarting += (_, args) =>
            {
                if (!Uri.TryCreate(args.Uri, UriKind.Absolute, out var uri) ||
                    !uri.Host.Equals("quickqr.local", StringComparison.OrdinalIgnoreCase))
                {
                    args.Cancel = true;
                    OpenExternalUrl(args.Uri);
                }
            };

            core.SetVirtualHostNameToFolderMapping(
                "quickqr.local",
                webDirectory,
                CoreWebView2HostResourceAccessKind.Allow);

            core.Navigate("https://quickqr.local/index.html");
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                this,
                "QuickQR could not start. Make sure the .NET 10 Desktop Runtime (x64) and Microsoft Edge WebView2 Runtime are installed.\n\nDetails: " + ex.Message,
                "QuickQR startup error",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            Close();
        }
    }

    private static void ExtractWebAssets(string webDirectory)
    {
        Directory.CreateDirectory(webDirectory);

        var assets = new (string ResourceName, string FileName)[]
        {
            ("QuickQR.Web.index.html", "index.html"),
            ("QuickQR.Web.style.css", "style.css"),
            ("QuickQR.Web.app.js", "app.js"),
            ("QuickQR.Web.jsQR.js", "jsQR.js")
        };

        var assembly = Assembly.GetExecutingAssembly();
        foreach (var (resourceName, fileName) in assets)
        {
            using var resource = assembly.GetManifestResourceStream(resourceName)
                ?? throw new InvalidOperationException(
                    $"The embedded application file '{fileName}' is missing.");
            using var destination = File.Create(Path.Combine(webDirectory, fileName));
            resource.CopyTo(destination);
        }
    }

    private void OfferWebView2Install()
    {
        var setupPath = Path.Combine(AppContext.BaseDirectory, "MicrosoftEdgeWebView2Setup.exe");
        var hasBundledSetup = File.Exists(setupPath);
        var message = hasBundledSetup
            ? "QuickQR needs the Microsoft Edge WebView2 Runtime. Install it now using the setup file included beside QuickQR.exe? After installation, open QuickQR again."
            : "QuickQR needs the Microsoft Edge WebView2 Runtime. Open Microsoft's official download page now? Install the Evergreen Bootstrapper, then open QuickQR again.";

        var answer = MessageBox.Show(
            this,
            message,
            "WebView2 Runtime required",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Information);

        if (answer != DialogResult.Yes) return;

        try
        {
            var target = hasBundledSetup
                ? setupPath
                : "https://developer.microsoft.com/en-us/microsoft-edge/webview2/";
            Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Could not open installer", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static void OpenExternalUrl(string value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri)) return;
        if (uri.Scheme is not ("http" or "https")) return;

        try
        {
            Process.Start(new ProcessStartInfo(uri.AbsoluteUri) { UseShellExecute = true });
        }
        catch
        {
            // The scanner remains usable even when the default browser cannot be started.
        }
    }
}
