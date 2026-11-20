using System.Diagnostics;
using System.Text.Json;
using GraftCode.Netcore.Telemetry.AppInsightsConnector;
using Hypertube.Netcore.Sdk;
using Hypertube.Netcore.Utils.ConnectionData;
using Microsoft.Extensions.Logging;

var configPath = args.Length > 0 ? args[0] : Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "matrix-config.json");
var jsonOptions = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
var config = JsonSerializer.Deserialize<MatrixConfig>(File.ReadAllText(Path.GetFullPath(configPath)), jsonOptions)
    ?? throw new InvalidOperationException("matrix-config.json missing");

var connectionString = Environment.GetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING");
if (string.IsNullOrWhiteSpace(connectionString))
{
    Console.Error.WriteLine("APPLICATIONINSIGHTS_CONNECTION_STRING is not set");
    return 2;
}

using var pipeline = GraftCodeAppInsightsConnector.Start(options =>
{
    options.ServiceName = "CrossTech.OneToOne.DotNetCaller";
    options.ConnectionString = connectionString;
});

foreach (var (calleeKey, callee) in config.Hosts)
{
    var input = $"matrix-dotnet-{calleeKey}".ToLowerInvariant();
    try
    {
        var traceId = string.Empty;
        var result = string.Empty;
        using (var activity = pipeline.StartActivity($"CrossMatrix.DotNet->{calleeKey}", ActivityKind.Server))
        {
            traceId = Activity.Current?.TraceId.ToHexString() ?? string.Empty;

            pipeline.Logger.LogInformation(
                "CrossMatrix Caller=DotNet Callee={Callee} Input={Input} TraceId={TraceId}",
                calleeKey,
                input,
                traceId);

            var factory = RuntimeBridge.WebSocket(new WsConnectionData(callee.Ws));
            var runtime = callee.RuntimeArg switch
            {
                "jvm" => factory.Jvm(),
                "python" => factory.Python(),
                "nodejs" => factory.Nodejs(),
                _ => factory.Netcore(),
            };

            var value = runtime
                .GetType(callee.TypeName)
                .InvokeStaticMethod(callee.MethodName, input)
                .Execute()
                .GetValue();

            result = value?.ToString() ?? string.Empty;
            pipeline.Logger.LogInformation("CrossMatrix after call Result={Result}", (object)result);
        }
        pipeline.ForceFlush(15000);

        Console.WriteLine($"OK|DotNet|{calleeKey}|{traceId}|{result.Replace('|', '/')}");
    }
    catch (Exception ex)
    {
        Console.WriteLine($"FAIL|DotNet|{calleeKey}||{ex.GetType().Name}: {ex.Message.Replace('|', '/')}");
    }
}

return 0;

internal sealed class MatrixConfig
{
    public Dictionary<string, CalleeConfig> Hosts { get; set; } = new();
}

internal sealed class CalleeConfig
{
    public string Ws { get; set; } = string.Empty;
    public string RuntimeArg { get; set; } = string.Empty;
    public string TypeName { get; set; } = string.Empty;
    public string MethodName { get; set; } = string.Empty;
}