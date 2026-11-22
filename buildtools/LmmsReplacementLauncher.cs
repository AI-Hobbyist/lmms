using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Windows.Forms;

internal static class LmmsReplacementLauncher
{
    [STAThread]
    private static int Main()
    {
        string installer = null;
        int exitCode = 1;
        try
        {
#if LMMS_PRODUCTION_REPLACEMENT
            const string target = @"D:\Program Files\LMMS";
#else
            string target;
            using (var dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Select the LMMS folder containing lmms.exe.";
                dialog.ShowNewFolderButton = false;
                dialog.SelectedPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "LMMS");
                if (dialog.ShowDialog() != DialogResult.OK)
                {
                    return 0;
                }
                target = Path.Combine(dialog.SelectedPath, ".");
            }
#endif
            string package = typeof(LmmsReplacementLauncher).Assembly.Location;
            installer = Path.Combine(Path.GetTempPath(), "lmms-replace-" + Guid.NewGuid().ToString("N") + ".ps1");
            using (ZipArchive archive = ZipFile.OpenRead(package))
            {
                ZipArchiveEntry entry = archive.GetEntry("Install-Replace.ps1");
                if (entry == null)
                {
                    throw new InvalidDataException("The replacement installer is missing from this package.");
                }
                entry.ExtractToFile(installer, false);
            }

            string powershell = Path.Combine(
                Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
            var start = new ProcessStartInfo(powershell,
                "-NoProfile -ExecutionPolicy Bypass -File \"" + installer
                + "\" -ArchivePath \"" + package + "\" -TargetDirectory \"" + target + "\"");
            start.UseShellExecute = false;
            using (Process process = Process.Start(start))
            {
                process.WaitForExit();
                exitCode = process.ExitCode;
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
        }
        finally
        {
            if (installer != null)
            {
                try
                {
                    File.Delete(installer);
                }
                catch (IOException error)
                {
                    Console.Error.WriteLine(error.Message);
                }
                catch (UnauthorizedAccessException error)
                {
                    Console.Error.WriteLine(error.Message);
                }
            }
        }
        if (!Console.IsInputRedirected)
        {
            Console.WriteLine("Press Enter to close.");
            Console.ReadLine();
        }
        return exitCode;
    }
}
