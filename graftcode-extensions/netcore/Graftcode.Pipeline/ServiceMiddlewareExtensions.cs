using Microsoft.Extensions.DependencyInjection;

namespace Graftcode.Pipeline;

public static class ServiceMiddlewareExtensions
{
    public static IServiceCollection AddServiceMiddleware<TMiddleware>(this IServiceCollection services)
        where TMiddleware : class, IServiceMiddleware =>
        services.AddSingleton<IServiceMiddleware, TMiddleware>();
}
