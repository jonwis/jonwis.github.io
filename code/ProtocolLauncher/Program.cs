// See https://aka.ms/new-console-template for more information
using Windows.System;
using Windows.Win32;
using Windows.Win32.UI.Shell;
using WinRT;

Windows.ApplicationModel.Package? GetCurrentPackage()
{
    try
    {
        return Windows.ApplicationModel.Package.Current;
    }
    catch (System.Exception)
    {
        return null;
    }
}

Console.WriteLine("Hello, World!");

var currentPackage = GetCurrentPackage();
if (currentPackage == null)
{
    var pathToSelf = System.Reflection.Assembly.GetExecutingAssembly().Location;
    Console.WriteLine("Not in a package...");
    Console.WriteLine($"Run from PowerShell like Invoke-CommandInDesktopPackage -PackageFamilyName MSTeams_8wekyb3d8bbwe -Command \"{pathToSelf}\" -Args \"custom-uri-launch-sample:foo\"");
}
else
{
    Console.WriteLine($"Package name {currentPackage.Id.FamilyName}");
    var options = new LauncherOptions();
    options.As<IInitializeWithWindow>().Initialize(PInvoke.GetDesktopWindow());
    var result = await Launcher.LaunchUriAsync(new Uri(args[0]), options);
    if (result)
    {
        Console.WriteLine("Launched successfully!");
    }
    else
    {
        Console.WriteLine("Failed to launch the URI.");
    }
}

