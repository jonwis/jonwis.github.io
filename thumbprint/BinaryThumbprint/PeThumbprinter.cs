using System.Collections.Immutable;
using System.Reflection.PortableExecutable;
using System.Text;

namespace BinaryThumbprint;

/// <summary>
/// Generates a 64-byte "thumbprint" of a PE binary that is resilient to
/// small in-place changes and minor insertions/deletions. Two binaries
/// built from nearly identical source will produce nearly identical thumbprints.
/// </summary>
public static class PeThumbprinter
{
    public const int ThumbprintSize = 64;
    private const int MaxSections = 6;
    private const int SectionFingerprintSize = 8;

    // Layout offsets
    private const int OffsetMachine = 0;        // 2 bytes
    private const int OffsetSubsystem = 2;      // 2 bytes
    private const int OffsetFileSize = 4;       // 4 bytes
    private const int OffsetSectionSig = 8;     // 4 bytes
    private const int OffsetSections = 12;      // 48 bytes (6 × 8)
    private const int OffsetReserved = 60;      // 4 bytes

    // Sections to skip entirely — their content is too volatile.
    private static readonly HashSet<string> VolatileSections = new(StringComparer.OrdinalIgnoreCase)
    {
        ".reloc",
    };

    /// <summary>
    /// Compute a 64-byte thumbprint for the PE file at <paramref name="filePath"/>.
    /// </summary>
    public static byte[] ComputeThumbprint(string filePath)
    {
        using var stream = File.OpenRead(filePath);
        return ComputeThumbprint(stream, new FileInfo(filePath).Length);
    }

    /// <summary>
    /// Compute a 64-byte thumbprint from a stream containing a PE image.
    /// </summary>
    public static byte[] ComputeThumbprint(Stream stream, long fileSize)
    {
        var thumbprint = new byte[ThumbprintSize];

        using var peReader = new PEReader(stream, PEStreamOptions.LeaveOpen);
        var headers = peReader.PEHeaders;

        // --- Bytes 0–1: Machine type ---
        WriteUInt16(thumbprint, OffsetMachine, (ushort)headers.CoffHeader.Machine);

        // --- Bytes 2–3: Subsystem ---
        WriteUInt16(thumbprint, OffsetSubsystem, (ushort)headers.PEHeader!.Subsystem);

        // --- Bytes 4–7: Quantized file size ---
        // log2(size) * 256 — files within ~0.4% of each other produce the same value.
        uint quantizedSize = fileSize > 0
            ? (uint)(Math.Log2(fileSize) * 256.0)
            : 0;
        WriteUInt32(thumbprint, OffsetFileSize, quantizedSize);

        // --- Bytes 8–11: Section table signature ---
        var eligibleSections = headers.SectionHeaders
            .Where(s => !VolatileSections.Contains(s.Name))
            .OrderBy(s => s.Name, StringComparer.Ordinal)
            .ToList();

        uint sectionSig = ComputeSectionTableSignature(eligibleSections);
        WriteUInt32(thumbprint, OffsetSectionSig, sectionSig);

        // --- Bytes 12–59: Per-section fingerprints ---
        int slotIndex = 0;
        foreach (var section in eligibleSections.Take(MaxSections))
        {
            var fingerprint = ComputeSectionFingerprint(peReader, section, headers);
            Buffer.BlockCopy(fingerprint, 0, thumbprint,
                OffsetSections + slotIndex * SectionFingerprintSize,
                SectionFingerprintSize);
            slotIndex++;
        }

        return thumbprint;
    }

    /// <summary>
    /// Compute the byte-level Hamming distance between two thumbprints.
    /// Returns the number of bytes that differ (0–64).
    /// </summary>
    public static int HammingDistance(ReadOnlySpan<byte> a, ReadOnlySpan<byte> b)
    {
        if (a.Length != ThumbprintSize || b.Length != ThumbprintSize)
            throw new ArgumentException($"Both thumbprints must be {ThumbprintSize} bytes.");

        int distance = 0;
        for (int i = 0; i < ThumbprintSize; i++)
        {
            if (a[i] != b[i]) distance++;
        }
        return distance;
    }

