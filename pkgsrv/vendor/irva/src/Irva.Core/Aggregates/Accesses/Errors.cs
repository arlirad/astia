using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Accesses;

public class TokenNotSpecified() : Error("access.token.not_specified");
public class TokenRevoked() : Error("access.token.revoked");
public class TokenExpired() : Error("access.token.expired");
public class TokenNotFound() : Error("access.token.not_found");
public class InvalidCredentials() : Error("auth.invalid_credentials");
public class RefreshTokenNotFound() : Error("access.refresh_token.not_found");
