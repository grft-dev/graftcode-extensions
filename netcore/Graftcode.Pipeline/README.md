# Graftcode.Pipeline

Middleware pipeline for Graftcode service invocations.

`ServicePipeline.RunAsync` builds a pipeline from every `IServiceMiddleware` registered in the service provider, then runs the method handler. Each call gets its own scope and a `ServiceContext` with the class name, method name, parameters, and a bag for values shared between middleware.

## Setup

Point the pipeline at the application service provider once, before the first call:

```csharp
ServicePipeline.Use(() => services.BuildServiceProvider());
```

Register middleware in the order it should run. Built-in steps:

```csharp
services.AddServiceMiddleware<LoggingMiddleware>();
services.AddServiceMiddleware<AuthenticationMiddleware>();
services.AddSingleton<IServiceAuthenticator, AppAuthenticator>();
```

`AuthenticationMiddleware` resolves `IServiceAuthenticator` from the call scope. Implement `GetUserIdFromClaims`, `ValidateM2mToken`, and `ValidateUserApiToken` for the tokens your methods accept.

## Invocation

```csharp
var userId = await ServicePipeline.RunAsync(
    className: "Orders",
    methodName: "Get",
    handler: async context =>
    {
        var orders = context.GetRequiredService<IOrderService>();
        return await orders.GetForUserAsync(context.GetRequiredUserId());
    },
    ("token", accessToken),
    ("id", orderId));
```

`GetRequiredUserId()` throws when the call requires an authenticated user and none was resolved.

## Built-in middleware

`LoggingMiddleware` logs the operation name and parameter values, then logs failures. Parameters whose names contain `token` are omitted.

`AuthenticationMiddleware` reads these parameters when they are present and non-empty:

- `token` — `GetUserIdFromClaims` sets `ServiceContext.UserId`
- `m2mToken` — `ValidateM2mToken`
- `apiToken` — `ValidateUserApiToken`

Invalid tokens throw `ArgumentException` before the handler runs.
