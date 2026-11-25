namespace Pkgsrv.Domain.Packages;

public sealed record PackageManifest
{
    private PackageManifest(
        PackageName name,
        PackageVersion version,
        PackageSource source,
        Toolchain toolchain,
        ContentHash contentHash,
        PackageProvenance provenance,
        PackageSignature signature)
    {
        Name = name;
        Version = version;
        Source = source;
        Toolchain = toolchain;
        ContentHash = contentHash;
        Provenance = provenance;
        Signature = signature;
    }

    public PackageName Name { get; }

    public PackageVersion Version { get; }

    public PackageSource Source { get; }

    public Toolchain Toolchain { get; }

    public ContentHash ContentHash { get; }

    public PackageProvenance Provenance { get; }

    public PackageSignature Signature { get; }

    public bool IsPublishable => Signature.IsPublishable;

    public static PackageManifest Create(
        PackageName name,
        PackageVersion version,
        PackageSource source,
        Toolchain toolchain,
        ContentHash contentHash,
        PackageProvenance provenance,
        PackageSignature signature)
    {
        ArgumentNullException.ThrowIfNull(name);
        ArgumentNullException.ThrowIfNull(version);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(toolchain);
        ArgumentNullException.ThrowIfNull(contentHash);
        ArgumentNullException.ThrowIfNull(provenance);
        ArgumentNullException.ThrowIfNull(signature);

        return new PackageManifest(name, version, source, toolchain, contentHash, provenance, signature);
    }

    public void EnsurePublishable() => Signature.EnsurePublishable();
}
