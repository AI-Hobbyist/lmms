using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Microsoft.Build.Framework;

// MSBuild events give target progress independently of console language and verbosity.
public sealed class BuildProgressLogger : ILogger
{
    private readonly object sync = new object();
    private readonly HashSet<string> active = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private StreamWriter output;

    public LoggerVerbosity Verbosity { get; set; }
    public string Parameters { get; set; }

    public void Initialize(IEventSource source)
    {
        output = new StreamWriter(Parameters, true, new UTF8Encoding(false));
        output.AutoFlush = true;
        source.ProjectStarted += OnProjectStarted;
        source.ProjectFinished += OnProjectFinished;
    }

    private void OnProjectStarted(object sender, ProjectStartedEventArgs args)
    {
        if (!args.ProjectFile.EndsWith(".vcxproj", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }
        if (!string.IsNullOrEmpty(args.TargetNames)
            && !Array.Exists(args.TargetNames.Split(';'), target => target == "Build"))
        {
            return;
        }
        lock (sync)
        {
            active.Add(args.ProjectFile);
            output.WriteLine("start\t" + args.ProjectFile);
        }
    }

    private void OnProjectFinished(object sender, ProjectFinishedEventArgs args)
    {
        lock (sync)
        {
            if (active.Remove(args.ProjectFile))
            {
                output.WriteLine((args.Succeeded ? "finish" : "fail") + "\t" + args.ProjectFile);
            }
        }
    }

    public void Shutdown()
    {
        lock (sync)
        {
            output.Dispose();
        }
    }
}
