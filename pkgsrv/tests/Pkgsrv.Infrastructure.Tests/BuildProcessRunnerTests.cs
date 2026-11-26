using System.Text;
using Pkgsrv.Infrastructure.Builds;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class BuildProcessRunnerTests
{
    [Fact]
    public async Task CapturesBoundedOutputAndDrainsSuccessfulProcess()
    {
        if (!File.Exists("/bin/cat"))
        {
            return;
        }

        var directory = Path.Combine(Path.GetTempPath(), $"pkgsrv-output-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var outputPath = Path.Combine(directory, "large-output.txt");
        await File.WriteAllBytesAsync(
            outputPath,
            Encoding.ASCII.GetBytes(new string('x', 300 * 1024)),
            CancellationToken.None);

        try
        {
            var runner = new SystemBuildProcessRunner();

            var result = await runner.RunAsync(
                new BuildProcessRequest(
                    "/bin/cat",
                    [outputPath],
                    directory,
                    new Dictionary<string, string>
                    {
                        ["PATH"] = "/usr/bin:/bin",
                        ["LC_ALL"] = "C"
                    },
                    TimeSpan.FromSeconds(10)),
                CancellationToken.None);

            Assert.Equal(0, result.ExitCode);
            Assert.False(result.TimedOut);
            Assert.True(result.OutputTruncated);
            Assert.True(result.StandardOutput.Length <= SystemBuildProcessRunner.MaxCapturedOutputBytes + 64);
            Assert.Contains("truncated", result.StandardOutput, StringComparison.Ordinal);
            Assert.Empty(result.StandardError);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public async Task SmallOutputCompletesWithoutTruncation()
    {
        if (!File.Exists("/bin/cat"))
        {
            return;
        }

        var directory = Path.Combine(Path.GetTempPath(), $"pkgsrv-output-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var outputPath = Path.Combine(directory, "small-output.txt");
        await File.WriteAllTextAsync(outputPath, "small output", CancellationToken.None);

        try
        {
            var result = await new SystemBuildProcessRunner().RunAsync(
                new BuildProcessRequest(
                    "/bin/cat",
                    [outputPath],
                    directory,
                    new Dictionary<string, string>
                    {
                        ["PATH"] = "/usr/bin:/bin",
                        ["LC_ALL"] = "C"
                    },
                    TimeSpan.FromSeconds(10)),
                CancellationToken.None);

            Assert.Equal(0, result.ExitCode);
            Assert.False(result.TimedOut);
            Assert.False(result.OutputTruncated);
            Assert.Equal("small output", result.StandardOutput);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }
}
