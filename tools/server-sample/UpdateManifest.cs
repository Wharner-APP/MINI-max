// Reference for the C# server ("Upload update" button). NOT compiled by this repository.
// NuGet: NSec.Cryptography  (Ed25519, libsodium-compatible keys: 32-byte raw seed / public key, base64)
using System.Security.Cryptography;
using System.Text.Json;
using NSec.Cryptography;

public static class UpdateManifest
{
    // dir = uploaded, already compiled client folder (e.g. MmServer/minimaxLinuxX64)
    public static (byte[] manifest, byte[] signature) Build(string dir, string platformId, string privateSeedBase64, string[]? removed = null)
    {
        var files = Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories)
            .Select(p => (rel: Path.GetRelativePath(dir, p).Replace('\\', '/'), path: p))
            .Where(x => x.rel != "configs/connect.ip")                       // never overwrite the user's server address
            .OrderBy(x => x.rel, StringComparer.Ordinal)
            .Select(x => new { path = x.rel, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(x.path))).ToLowerInvariant(), size = new FileInfo(x.path).Length })
            .ToArray();
        var revision = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(files)))[..16].ToLowerInvariant();
        var manifest = JsonSerializer.SerializeToUtf8Bytes(new { platform = platformId, revision, created = DateTime.UtcNow.ToString("o"), files, removed = removed ?? Array.Empty<string>() });

        var key = Key.Import(SignatureAlgorithm.Ed25519, Convert.FromBase64String(privateSeedBase64), KeyBlobFormat.RawPrivateKey);
        var sig = SignatureAlgorithm.Ed25519.Sign(key, manifest);
        // Serve:  GET /update/{platform}/manifest.json -> manifest
        //         GET /update/{platform}/manifest.sig  -> Convert.ToBase64String(sig)
        //         GET /update/{platform}/files/{relative path} -> file
        // The replacement is thereby "logged": the new `revision` is what clients compare against.
        return (manifest, sig);
    }
}
