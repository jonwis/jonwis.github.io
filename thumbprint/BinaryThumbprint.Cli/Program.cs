using System.Reflection.PortableExecutable;
using BinaryThumbprint;

if (args.Length == 0)
{
    PrintUsage();
    return 1;
}

return args[0].ToLowerInvariant() switch
{
    "compute" => Compute(args.AsSpan(1)),
    "compare" => Compare(args.AsSpan(1)),
    "scan" => Scan(args.AsSpan(1)),
    _ => PrintUsage()
};

static int PrintUsage()
{
    Console.Error.WriteLine("""
        Usage:
          thumbprint compute <file> [file2 ...]
            Prints base64-encoded thumbprint for each file.

          thumbprint compare <file1> <file2>
            Prints similarity score between two PE files.

          thumbprint scan <thumbprint-base64> <directory> [--threshold 0.85]
            Recursively scans directory for PE files with similar thumbprints.
        """);
    return 1;
}

static int Compute(ReadOnlySpan<string> files)
{
    if (files.Length == 0)
    {
        Console.Error.WriteLine("Error: specify at least one file.");
        return 1;
    }

    foreach (var file in files)
    {
        try
        {
            var tp = PeThumbprinter.ComputeThumbprint(file);
            Console.WriteLine($"{PeThumbprinter.ToBase64(tp)}  {file}");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error [{file}]: {ex.Message}");
        }
    }
    return 0;
}

static int Compare(ReadOnlySpan<string> files)
{
    if (files.Length != 2)
    {
        Console.Error.WriteLine("Error: specify exactly two files.");
        return 1;
    }

    try
    {
        var tp1 = PeThumbprinter.ComputeThumbprint(files[0]);
        var tp2 = PeThumbprinter.ComputeThumbprint(files[1]);
        int hamming = PeThumbprinter.HammingDistance(tp1, tp2);
        double similarity = PeThumbprinter.Similarity(tp1, tp2);
        double nines = PeThumbprinter.SimilarityNines(tp1, tp2);

        Console.WriteLine($"Thumbprint A: {PeThumbprinter.ToBase64(tp1)}");
        Console.WriteLine($"Thumbprint B: {PeThumbprinter.ToBase64(tp2)}");
        Console.WriteLine($"Hamming dist: {hamming}/{PeThumbprinter.ThumbprintSize}");
        Console.WriteLine($"Similarity:   {similarity:P1} ({nines:F2} nines)");

        string verdict = nines switch
        {
            >= 2.0 => "Nearly identical (two+ nines)",
            >= 1.3 => "Very likely same binary (minor patch)",
            >= 0.85 => "Likely same binary (moderate changes)",
            >= 0.5 => "Possibly related (same codebase)",
            _ => "Probably different binaries"
        };
        Console.WriteLine($"Verdict:      {verdict}");
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"Error: {ex.Message}");
        return 1;
    }
    return 0;
}

static int Scan(ReadOnlySpan<string> args)
{
    if (args.Length < 2)
    {
        Console.Error.WriteLine("Error: specify a thumbprint and a directory.");
        return 1;
    }

    byte[] target;
    try
    {
        target = PeThumbprinter.FromBase64(args[0]);
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"Error parsing thumbprint: {ex.Message}");
        return 1;
    }

    string directory = args[1];
    double threshold = 0.85;

    // Parse optional --threshold
    for (int i = 2; i < args.Length - 1; i++)
    {
        if (args[i] == "--threshold" && double.TryParse(args[i + 1], out var t))
        {
            threshold = t;
            break;
        }
    }

    if (!Directory.Exists(directory))
    {
        Console.Error.WriteLine($"Error: directory not found: {directory}");
        return 1;
    }

    var peExtensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        ".exe", ".dll", ".sys", ".ocx", ".drv", ".efi", ".scr", ".cpl"
    };

    int scanned = 0, matched = 0;
    foreach (var file in Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories))
    {
        string ext = Path.GetExtension(file);
        if (!peExtensions.Contains(ext)) continue;

        try
        {
            var tp = PeThumbprinter.ComputeThumbprint(file);
            double sim = PeThumbprinter.Similarity(target, tp);
            scanned++;

            if (sim >= threshold)
            {
                matched++;
                Console.WriteLine($"{sim:P1}  {PeThumbprinter.ToBase64(tp)}  {file}");
            }
        }
        catch
        {
            // Skip files that aren't valid PE or can't be read.
        }
    }

    Console.Error.WriteLine($"Scanned {scanned} PE files, {matched} matched (threshold {threshold:P0}).");
    return 0;
}
