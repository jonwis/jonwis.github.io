// See https://aka.ms/new-console-template for more information
using Windows.Foundation.Metadata;

Console.WriteLine("Hello, World!");

void SetInterval(Windows.Graphics.Capture.GraphicsCaptureSession session, int FrameRate)
{
    if (ApiInformation.IsPropertyPresent("Windows.Media.Capture.Frames.MediaFrameSource", "MinUpdateInterval"))
    {
        session.MinUpdateInterval = TimeSpan.FromMicroseconds(10000000 / FrameRate);
    }
}

