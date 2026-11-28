using Irva.Core.ValueObjects;

namespace Irva.Core.Aggregates.Applications;

public class ApplicationNameNotSpecified() : Error("application.name.not_specified");
public class ApplicationNameTooLong() : Error("application.name.too_long");
public class ApplicationNotFound() : Error("application.not_found");
public class ApplicationTokenNameNotSpecified() : Error("application.token.name.not_specified");
public class ApplicationTokenNameTooLong() : Error("application.token.name.too_long");
public class ApplicationTokenNotSpecified() : Error("application.token.not_specified");
public class ApplicationTokenNotFound() : Error("application.token.not_found");
public class ApplicationTokenRevoked() : Error("application.token.revoked");
public class ApplicationNotArchived() : Error("application.not_archived");
public class ApplicationNameDuplicate() : Error("application.name.duplicate");
public class ApplicationIsArchived() : Error("application.is_archived");
public class MasterKeyInvalid() : Error("auth.master_key.invalid");
