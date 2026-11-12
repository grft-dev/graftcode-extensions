using Microsoft.Extensions.DependencyInjection;

namespace Graftcode.Pipeline;

public sealed class ServiceContext
{
    public ServiceContext(
        string className,
        string methodName,
        IServiceProvider services,
        IReadOnlyList<(string Name, object? Value)> parameters)
    {
        ClassName = className;
        MethodName = methodName;
        Services = services;
        Parameters = parameters;
    }

    public string ClassName { get; }

    public string MethodName { get; }

    public IServiceProvider Services { get; }

    public IReadOnlyList<(string Name, object? Value)> Parameters { get; }

    public IDictionary<string, object?> Items { get; } = new Dictionary<string, object?>();

    public string OperationName => $"{ClassName}.{MethodName}";

    public string? UserId { get; set; }

    public T GetRequiredService<T>() where T : notnull => Services.GetRequiredService<T>();

    public T? GetService<T>() => Services.GetService<T>();

    public object? GetParameter(string name) =>
        Parameters.FirstOrDefault(parameter => parameter.Name.Equals(name, StringComparison.OrdinalIgnoreCase)).Value;

    public string GetRequiredUserId() =>
        UserId ?? throw new InvalidOperationException(
            $"{OperationName} requires an authenticated user, but none was resolved. " +
            "Ensure a token parameter is supplied and the authentication step is registered.");
}
