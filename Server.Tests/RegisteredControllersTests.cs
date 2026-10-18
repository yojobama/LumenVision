using System;
using System.Linq;
using Microsoft.AspNetCore.Mvc;
using Server;
using Xunit;

namespace Server.Tests;

public class RegisteredControllersTests
{
    [Fact]
    public void EveryControllerInTheServerIsRegistered()
    {
        // a controller that is not in RegisteredControllers.All is never routed: its endpoints answer 404 with nothing to say why
        Type[] defined = typeof(RegisteredControllers).Assembly.GetTypes()
            .Where(t => t.IsClass && !t.IsAbstract && typeof(ControllerBase).IsAssignableFrom(t))
            .ToArray();

        Type[] missing = defined.Where(t => !RegisteredControllers.All.Contains(t)).ToArray();

        Assert.True(missing.Length == 0, "not registered in RegisteredControllers.All: " + string.Join(", ", missing.Select(t => t.Name)));
        Assert.NotEmpty(defined);
    }
}
