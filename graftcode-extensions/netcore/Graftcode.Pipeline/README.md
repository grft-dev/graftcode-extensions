# Graftcode.Pipeline

Middleware pipeline for Graftcode service invocations.

`ServicePipeline.RunAsync` builds a pipeline from every `IServiceMiddleware` registered in the service provider, then runs the method handler. Each call gets its own scope and a `ServiceContext` with the class name, method name, parameters, and a bag for values shared between middleware.

## Setup

Point the pipeline at the application service provider once, before the first call:

```csharp
ServicePipeline.Use(() => services.BuildServiceProvider());
```

Register middleware in the order it should run:

```csharp
services.AddServiceMiddleware<TimingMiddleware>();
```

```csharp
using System.Diagnostics;

public sealed class TimingMiddleware : IServiceMiddleware
{
    public async Task InvokeAsync(ServiceContext context, ServiceMiddlewareDelegate next)
    {
        var started = Stopwatch.StartNew();
        try
        {
            await next(context);
        }
        finally
        {
            context.Items["elapsed"] = started.Elapsed;
        }
    }
}
```

## Invocation

```csharp
var order = await ServicePipeline.RunAsync(
    className: "Orders",
    methodName: "Get",
    handler: async context =>
    {
        var orders = context.GetRequiredService<IOrderService>();
        var id = context.GetParameter("id");
        return await orders.GetAsync(id);
    },
    ("id", orderId));
```
