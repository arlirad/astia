using Pkgsrv.Infrastructure.Builds;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class GitSourceCheckoutTests
{
    [Fact]
    public async Task Checkout_UsesFixedGitArgvPinnedToSourceCommitAndIsolatedEnvironment()
    {
        var runner = new RecordingProcessRunner();
        var checkout = new GitSourceCheckout(runner, "http://127.0.0.1:3128");
        var source = TestPackages.CreateManifest().Source;
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-checkout-tests-{Guid.NewGuid():N}");
        var workspace = Path.Combine(root, "workspace");
        Directory.CreateDirectory(root);

        try
        {
            await checkout.CheckoutAsync(
                source,
                workspace,
                TimeSpan.FromSeconds(10),
                CancellationToken.None);

            Assert.Equal(4, runner.Requests.Count);
            Assert.All(runner.Requests, request =>
            {
                Assert.Equal("git", request.FileName);
                Assert.DoesNotContain("sh", request.FileName, StringComparison.OrdinalIgnoreCase);
                Assert.DoesNotContain("bash", request.FileName, StringComparison.OrdinalIgnoreCase);

                var environment = request.Environment;
                Assert.Equal("/dev/null", environment["GIT_CONFIG_GLOBAL"]);
                Assert.Equal("1", environment["GIT_CONFIG_NOSYSTEM"]);
                Assert.Equal("0", environment["GIT_TERMINAL_PROMPT"]);
                Assert.Equal("/bin/false", environment["GIT_ASKPASS"]);
                Assert.Equal("/bin/false", environment["GIT_SSH_COMMAND"]);
                Assert.Equal("https", environment["GIT_ALLOW_PROTOCOL"]);
                Assert.Equal("0", environment["GIT_PROTOCOL_FROM_USER"]);
                Assert.Equal("http://127.0.0.1:3128", environment["HTTPS_PROXY"]);
                Assert.Equal("http://127.0.0.1:3128", environment["https_proxy"]);
                Assert.StartsWith(workspace, environment["HOME"]);
                Assert.StartsWith(workspace, environment["XDG_CONFIG_HOME"]);
                Assert.StartsWith(workspace, environment["TMPDIR"]);
                Assert.Contains(
                    request.Arguments,
                    argument => argument == "core.hooksPath=/dev/null");
                Assert.Contains(
                    request.Arguments,
                    argument => argument == "credential.helper=");
                Assert.Contains(
                    request.Arguments,
                    argument => argument == "http.followRedirects=false");
            });

            var arguments = runner.Requests.SelectMany(request => request.Arguments).ToArray();

            Assert.Contains("--template=/dev/null", arguments);
            Assert.Contains(source.RepositoryUri.ToString(), arguments);
            Assert.Contains(source.CommitSha, arguments);
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [Fact]
    public async Task Checkout_FailsClosedWithoutEgressProxy()
    {
        var runner = new RecordingProcessRunner();
        var checkout = new GitSourceCheckout(runner);
        var source = TestPackages.CreateManifest().Source;
        var root = Path.Combine(Path.GetTempPath(), $"pkgsrv-checkout-tests-{Guid.NewGuid():N}");
        var workspace = Path.Combine(root, "workspace");
        Directory.CreateDirectory(root);

        try
        {
            await Assert.ThrowsAsync<InvalidOperationException>(() =>
                checkout.CheckoutAsync(
                    source,
                    workspace,
                    TimeSpan.FromSeconds(10),
                    CancellationToken.None));

            Assert.Empty(runner.Requests);
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    private sealed class RecordingProcessRunner : IBuildProcessRunner
    {
        public List<BuildProcessRequest> Requests { get; } = new();

        public Task<BuildProcessResult> RunAsync(
            BuildProcessRequest request,
            CancellationToken cancellationToken)
        {
            Requests.Add(request);
            return Task.FromResult(new BuildProcessResult(0, string.Empty, string.Empty, false));
        }
    }
}
