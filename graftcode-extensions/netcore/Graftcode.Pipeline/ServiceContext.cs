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

    public T GetRequiredService<T>() where T : notnull => Services.GetRequiredService<T>();

    public T? GetService<T>() => Services.GetService<T>();

    public object? GetParameter(string name) =>
        Parameters.FirstOrDefault(parameter => parameter.Name.Equals(name, StringComparison.OrdinalIgnoreCase)).Value;
}
