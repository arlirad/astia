using System.Diagnostics;

namespace Pkgsrv.Infrastructure.Builds;

public sealed record BuildProcessRequest(
    string FileName,
    IReadOnlyList<string> Arguments,
    string WorkingDirectory,
    IReadOnlyDictionary<string, string> Environment,
    TimeSpan Timeout);

public sealed record BuildProcessResult(
    int ExitCode,
    string StandardOutput,
    string StandardError,
    bool TimedOut,
    bool OutputTruncated = false);

public interface IBuildProcessRunner
{
    Task<BuildProcessResult> RunAsync(
        BuildProcessRequest request,
        CancellationToken cancellationToken);
}

public sealed class SystemBuildProcessRunner : IBuildProcessRunner
{
    public const int MaxCapturedOutputBytes = 64 * 1024;

    public async Task<BuildProcessResult> RunAsync(
        BuildProcessRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.Arguments);
        ArgumentNullException.ThrowIfNull(request.Environment);

        var startInfo = new ProcessStartInfo
        {
            FileName = request.FileName,
            WorkingDirectory = request.WorkingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        foreach (var argument in request.Arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        startInfo.Environment.Clear();

        foreach (var variable in request.Environment)
        {
            startInfo.Environment[variable.Key] = variable.Value;
        }

        using var process = new Process { StartInfo = startInfo };
        process.Start();

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(request.Timeout);

        var stdoutTask = ReadBoundedAsync(process.StandardOutput.BaseStream);
        var stderrTask = ReadBoundedAsync(process.StandardError.BaseStream);
        var timedOut = false;

        try
        {
            await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            timedOut = true;
            TryKill(process);
        }

        var stdout = await stdoutTask.ConfigureAwait(false);
        var stderr = await stderrTask.ConfigureAwait(false);

        return new BuildProcessResult(
            timedOut ? -1 : process.ExitCode,
            stdout.Text,
            stderr.Text,
            timedOut,
            stdout.Truncated || stderr.Truncated);
    }

    private static async Task<(string Text, bool Truncated)> ReadBoundedAsync(Stream stream)
    {
        var capture = new BoundedOutputCapture(MaxCapturedOutputBytes);
        var buffer = new byte[8192];

        try
        {
            int read;

            while ((read = await stream.ReadAsync(buffer).ConfigureAwait(false)) > 0)
            {
                capture.Append(buffer.AsSpan(0, read));
            }
        }
        catch (IOException)
        {
        }
        catch (ObjectDisposedException)
        {
        }

        return capture.ToResult();
    }

    private static void TryKill(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                process.WaitForExit(5000);
            }
        }
        catch (InvalidOperationException)
        {
        }
        catch (System.ComponentModel.Win32Exception)
        {
        }
    }

    private sealed class BoundedOutputCapture
    {
        private const string TruncationMarker = "\n...[output truncated]...\n";

        private readonly int _halfLimit;
        private readonly MemoryStream _prefix = new();
        private readonly Queue<byte[]> _tail = new();
        private int _tailCount;
        private long _total;

        public BoundedOutputCapture(int limit)
        {
            _halfLimit = Math.Max(1, limit / 2);
        }

        public void Append(ReadOnlySpan<byte> bytes)
        {
            var prefixRemaining = _halfLimit - (int)_prefix.Length;

            if (prefixRemaining > 0)
            {
                var prefixCount = Math.Min(prefixRemaining, bytes.Length);
                _prefix.Write(bytes[..prefixCount]);
                bytes = bytes[prefixCount..];
            }

            if (bytes.IsEmpty)
            {
                return;
            }

            var copy = bytes.ToArray();
            _tail.Enqueue(copy);
            _tailCount += copy.Length;
            _total += copy.Length;

            while (_tailCount > _halfLimit && _tail.Count > 0)
            {
                var first = _tail.Peek();
                var excess = _tailCount - _halfLimit;

                if (first.Length <= excess)
                {
                    _tail.Dequeue();
                    _tailCount -= first.Length;
                }
                else
                {
                    _tail.Dequeue();
                    _tail.Enqueue(first.AsSpan(excess).ToArray());
                    _tailCount -= excess;
                }
            }
        }

        public (string Text, bool Truncated) ToResult()
        {
            var prefix = _prefix.ToArray();
            var tailLength = _tail.Sum(chunk => chunk.Length);
            var tail = new byte[tailLength];
            var offset = 0;

            foreach (var chunk in _tail)
            {
                chunk.CopyTo(tail, offset);
                offset += chunk.Length;
            }

            var truncated = _total + _prefix.Length > MaxCapturedOutputBytes;

            if (!truncated)
            {
                var complete = new byte[prefix.Length + tail.Length];
                prefix.CopyTo(complete, 0);
                tail.CopyTo(complete, prefix.Length);
                return (System.Text.Encoding.UTF8.GetString(complete), false);
            }

            var marker = System.Text.Encoding.UTF8.GetBytes(TruncationMarker);
            var result = new byte[prefix.Length + marker.Length + tail.Length];
            prefix.CopyTo(result, 0);
            marker.CopyTo(result, prefix.Length);
            tail.CopyTo(result, prefix.Length + marker.Length);

            return (System.Text.Encoding.UTF8.GetString(result), true);
        }
    }
}
