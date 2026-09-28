using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Routing;

namespace Server.OpenApi
{
    // Reflection-based OpenAPI 3.0 generator over the controllers' attributes: operationIds {Controller}_{Method}, PascalCase properties, numeric enums.
    // Handles route-template params, [FromQuery] primitives, one [FromBody] parameter and Task/Task<T> returns; paths exclude /api.
    public static class OpenApiGenerator
    {
        // Endpoints that write a raw text body via SendStringAsync (see WebRTCSinkController); reflection only sees Task, so they are listed.
        private static readonly HashSet<string> PlainTextResponseMethods = new()
        {
            "WebRTCSinkController.CreateOffer",
        };

        private static readonly HashSet<string> PlainTextRequestMethods = new()
        {
            "WebRTCSinkController.SetAnswer",
        };

        public static JsonObject Generate(IEnumerable<Type> controllerTypes)
        {
            var paths = new JsonObject();
            var schemas = new JsonObject();
            var schemaNames = new Dictionary<Type, string>();

            foreach (var controllerType in controllerTypes.OrderBy(t => t.Name))
            {
                string tag = controllerType.Name.EndsWith("Controller")
                    ? controllerType.Name[..^"Controller".Length]
                    : controllerType.Name;

                foreach (var method in controllerType.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly))
                {
                    var routeAttr = method.GetCustomAttribute<HttpMethodAttribute>();
                    if (routeAttr == null || routeAttr.Template == null) continue;

                    // templates are relative to the global "api/" prefix (ApiPrefixConvention);
                    // documented paths have always been the un-prefixed form
                    string path = "/" + routeAttr.Template.TrimStart('/');
                    string verb = routeAttr.HttpMethods.First().ToLowerInvariant();
                    string methodKey = $"{controllerType.Name}.{method.Name}";

                    var routeParamNames = ExtractRouteParamNames(path);
                    var parameters = new JsonArray();
                    JsonObject? requestBody = null;

                    foreach (var p in method.GetParameters())
                    {
                        if (p.GetCustomAttribute<FromBodyAttribute>() != null)
                        {
                            requestBody = new JsonObject
                            {
                                ["required"] = true,
                                ["content"] = PlainTextRequestMethods.Contains(methodKey)
                                    ? new JsonObject { ["text/plain"] = new JsonObject() }
                                    : new JsonObject { ["application/json"] = new JsonObject { ["schema"] = SchemaFor(p.ParameterType, schemas, schemaNames) } }
                            };
                            continue;
                        }

                        bool isRouteParam = routeParamNames.Contains(p.Name!);
                        parameters.Add(new JsonObject
                        {
                            ["name"] = p.Name,
                            ["in"] = isRouteParam ? "path" : "query",
                            ["required"] = isRouteParam || (!p.HasDefaultValue && !IsNullable(p.ParameterType)),
                            ["schema"] = SchemaFor(p.ParameterType, schemas, schemaNames)
                        });
                    }

                    var operation = new JsonObject
                    {
                        ["operationId"] = $"{controllerType.Name}_{method.Name}",
                        ["tags"] = new JsonArray(tag),
                        ["parameters"] = parameters
                    };
                    if (requestBody != null) operation["requestBody"] = requestBody;

                    Type returnType = UnwrapTask(method.ReturnType);
                    var responses = new JsonObject();
                    if (returnType == typeof(void))
                    {
                        responses["200"] = new JsonObject { ["description"] = "OK" };
                    }
                    else if (PlainTextResponseMethods.Contains(methodKey) || returnType == typeof(string) && methodKey == "WebRTCSinkController.CreateOffer")
                    {
                        responses["200"] = new JsonObject
                        {
                            ["description"] = "OK",
                            ["content"] = new JsonObject { ["text/plain"] = new JsonObject() }
                        };
                    }
                    else
                    {
                        responses["200"] = new JsonObject
                        {
                            ["description"] = "OK",
                            ["content"] = new JsonObject
                            {
                                ["application/json"] = new JsonObject { ["schema"] = SchemaFor(returnType, schemas, schemaNames) }
                            }
                        };
                    }
                    operation["responses"] = responses;

                    if (paths[path] is not JsonObject pathItem)
                    {
                        pathItem = new JsonObject();
                        paths[path] = pathItem;
                    }
                    pathItem[verb] = operation;
                }
            }

