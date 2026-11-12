namespace Graftcode.Pipeline;

public interface IServiceAuthenticator
{
    Task<string> GetUserIdFromClaims(string token);

    Task ValidateM2mToken(string token);

    Task ValidateUserApiToken(string token);
}
