namespace Irva.Core.ValueObjects;

public class Error(string message)
{
    public string Message => message;
}