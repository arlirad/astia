using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Accounts;

public class UsernameNotSpecified() : Error("account.username.not_specified");
public class UsernameInvalidFormat() : Error("account.username.invalid_format");
public class UsernameTooLong() : Error("account.username.too_long");
public class DuplicateUsername() : Error("account.username.duplicate");
public class AuthenticationDataNotSpecified() : Error("account.authentication_data.not_specified");
public class AuthenticationDataTooLong() : Error("account.authentication_data.too_long");
public class AccountNotFound() : Error("account.not_found");
public class OldPasswordIncorrect() : Error("account.old_password.incorrect");