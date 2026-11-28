namespace Irva.Core.ValueObjects;

public class Result<T>(bool isSuccess, T value, Error? error)
{
    public bool IsSuccess => isSuccess;
    public T? Value => value;
    public Error? Error => error;

    public static Result<T> Ok(T value) => new(true, value, null);
    public static Result<T> Fail(Error error) => new(false, default!, error);
}

public class Result(bool isSuccess, Error? error)
{
    public bool IsSuccess => isSuccess;
    public Error? Error => error;

    public static Result Ok() => new(true, null);
    public static Result Fail(Error error) => new(false, error);
}