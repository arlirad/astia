using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Builds;

public interface ISourceCheckout
{
    Task CheckoutAsync(
        PackageSource source,
        string workspace,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}

public sealed class GitSourceCheckout : ISourceCheckout
{
    private readonly IBuildProcessRunner _processRunner;
    private readonly string? _egressProxy;

    public GitSourceCheckout(IBuildProcessRunner processRunner, string? egressProxy = null)
    {
        _processRunner = processRunner;

        if (!string.IsNullOrWhiteSpace(egressProxy)
            && !IsValidEgressProxy(egressProxy))
        {
            throw new ArgumentException(
                "Egress proxy must be an absolute HTTP(S) URL without credentials.",
                nameof(egressProxy));
        }

        _egressProxy = string.IsNullOrWhiteSpace(egressProxy)
            ? null
            : egressProxy.Trim();
    }

    public static bool IsValidEgressProxy(string? egressProxy) =>
        !string.IsNullOrWhiteSpace(egressProxy)
        && Uri.TryCreate(egressProxy.Trim(), UriKind.Absolute, out var proxyUri)
        && (proxyUri.Scheme == Uri.UriSchemeHttp
            || proxyUri.Scheme == Uri.UriSchemeHttps)
        && string.IsNullOrEmpty(proxyUri.UserInfo);

    public async Task CheckoutAsync(
        PackageSource source,
        string workspace,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(workspace);

        if (_egressProxy is null)
        {
            throw new InvalidOperationException(
                "Source checkout requires an operator-configured egress proxy that enforces the host/IP allowlist; the source-host allowlist alone does not prevent DNS rebinding.");
        }

        var gitHome = Path.Combine(workspace, ".git-home");
        var gitConfigHome = Path.Combine(workspace, ".git-config");
        var gitTemp = Path.Combine(workspace, ".git-tmp");
        Directory.CreateDirectory(gitHome);
        Directory.CreateDirectory(gitConfigHome);
        Directory.CreateDirectory(gitTemp);

        var environment = CreateGitEnvironment(
            gitHome,
            gitConfigHome,
            gitTemp,
            _egressProxy);
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);

        await RunGitAsync(
            ["init", "--template=/dev/null", "--quiet", workspace],
            Path.GetDirectoryName(workspace) ?? workspace,
            timeout,
            deadline.Token,
            environment).ConfigureAwait(false);

        await RunGitAsync(
            ["-C", workspace, "remote", "add", "origin", source.RepositoryUri.ToString()],
            Path.GetDirectoryName(workspace) ?? workspace,
            timeout,
            deadline.Token,
            environment).ConfigureAwait(false);

        await RunGitAsync(
            ["-C", workspace, "fetch", "--depth", "1", "origin", source.CommitSha],
            Path.GetDirectoryName(workspace) ?? workspace,
            timeout,
            deadline.Token,
            environment).ConfigureAwait(false);

        await RunGitAsync(
            ["-C", workspace, "checkout", "--detach", source.CommitSha],
            Path.GetDirectoryName(workspace) ?? workspace,
            timeout,
            deadline.Token,
            environment).ConfigureAwait(false);
    }

    private async Task RunGitAsync(
        IReadOnlyList<string> arguments,
        string workingDirectory,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        IReadOnlyDictionary<string, string> environment)
    {
        var isolatedArguments = new List<string>
        {
            "-c", "core.hooksPath=/dev/null",
            "-c", "credential.helper=",
            "-c", "http.followRedirects=false"
        };
        isolatedArguments.AddRange(arguments);

        var result = await _processRunner.RunAsync(
            new BuildProcessRequest(
                "git",
                isolatedArguments,
                workingDirectory,
                environment,
                timeout),
            cancellationToken).ConfigureAwait(false);

        if (result.TimedOut)
        {
            throw new TimeoutException("Git source checkout timed out.");
        }

        if (result.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"Git source checkout step failed with exit code {result.ExitCode}.");
        }
    }

    private static IReadOnlyDictionary<string, string> CreateGitEnvironment(
        string home,
        string configHome,
        string tempDirectory,
        string egressProxy) =>
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["PATH"] = "/usr/bin:/bin",
            ["HOME"] = home,
            ["XDG_CONFIG_HOME"] = configHome,
            ["TMPDIR"] = tempDirectory,
            ["LC_ALL"] = "C",
            ["GIT_CONFIG_GLOBAL"] = "/dev/null",
            ["GIT_CONFIG_NOSYSTEM"] = "1",
            ["GIT_TERMINAL_PROMPT"] = "0",
            ["GIT_ASKPASS"] = "/bin/false",
            ["GIT_SSH_COMMAND"] = "/bin/false",
            ["GIT_ALLOW_PROTOCOL"] = "https",
            ["GIT_PROTOCOL_FROM_USER"] = "0",
            ["HTTPS_PROXY"] = egressProxy,
            ["https_proxy"] = egressProxy
        };
}