    /// <summary>
    /// Compute a linear similarity score between two thumbprints (0.0 = completely different, 1.0 = identical).
    /// </summary>
    public static double Similarity(ReadOnlySpan<byte> a, ReadOnlySpan<byte> b) =>
        (ThumbprintSize - HammingDistance(a, b)) / (double)ThumbprintSize;

    /// <summary>
    /// Compute a "nines" similarity score: -log10(1 - linearSimilarity).
    /// Each whole number represents a 10× reduction in differences:
    /// 1.0 = 90% similar, 2.0 = 99%, 3.0 = 99.9%, etc.
    /// Identical thumbprints are clamped to <paramref name="maxNines"/> (default 6.0).
    /// Returns 0.0 when similarity is ≤ 0.
    /// </summary>
    public static double SimilarityNines(ReadOnlySpan<byte> a, ReadOnlySpan<byte> b, double maxNines = 6.0)
    {
        double linear = Similarity(a, b);
        if (linear >= 1.0) return maxNines;
        if (linear <= 0.0) return 0.0;
        return Math.Min(-Math.Log10(1.0 - linear), maxNines);
    }

    /// <summary>
    /// Format a thumbprint as a URL-safe Base64 string (no padding).
    /// </summary>
    public static string ToBase64(ReadOnlySpan<byte> thumbprint) =>
        Convert.ToBase64String(thumbprint).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    /// <summary>
    /// Parse a thumbprint from a URL-safe Base64 string.
    /// </summary>
    public static byte[] FromBase64(string encoded)
    {
        string padded = encoded.Replace('-', '+').Replace('_', '/');
        switch (padded.Length % 4)
        {
            case 2: padded += "=="; break;
            case 3: padded += "="; break;
        }
        var bytes = Convert.FromBase64String(padded);
        if (bytes.Length != ThumbprintSize)
            throw new FormatException($"Decoded thumbprint must be {ThumbprintSize} bytes, got {bytes.Length}.");
        return bytes;
    }

    // ─── Private helpers ─────────────────────────────────────────────

    private static uint ComputeSectionTableSignature(List<SectionHeader> sections)
    {
        // Combine section count with a hash of sorted section names.
        uint hash = (uint)sections.Count;
        foreach (var s in sections)
        {
            foreach (char c in s.Name)
            {
                hash = hash * 31 + c;
            }
        }
        return hash;
    }

    private static byte[] ComputeSectionFingerprint(
        PEReader peReader,
        SectionHeader section,
        PEHeaders headers)
    {
        var fp = new byte[SectionFingerprintSize];

        // Byte 0: section name hash
        fp[0] = HashSectionName(section.Name);

        // Read section data
        var sectionData = peReader.GetSectionData(section.VirtualAddress);
        if (sectionData.Length == 0)
            return fp;

        var content = sectionData.GetContent();
        if (content.Length == 0)
            return fp;

        // Build byte-frequency histogram, skipping known volatile regions
        var histogram = BuildHistogram(content, section, headers);

        // Bytes 1–2: Shannon entropy (quantized, × 100, as uint16)
        double entropy = ComputeEntropy(histogram, content.Length);
        WriteUInt16(fp, 1, (ushort)(entropy * 100.0));

        // Find top-2 most frequent byte values
        var (top1Val, top1Count, top2Val, top2Count) = FindTop2(histogram);

        // Bytes 3–4: top-2 byte values
        fp[3] = top1Val;
        fp[4] = top2Val;

        // Bytes 5–6: quantized frequencies (proportion × 255)
        double total = content.Length;
        fp[5] = (byte)Math.Clamp(top1Count / total * 255.0, 0, 255);
        fp[6] = (byte)Math.Clamp(top2Count / total * 255.0, 0, 255);

        // Byte 7: uniformity score
        fp[7] = ComputeUniformity(histogram, content.Length);

        return fp;
    }

    private static long[] BuildHistogram(
        ImmutableArray<byte> content,
        SectionHeader section,
        PEHeaders headers)
    {
        var histogram = new long[256];

        // Determine volatile byte ranges within this section to skip.
        var volatileRanges = GetVolatileRanges(section, headers);

        int sectionStart = section.VirtualAddress;
        for (int i = 0; i < content.Length; i++)
        {
            int rva = sectionStart + i;
            if (IsInVolatileRange(rva, volatileRanges))
                continue;

            histogram[content[i]]++;
        }

        return histogram;
    }

