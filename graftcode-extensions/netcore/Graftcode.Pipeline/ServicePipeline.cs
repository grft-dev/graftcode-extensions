using Microsoft.Extensions.DependencyInjection;

namespace Graftcode.Pipeline;

public static class ServicePipeline
{
    private static readonly object Gate = new();
    private static Func<IServiceProvider>? _resolve;

    public static void Use(Func<IServiceProvider> resolve)
    {
        ArgumentNullException.ThrowIfNull(resolve);
        lock (Gate)
        {
            _resolve = resolve;
        }
    }

    public static async Task<T> RunAsync<T>(
        string className,
        string methodName,
        Func<ServiceContext, Task<T>> handler,
        params (string Name, object? Value)[] parameters)
    {
        using var scope = Provider.CreateScope();
        var context = new ServiceContext(className, methodName, scope.ServiceProvider, parameters);

        var result = default(T)!;
        ServiceMiddlewareDelegate terminal = async ctx => result = await handler(ctx);

        await BuildPipeline(context.Services, terminal)(context);
        return result;
    }

    public static async Task RunAsync(
        string className,
        string methodName,
        Func<ServiceContext, Task> handler,
        params (string Name, object? Value)[] parameters)
    {
        using var scope = Provider.CreateScope();
        var context = new ServiceContext(className, methodName, scope.ServiceProvider, parameters);

        ServiceMiddlewareDelegate terminal = async ctx => await handler(ctx);

        await BuildPipeline(context.Services, terminal)(context);
    }

    private static IServiceProvider Provider
    {
        get
        {
            Func<IServiceProvider>? resolve;
            lock (Gate)
            {
                resolve = _resolve;
            }

            if (resolve is null)
            {
                throw new InvalidOperationException("Use must be called before the first RunAsync.");
            }

            return resolve();
        }
    }

    private static ServiceMiddlewareDelegate BuildPipeline(IServiceProvider services, ServiceMiddlewareDelegate terminal)
    {
        var middleware = services.GetServices<IServiceMiddleware>().ToArray();

        var next = terminal;
        for (var i = middleware.Length - 1; i >= 0; i--)
        {
            var current = middleware[i];
            var localNext = next;
            next = context => current.InvokeAsync(context, localNext);
        }

        return next;
    }
}
