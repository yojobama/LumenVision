using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Filters;
using Microsoft.AspNetCore.Mvc.ApplicationModels;
using Microsoft.AspNetCore.Mvc.ApplicationParts;
using Microsoft.AspNetCore.Mvc.Controllers;
using Microsoft.AspNetCore.Mvc.Routing;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace Server.Web
{
    // Registers exactly RegisteredControllers.All (shared with OpenApiGenerator), not every public controller in the assembly.
    public sealed class RegisteredControllerFeatureProvider : IApplicationFeatureProvider<ControllerFeature>
    {
        public void PopulateFeature(IEnumerable<ApplicationPart> parts, ControllerFeature feature)
        {
            feature.Controllers.Clear();
            foreach (var type in RegisteredControllers.All)
                feature.Controllers.Add(type.GetTypeInfo());
        }
    }

    // 400 for a query/route value that can't be converted to its parameter type (without [ApiController], MVC would bind
    // default(T) and run the action). An absent key is not an error and still binds the parameter's default.
    public sealed class RejectUnparseableParametersFilter : IActionFilter
    {
        public void OnActionExecuting(ActionExecutingContext context)
        {
            if (context.ModelState.IsValid) return;
            var errors = context.ModelState
                .Where(kv => kv.Value?.Errors.Count > 0)
                .Select(kv => $"{kv.Key}: {string.Join("; ", kv.Value!.Errors.Select(e => e.ErrorMessage))}");
            context.Result = new ContentResult
            {
                StatusCode = 400,
                ContentType = "text/plain; charset=utf-8",
                Content = "invalid parameter(s) - " + string.Join(" | ", errors),
            };
        }

        public void OnActionExecuted(ActionExecutedContext context) { }
    }

    // Prefixes every attribute route with "api/".
    public sealed class ApiPrefixConvention : IApplicationModelConvention
    {
        private readonly AttributeRouteModel _prefix = new(new RouteAttribute("api"));

        public void Apply(ApplicationModel application)
        {
            foreach (var selector in application.Controllers.SelectMany(c => c.Actions).SelectMany(a => a.Selectors))
            {
                if (selector.AttributeRouteModel != null)
                    selector.AttributeRouteModel = AttributeRouteModel.CombineAttributeRouteModel(_prefix, selector.AttributeRouteModel);
            }
        }
    }
}
