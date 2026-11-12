namespace Graftcode.Pipeline;

public delegate Task ServiceMiddlewareDelegate(ServiceContext context);

public interface IServiceMiddleware
{
    Task InvokeAsync(ServiceContext context, ServiceMiddlewareDelegate next);
}
