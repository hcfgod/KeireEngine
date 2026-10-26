using System.Reflection;
using System.Runtime.Loader;

if (args.Length != 1)
{
    throw new ArgumentException("Pass the Build/Managed directory containing runtime and DesignTime APIs.");
}

foreach (string assembly in new[] { "Keire.Managed.dll", "Keire.Editor.Managed.dll" })
{
    SortedSet<string> runtime = ReadSurface(Path.Combine(args[0], assembly));
    SortedSet<string> designTime = ReadSurface(Path.Combine(args[0], "DesignTime", assembly));
    string[] missing = runtime.Except(designTime).ToArray();
    string[] extra = designTime.Except(runtime).ToArray();
    if (missing.Length != 0 || extra.Length != 0)
    {
        throw new InvalidOperationException($"{assembly} API mismatch:\n" +
            string.Join('\n', missing.Select(value => "MISSING " + value)) + "\n" +
            string.Join('\n', extra.Select(value => "EXTRA " + value)));
    }
    Console.WriteLine($"PASS {assembly}: {runtime.Count} matching public/protected API records");
}

static SortedSet<string> ReadSurface(string path)
{
    string directory = Path.GetDirectoryName(Path.GetFullPath(path))!;
    var context = new AssemblyLoadContext("API parity: " + path, isCollectible: true);
    context.Resolving += (loadContext, name) =>
    {
        string candidate = Path.Combine(directory, name.Name + ".dll");
        return File.Exists(candidate) ? loadContext.LoadFromAssemblyPath(candidate) : null;
    };
    try
    {
        Assembly assembly = context.LoadFromAssemblyPath(Path.GetFullPath(path));
        var surface = new SortedSet<string>(StringComparer.Ordinal);
        const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic |
            BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        foreach (Type type in assembly.GetTypes().Where(VisibleType))
        {
            string owner = TypeName(type);
            surface.Add($"TYPE {owner} {type.Attributes} : {TypeName(type.BaseType)} {Attributes(type)}");
            foreach (Type implemented in type.GetInterfaces())
            {
                surface.Add($"INTERFACE {owner} : {TypeName(implemented)}");
            }
            AddGenericParameters(surface, owner, type.GetGenericArguments());
            foreach (FieldInfo field in type.GetFields(flags).Where(VisibleField))
            {
                string constant = field.IsLiteral ? FormatConstant(field.GetRawConstantValue()) : "";
                surface.Add($"FIELD {owner}.{field.Name} {field.Attributes} {TypeName(field.FieldType)} " +
                    $"{Modifiers(field.GetRequiredCustomModifiers(), field.GetOptionalCustomModifiers())} " +
                    $"{constant} {Attributes(field)}");
            }
            IEnumerable<MethodBase> methods = type.GetMethods(flags).Cast<MethodBase>()
                .Concat(type.GetConstructors(flags)).Where(VisibleMethod);
            foreach (MethodBase method in methods)
            {
                string arity = method.IsGenericMethod ? "``" + method.GetGenericArguments().Length : "";
                string signature = owner + "." + method.Name + arity + "(" +
                    string.Join(",", method.GetParameters().Select(Parameter)) + ")";
                string result = method is MethodInfo info ? Parameter(info.ReturnParameter) : "constructor";
                surface.Add($"METHOD {signature} {method.Attributes} {method.CallingConvention} " +
                    $"{result} {Attributes(method)}");
                if (method.IsGenericMethod)
                {
                    AddGenericParameters(surface, signature, method.GetGenericArguments());
                }
            }
            foreach (PropertyInfo property in type.GetProperties(flags)
                .Where(value => value.GetAccessors(true).Any(VisibleMethod)))
            {
                surface.Add($"PROPERTY {owner}.{property.Name} {property.Attributes} " +
                    $"{TypeName(property.PropertyType)} ({string.Join(",", property.GetIndexParameters().Select(Parameter))}) " +
                    $"{Modifiers(property.GetRequiredCustomModifiers(), property.GetOptionalCustomModifiers())} " +
                    Attributes(property));
            }
            foreach (EventInfo item in type.GetEvents(flags).Where(value =>
                (value.AddMethod is not null && VisibleMethod(value.AddMethod)) ||
                (value.RemoveMethod is not null && VisibleMethod(value.RemoveMethod))))
            {
                surface.Add($"EVENT {owner}.{item.Name} {item.Attributes} " +
                    $"{TypeName(item.EventHandlerType)} {Attributes(item)}");
            }
        }
        return surface;
    }
    finally
    {
        context.Unload();
    }
}

static bool VisibleType(Type type)
{
    return type.DeclaringType is null ? type.IsPublic :
        VisibleType(type.DeclaringType) && (type.IsNestedPublic || type.IsNestedFamily || type.IsNestedFamORAssem);
}

static bool VisibleField(FieldInfo field)
{
    return field.IsPublic || field.IsFamily || field.IsFamilyOrAssembly;
}

static bool VisibleMethod(MethodBase method)
{
    return method.IsPublic || method.IsFamily || method.IsFamilyOrAssembly;
}

static string TypeName(Type? type)
{
    if (type is null)
    {
        return "";
    }
    if (type.HasElementType)
    {
        string suffix = type.IsByRef ? "&" : type.IsPointer ? "*" :
            "[" + new string(',', type.GetArrayRank() - 1) + "]";
        return TypeName(type.GetElementType()) + suffix;
    }
    if (type.IsGenericParameter)
    {
        return (type.DeclaringMethod is null ? "!" : "!!") + type.GenericParameterPosition;
    }
    if (type.IsGenericType)
    {
        return type.GetGenericTypeDefinition().FullName + "<" +
            string.Join(",", type.GetGenericArguments().Select(TypeName)) + ">";
    }
    return type.FullName ?? type.Name;
}

static string Parameter(ParameterInfo parameter)
{
    return $"{TypeName(parameter.ParameterType)} {parameter.Name} {parameter.Attributes} " +
        $"{Modifiers(parameter.GetRequiredCustomModifiers(), parameter.GetOptionalCustomModifiers())} " +
        $"{(parameter.HasDefaultValue ? FormatConstant(parameter.RawDefaultValue) : "")} " +
        string.Join(";", parameter.GetCustomAttributesData().Select(value => value.ToString()).Order());
}

static string Modifiers(Type[] required, Type[] optional)
{
    return $"required:{string.Join(",", required.Select(TypeName))} optional:{string.Join(",", optional.Select(TypeName))}";
}

static string Attributes(MemberInfo member)
{
    return string.Join(";", member.GetCustomAttributesData().Select(value => value.ToString()).Order());
}

static string FormatConstant(object? value)
{
    return value is null ? "null" : value.GetType().FullName + ":" +
        Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture);
}

static void AddGenericParameters(SortedSet<string> surface, string owner, IEnumerable<Type> parameters)
{
    foreach (Type parameter in parameters.Where(value => value.IsGenericParameter))
    {
        surface.Add($"GENERIC {owner} {TypeName(parameter)} {parameter.Name} {parameter.GenericParameterAttributes} " +
            string.Join(",", parameter.GetGenericParameterConstraints().Select(TypeName).Order()) + " " + Attributes(parameter));
    }
}
