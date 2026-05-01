using System.Reflection.PortableExecutable;

namespace BinaryThumbprint.Tests;

public class PeThumbprinterTests
{
    /// <summary>
    /// Same file computed twice must produce an identical thumbprint.
    /// </summary>
    [Fact]
    public void SameFile_ProducesIdenticalThumbprint()
    {
        string target = FindSystemDll("kernel32.dll");

        var tp1 = PeThumbprinter.ComputeThumbprint(target);
        var tp2 = PeThumbprinter.ComputeThumbprint(target);

        Assert.Equal(PeThumbprinter.ThumbprintSize, tp1.Length);
        Assert.Equal(tp1, tp2);
    }

    /// <summary>
    /// Thumbprint is exactly 64 bytes.
    /// </summary>
    [Fact]
    public void Thumbprint_IsCorrectSize()
    {
        string target = FindSystemDll("ntdll.dll");
        var tp = PeThumbprinter.ComputeThumbprint(target);
        Assert.Equal(64, tp.Length);
    }

    /// <summary>
    /// Two different system DLLs should have low similarity.
    /// </summary>
    [Fact]
    public void DifferentBinaries_HaveLowSimilarity()
    {
        var tpA = PeThumbprinter.ComputeThumbprint(FindSystemDll("kernel32.dll"));
        var tpB = PeThumbprinter.ComputeThumbprint(FindSystemDll("ntdll.dll"));

        double sim = PeThumbprinter.Similarity(tpA, tpB);
        // Two fundamentally different DLLs should be < 85% similar
        Assert.True(sim < 0.85, $"kernel32 vs ntdll similarity was {sim:P1}, expected < 85%");
    }

    /// <summary>
    /// A file with a small in-place patch (4KB overwritten) should still
    /// produce a very similar thumbprint.
    /// </summary>
    [Fact]
    public void SmallPatch_ThumbprintResilient()
    {
        string source = FindSystemDll("kernel32.dll");
        byte[] original = File.ReadAllBytes(source);

        // Make a patched copy: overwrite 4KB in the middle with 0xCC
        byte[] patched = (byte[])original.Clone();
        int patchOffset = patched.Length / 2;
        int patchSize = Math.Min(4096, patched.Length - patchOffset);
        Array.Fill(patched, (byte)0xCC, patchOffset, patchSize);

        var tpOriginal = PeThumbprinter.ComputeThumbprint(
            new MemoryStream(original), original.Length);
        var tpPatched = PeThumbprinter.ComputeThumbprint(
            new MemoryStream(patched), patched.Length);

        int hamming = PeThumbprinter.HammingDistance(tpOriginal, tpPatched);
        // 4KB patch in a ~1MB file should affect at most a few bytes of the thumbprint
        Assert.True(hamming <= 6,
            $"4KB patch caused {hamming} byte differences, expected ≤ 6");
    }

    /// <summary>
    /// Scattered small changes (32KB total) should not significantly affect the thumbprint.
    /// </summary>
    [Fact]
    public void ScatteredChanges_ThumbprintResilient()
    {
        string source = FindSystemDll("kernel32.dll");
        byte[] original = File.ReadAllBytes(source);
        byte[] patched = (byte[])original.Clone();

        // Scatter 32KB of changes in the back half of the file (avoids PE headers).
        int totalChanges = 32 * 1024;
        int regionStart = patched.Length / 2;
        int regionLength = patched.Length - regionStart;
        int stride = Math.Max(1, regionLength / totalChanges);
        int changed = 0;
        for (int i = regionStart; i < patched.Length && changed < totalChanges; i += stride)
        {
            patched[i] ^= 0xFF;
            changed++;
        }

        var tpOriginal = PeThumbprinter.ComputeThumbprint(
            new MemoryStream(original), original.Length);
        var tpPatched = PeThumbprinter.ComputeThumbprint(
            new MemoryStream(patched), patched.Length);

        int hamming = PeThumbprinter.HammingDistance(tpOriginal, tpPatched);
        // 32KB scattered in a ~1MB file is ~3% of content — allow proportionally
        // more drift than the 100MB target scenario. In production (100MB files),
        // 32KB is 0.03% and would cause ≤2 byte differences.
        Assert.True(hamming <= 14,
            $"32KB scattered changes caused {hamming} byte differences, expected ≤ 14");
    }

    /// <summary>
    /// Base64 round-trip should preserve the thumbprint exactly.
    /// </summary>
    [Fact]
    public void Base64_RoundTrips()
    {
        var tp = PeThumbprinter.ComputeThumbprint(FindSystemDll("kernel32.dll"));
        string encoded = PeThumbprinter.ToBase64(tp);
        var decoded = PeThumbprinter.FromBase64(encoded);
        Assert.Equal(tp, decoded);
    }

    /// <summary>
    /// Hamming distance of identical thumbprints is zero.
    /// </summary>
    [Fact]
    public void HammingDistance_Identical_IsZero()
    {
        var tp = PeThumbprinter.ComputeThumbprint(FindSystemDll("ntdll.dll"));
        Assert.Equal(0, PeThumbprinter.HammingDistance(tp, tp));
        Assert.Equal(1.0, PeThumbprinter.Similarity(tp, tp));
    }

    /// <summary>
    /// Non-PE file throws a meaningful exception.
    /// </summary>
    [Fact]
    public void NonPeFile_Throws()
    {
        string tempFile = Path.GetTempFileName();
        try
        {
            File.WriteAllText(tempFile, "This is not a PE file.");
            Assert.ThrowsAny<Exception>(() => PeThumbprinter.ComputeThumbprint(tempFile));
        }
        finally
        {
            File.Delete(tempFile);
        }
    }

    private static string FindSystemDll(string name)
    {
        string path = Path.Combine(Environment.SystemDirectory, name);
        if (!File.Exists(path))
            throw new FileNotFoundException($"System DLL not found: {path}");
        return path;
    }
}
