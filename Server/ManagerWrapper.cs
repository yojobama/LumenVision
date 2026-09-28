namespace Server;

// singleton Manager instance shared by the whole server
public class ManagerWrapper : Manager
{
    public static ManagerWrapper Instance { get; } = new ManagerWrapper();

    private ManagerWrapper() : base() {}
}