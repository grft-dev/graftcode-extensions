namespace Graftcode.Pipeline;

public sealed class AuthenticationMiddleware : IServiceMiddleware
{
    public const string TokenParameterName = "token";
    public const string M2mTokenParameterName = "m2mToken";
    public const string ApiTokenParameterName = "apiToken";

    public async Task InvokeAsync(ServiceContext context, ServiceMiddlewareDelegate next)
    {
        IServiceAuthenticator? authenticator = null;

        if (context.GetParameter(TokenParameterName) is string token && !string.IsNullOrEmpty(token))
        {
            var userId = await Resolve(context).GetUserIdFromClaims(token);
            if (string.IsNullOrEmpty(userId))
            {
                throw new ArgumentException("Invalid user id in token claims");
            }

            context.UserId = userId;
        }

        if (context.GetParameter(M2mTokenParameterName) is string m2mToken && !string.IsNullOrEmpty(m2mToken))
        {
            try
            {
                await Resolve(context).ValidateM2mToken(m2mToken);
            }
            catch (Exception ex)
            {
                throw new ArgumentException("Invalid m2m token", ex);
            }
        }

        if (context.GetParameter(ApiTokenParameterName) is string apiToken && !string.IsNullOrEmpty(apiToken))
        {
            try
            {
                await Resolve(context).ValidateUserApiToken(apiToken);
            }
            catch (Exception ex)
            {
                throw new ArgumentException("Invalid api token", ex);
            }
        }

        await next(context);
        return;

        IServiceAuthenticator Resolve(ServiceContext source) =>
            authenticator ??= source.GetRequiredService<IServiceAuthenticator>();
    }
}
