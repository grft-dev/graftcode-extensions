using Microsoft.Extensions.Logging;

namespace Graftcode.Pipeline;

public sealed class LoggingMiddleware : IServiceMiddleware
{
    private readonly ILogger<LoggingMiddleware> _logger;

    public LoggingMiddleware(ILogger<LoggingMiddleware> logger) => _logger = logger;

    public async Task InvokeAsync(ServiceContext context, ServiceMiddlewareDelegate next)
    {
        try
        {
            LogInvocation(context);
            await next(context);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed {OperationName}", context.OperationName);
            throw;
        }
    }

    private void LogInvocation(ServiceContext context)
    {
        var loggable = context.Parameters
            .Where(parameter => !parameter.Name.Contains("token", StringComparison.OrdinalIgnoreCase))
            .ToArray();

        if (loggable.Length == 0)
        {
            _logger.LogInformation("Called {OperationName}", context.OperationName);
            return;
        }

        var paramTemplate = string.Join(", ", loggable.Select(parameter => $"{parameter.Name} {{{parameter.Name}}}"));
        var args = new object[loggable.Length + 1];
        args[0] = context.OperationName;
        for (var i = 0; i < loggable.Length; i++)
        {
            args[i + 1] = loggable[i].Value ?? "null";
        }

        _logger.LogInformation($"Called {{OperationName}} with params {paramTemplate}", args);
    }
}