            return new JsonObject
            {
                ["openapi"] = "3.0.3",
                ["info"] = new JsonObject { ["title"] = "LumenVision Server API", ["version"] = "1.0" },
                ["paths"] = paths,
                ["components"] = new JsonObject { ["schemas"] = schemas }
            };
        }

        private static readonly Regex RouteParamPattern = new(@"\{([^}:]+)(:[^}]+)?\}", RegexOptions.Compiled);

        private static HashSet<string> ExtractRouteParamNames(string route) =>
            RouteParamPattern.Matches(route).Select(m => m.Groups[1].Value).ToHashSet();

        private static Type UnwrapTask(Type t)
        {
            if (t == typeof(System.Threading.Tasks.Task)) return typeof(void);
            if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(System.Threading.Tasks.Task<>))
                t = t.GetGenericArguments()[0];
            // an IActionResult action (e.g. RecordSinkController.Download's PhysicalFile) writes a non-JSON body
            if (typeof(IActionResult).IsAssignableFrom(t)) return typeof(void);
            return t;
        }

        private static bool IsNullable(Type t) =>
            !t.IsValueType || Nullable.GetUnderlyingType(t) != null;

        // JSON Schema for a C# type. Enums serialise as their underlying int (no JsonStringEnumConverter is registered).
        private static JsonNode SchemaFor(Type type, JsonObject schemas, Dictionary<Type, string> schemaNames)
        {
            type = Nullable.GetUnderlyingType(type) ?? type;

            if (type == typeof(string)) return new JsonObject { ["type"] = "string" };
            if (type == typeof(bool)) return new JsonObject { ["type"] = "boolean" };
            if (type == typeof(int) || type == typeof(long) || type == typeof(short))
                return new JsonObject { ["type"] = "integer" };
            if (type == typeof(double) || type == typeof(float) || type == typeof(decimal))
                return new JsonObject { ["type"] = "number" };
            if (type.IsEnum)
                return new JsonObject { ["type"] = "integer", ["x-enum-names"] = new JsonArray(Enum.GetNames(type).Select(n => (JsonNode)n).ToArray()) };

            if (type.IsArray)
                return new JsonObject { ["type"] = "array", ["items"] = SchemaFor(type.GetElementType()!, schemas, schemaNames) };

            if (typeof(System.Collections.IEnumerable).IsAssignableFrom(type) && type != typeof(string))
            {
                Type elementType = type.IsGenericType ? type.GetGenericArguments().FirstOrDefault() ?? typeof(object) : typeof(object);
                return new JsonObject { ["type"] = "array", ["items"] = SchemaFor(elementType, schemas, schemaNames) };
            }

            if (type == typeof(object)) return new JsonObject();

            // a plain DTO/record/class - reference a named schema, generating it once
            if (!schemaNames.TryGetValue(type, out string? name))
            {
                name = type.Name;
                schemaNames[type] = name;
                var properties = new JsonObject();
                var required = new JsonArray();
                foreach (var prop in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
                {
                    if (prop.GetIndexParameters().Length > 0) continue; // skip indexers
                    properties[prop.Name] = SchemaFor(prop.PropertyType, schemas, schemaNames);
                    if (!IsNullable(prop.PropertyType)) required.Add(prop.Name);
                }
                var schema = new JsonObject { ["type"] = "object", ["properties"] = properties };
                if (required.Count > 0) schema["required"] = required;
                schemas[name] = schema;
            }
            return new JsonObject { ["$ref"] = $"#/components/schemas/{name}" };
        }
    }
}
