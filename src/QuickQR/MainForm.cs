using System.Diagnostics;
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
            var userData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "QuickQR", "WebView2");
            Directory.CreateDirectory(userData);

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

            core.SetVirtualHostNameToFolderMapping(
                "quickqr.local",
                Path.Combine(AppContext.BaseDirectory, "web"),
                CoreWebView2HostResourceAccessKind.Allow);

            core.Navigate("https://quickqr.local/index.html");
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                this,
                "QuickQR could not start its embedded browser. Install the .NET 10 Desktop Runtime and Microsoft Edge WebView2 Runtime, then try again.\n\n" + ex.Message,
                "QuickQR startup error",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            Close();
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