    private static List<(int Start, int End)> GetVolatileRanges(
        SectionHeader section,
        PEHeaders headers)
    {
        var ranges = new List<(int Start, int End)>();
        var pe = headers.PEHeader!;

        // PE checksum (offset into optional header is at a fixed location)
        // We skip by RVA, so we need to find the header section — but these
        // fields are in the PE header, not in a section. We skip them at the
        // section level only if the section overlaps those addresses.

        // Debug directory data
        var debugDir = pe.DebugTableDirectory;
        if (debugDir.Size > 0)
        {
            ranges.Add((debugDir.RelativeVirtualAddress,
                        debugDir.RelativeVirtualAddress + debugDir.Size));
        }

        // Certificate table (security directory) — uses file offset, not RVA,
        // but PEReader won't map it into a section anyway, so nothing to skip.

        // Resource section: skip the VS_VERSION_INFO block if present.
        // This is complex to parse precisely, so we skip the entire .rsrc
        // section's first 4KB as a heuristic (version info is typically early).
        if (section.Name.Equals(".rsrc", StringComparison.OrdinalIgnoreCase))
        {
            int rsrcStart = section.VirtualAddress;
            ranges.Add((rsrcStart, rsrcStart + Math.Min(4096, section.VirtualSize)));
        }

        return ranges;
    }

    private static bool IsInVolatileRange(int rva, List<(int Start, int End)> ranges)
    {
        foreach (var (start, end) in ranges)
        {
            if (rva >= start && rva < end) return true;
        }
        return false;
    }

    private static double ComputeEntropy(long[] histogram, int totalBytes)
    {
        if (totalBytes == 0) return 0;

        double entropy = 0;
        double total = totalBytes;
        for (int i = 0; i < 256; i++)
        {
            if (histogram[i] == 0) continue;
            double p = histogram[i] / total;
            entropy -= p * Math.Log2(p);
        }
        return entropy; // 0..8
    }

    private static (byte top1Val, long top1Count, byte top2Val, long top2Count) FindTop2(long[] histogram)
    {
        byte top1 = 0, top2 = 0;
        long count1 = -1, count2 = -1;

        for (int i = 0; i < 256; i++)
        {
            if (histogram[i] > count1)
            {
                top2 = top1; count2 = count1;
                top1 = (byte)i; count1 = histogram[i];
            }
            else if (histogram[i] > count2)
            {
                top2 = (byte)i; count2 = histogram[i];
            }
        }

        return (top1, Math.Max(count1, 0), top2, Math.Max(count2, 0));
    }

    private static byte ComputeUniformity(long[] histogram, int totalBytes)
    {
        if (totalBytes == 0) return 0;

        // Uniformity: how close the distribution is to perfectly uniform.
        // Measured as 1 - (max deviation from uniform / expected).
        // 0 = all bytes the same value, 255 = perfectly uniform.
        double expected = totalBytes / 256.0;
        double maxDeviation = 0;
        for (int i = 0; i < 256; i++)
        {
            double dev = Math.Abs(histogram[i] - expected);
            if (dev > maxDeviation) maxDeviation = dev;
        }

        double ratio = maxDeviation / expected;
        // ratio = 0 means perfect uniformity, ratio ≥ 255 means all one byte.
        double uniformity = Math.Clamp(1.0 - ratio / 255.0, 0, 1);
        return (byte)(uniformity * 255);
    }

    private static byte HashSectionName(string name)
    {
        // Simple hash to fit a section name into a single byte.
        uint h = 0;
        foreach (char c in name)
        {
            h = h * 17 + c;
        }
        return (byte)(h & 0xFF);
    }

    private static void WriteUInt16(byte[] buffer, int offset, ushort value)
    {
        buffer[offset] = (byte)(value & 0xFF);
        buffer[offset + 1] = (byte)(value >> 8);
    }

    private static void WriteUInt32(byte[] buffer, int offset, uint value)
    {
        buffer[offset] = (byte)(value & 0xFF);
        buffer[offset + 1] = (byte)((value >> 8) & 0xFF);
        buffer[offset + 2] = (byte)((value >> 16) & 0xFF);
        buffer[offset + 3] = (byte)((value >> 24) & 0xFF);
    }
}
