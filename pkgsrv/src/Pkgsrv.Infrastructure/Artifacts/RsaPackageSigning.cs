using System.Security.Cryptography;
using System.Text;
using Pkgsrv.Application.Interfaces.Artifacts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class RsaPackageSigner : IPackageSigner
{
    private readonly string _privateKeyPem;

    public RsaPackageSigner(string privateKeyPath)
    {
        if (string.IsNullOrWhiteSpace(privateKeyPath))
        {
            throw new ArgumentException("RSA private key path is required.", nameof(privateKeyPath));
        }

        _privateKeyPem = RsaKeyFiles.ReadPrivateKey(privateKeyPath);

        using var rsa = RSA.Create();
        rsa.ImportFromPem(_privateKeyPem);

        if (rsa.ExportParameters(includePrivateParameters: true).D is null)
        {
            throw new InvalidOperationException("Configured RSA key does not contain a private key.");
        }

        if (rsa.KeySize < 2048)
        {
            throw new InvalidOperationException(
                "Configured RSA private key must be at least 2048 bits.");
        }
    }

    public Task<SigningResult> SignAsync(ContentHash contentHash, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(contentHash);

        using var rsa = RSA.Create();
        rsa.ImportFromPem(_privateKeyPem);
        var signature = rsa.SignHash(
            Convert.FromHexString(contentHash.Value),
            HashAlgorithmName.SHA256,
            RSASignaturePadding.Pkcs1);

        return Task.FromResult(new SigningResult(
            PackageSignature.Signed(
                PackageSignatureAlgorithms.RsaSha256,
                Convert.ToBase64String(signature)),
            "Artifact content hash signed with RSA-SHA256."));
    }
}

public sealed class RsaPackageSignatureVerifier : IPackageSignatureVerifier
{
    private readonly string _publicKeyPem;

    public RsaPackageSignatureVerifier(string publicKeyPath, string? privateKeyPath = null)
    {
        if (!string.IsNullOrWhiteSpace(publicKeyPath))
        {
            _publicKeyPem = RsaKeyFiles.ReadPublicKey(publicKeyPath);
            return;
        }

        if (string.IsNullOrWhiteSpace(privateKeyPath))
        {
            throw new ArgumentException("An RSA public or private key path is required.");
        }

        using var rsa = RSA.Create();
        rsa.ImportFromPem(RsaKeyFiles.ReadPrivateKey(privateKeyPath));
        _publicKeyPem = rsa.ExportSubjectPublicKeyInfoPem();
    }

    public bool Verify(ContentHash contentHash, PackageSignature signature)
    {
        ArgumentNullException.ThrowIfNull(contentHash);
        ArgumentNullException.ThrowIfNull(signature);

        if (!signature.IsPublishable
            || !string.Equals(
                signature.Algorithm,
                PackageSignatureAlgorithms.RsaSha256,
                StringComparison.Ordinal)
            || string.IsNullOrWhiteSpace(signature.Value))
        {
            return false;
        }

        try
        {
            using var rsa = RSA.Create();
            rsa.ImportFromPem(_publicKeyPem);

            return rsa.VerifyHash(
                Convert.FromHexString(contentHash.Value),
                Convert.FromBase64String(signature.Value),
                HashAlgorithmName.SHA256,
                RSASignaturePadding.Pkcs1);
        }
        catch (FormatException)
        {
            return false;
        }
        catch (CryptographicException)
        {
            return false;
        }
    }
}
