// This file is copied into a temporary Unity project's Assets/Editor directory by the
// VFX parity manifest generation workflow. It intentionally uses reflection because
// Unity's VFX catalog types are internal to Unity.VisualEffectGraph.Editor.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using UnityEditor;
using UnityEngine;

[InitializeOnLoad]
internal static class UnityVfxCatalogExporter
{
    private const string OutputEnvironmentVariable = "KEIRE_UNITY_VFX_CATALOG_OUTPUT";

    [Serializable]
    private sealed class Catalog
    {
        public string unityVersion;
        public List<Entry> entries = new List<Entry>();
        public List<string> errors = new List<string>();
    }

    [Serializable]
    private sealed class Entry
    {
        public string kind;
        public string label;
        public string category;
        public string modelType;
        public string sourcePath;
        public string documentationUrl;
        public bool experimental;
        public List<string> synonyms = new List<string>();
        public List<Setting> settings = new List<Setting>();
    }

    [Serializable]
    private sealed class Setting
    {
        public string name;
        public string label;
        public string type;
        public string defaultValue;
        public string visibility;
        public string tooltip;
        public List<string> enumValues = new List<string>();
        public List<string> annotations = new List<string>();
    }

    static UnityVfxCatalogExporter()
    {
        if (!Application.isBatchMode ||
            string.IsNullOrEmpty(Environment.GetEnvironmentVariable(OutputEnvironmentVariable)))
            return;

        EditorApplication.delayCall += Export;
    }

    private static void Export()
    {
        var outputPath = Environment.GetEnvironmentVariable(OutputEnvironmentVariable);
        var catalog = new Catalog { unityVersion = Application.unityVersion };
        const string experimentalKey = "VFX.displayExperimentalOperatorKey";
        var hadExperimentalPreference = EditorPrefs.HasKey(experimentalKey);
        var previousExperimentalPreference = EditorPrefs.GetBool(experimentalKey, false);

        try
        {
            EditorPrefs.SetBool(experimentalKey, true);
            var libraryType = FindType("UnityEditor.VFX.VFXLibrary");
            if (libraryType == null)
                throw new InvalidOperationException("UnityEditor.VFX.VFXLibrary was not loaded.");

            ExportKind(libraryType, "GetOperators", "Operator", catalog);
            ExportKind(libraryType, "GetBlocks", "Block", catalog);
            ExportKind(libraryType, "GetContexts", "Context", catalog);

            catalog.entries = catalog.entries.OrderBy(entry => entry.kind, StringComparer.Ordinal)
                                  .ThenBy(entry => entry.category, StringComparer.Ordinal)
                                  .ThenBy(entry => entry.label, StringComparer.Ordinal)
                                  .ThenBy(entry => entry.modelType, StringComparer.Ordinal)
                                  .ToList();
        }
        catch (Exception exception)
        {
            catalog.errors.Add(exception.ToString());
        }
        finally
        {
            if (hadExperimentalPreference)
                EditorPrefs.SetBool(experimentalKey, previousExperimentalPreference);
            else
                EditorPrefs.DeleteKey(experimentalKey);
        }

        outputPath = Path.GetFullPath(outputPath);
        Directory.CreateDirectory(Path.GetDirectoryName(outputPath));
        File.WriteAllText(outputPath, JsonUtility.ToJson(catalog, true) + "\n");
        EditorApplication.Exit(catalog.errors.Count == 0 ? 0 : 1);
    }

    private static void ExportKind(Type libraryType, string methodName, string defaultKind, Catalog catalog)
    {
        var method =
            libraryType.GetMethod(methodName, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
        if (method == null)
            throw new MissingMethodException(libraryType.FullName, methodName);

        var descriptors = method.Invoke(null, null) as IEnumerable;
        if (descriptors == null)
            throw new InvalidOperationException(methodName + " did not return an enumerable catalog.");

        foreach (var descriptor in descriptors)
            ExportDescriptorTree(descriptor, defaultKind, catalog);
    }

    private static void ExportDescriptorTree(object descriptor, string defaultKind, Catalog catalog)
    {
        if (descriptor == null)
            return;

        try
        {
            catalog.entries.Add(ExportDescriptor(descriptor, defaultKind));
        }
        catch (Exception exception)
        {
            catalog.errors.Add(defaultKind + " descriptor: " + exception);
        }

        var subVariants = ReadProperty(descriptor, "subVariantDescriptors") as IEnumerable;
        if (subVariants == null)
            return;

        foreach (var subVariant in subVariants)
            ExportDescriptorTree(subVariant, defaultKind, catalog);
    }

    private static Entry ExportDescriptor(object descriptor, string defaultKind)
    {
        var model = ReadProperty(descriptor, "unTypedModel");
        var modelType = ReadProperty(descriptor, "modelType") as Type ?? model?.GetType();
        var label = Convert.ToString(ReadProperty(descriptor, "name"), CultureInfo.InvariantCulture) ?? string.Empty;
        var category =
            Convert.ToString(ReadProperty(descriptor, "category"), CultureInfo.InvariantCulture) ?? string.Empty;

        var entry = new Entry { kind = defaultKind == "Context" && label.StartsWith("Output", StringComparison.Ordinal)
                                           ? "Output"
                                           : defaultKind,
                                label = label,
                                category = category,
                                modelType = modelType?.FullName ?? string.Empty,
                                sourcePath = SourcePath(model),
                                documentationUrl = DocumentationUrl(descriptor),
                                experimental = ReadExperimental(descriptor),
                                synonyms = ReadStrings(ReadProperty(descriptor, "synonyms")),
                                settings = ReadSettings(model) };

        return entry;
    }

    private static List<Setting> ReadSettings(object model)
    {
        var result = new List<Setting>();
        if (model == null)
            return result;

        var method = model.GetType().GetMethod(
            "GetSettings", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null,
            new[] { typeof(bool), FindType("UnityEditor.VFX.VFXSettingAttribute+VisibleFlags") }, null);
        if (method == null)
            return result;

        var flagsType = method.GetParameters()[1].ParameterType;
        var allFlags = Enum.ToObject(flagsType, -1);
        var values = method.Invoke(model, new[] { (object) true, allFlags }) as IEnumerable;
        if (values == null)
            return result;

        foreach (var value in values)
        {
            var field = ReadField(value, "field") as FieldInfo;
            if (field == null)
                continue;

            var currentValue = ReadProperty(value, "value");
            var setting = new Setting {
                name = field.Name,
                label = ObjectNames.NicifyVariableName(field.Name),
                type = FriendlyTypeName(field.FieldType),
                defaultValue = StableValue(currentValue),
                visibility =
                    Convert.ToString(ReadProperty(value, "visibility"), CultureInfo.InvariantCulture) ?? string.Empty,
                tooltip = ReadTooltip(field),
                enumValues = field.FieldType.IsEnum ? Enum.GetNames(field.FieldType).ToList() : new List<string>(),
                annotations = field.GetCustomAttributes(false)
                                  .Select(attribute => attribute.GetType().FullName)
                                  .Where(name => !string.IsNullOrEmpty(name))
                                  .OrderBy(name => name, StringComparer.Ordinal)
                                  .ToList()
            };
            result.Add(setting);
        }

        return result.OrderBy(setting => setting.name, StringComparer.Ordinal).ToList();
    }

    private static string DocumentationUrl(object descriptor)
    {
        var variant = ReadProperty(descriptor, "variant");
        var method = variant?.GetType().GetMethod("GetDocumentationLink",
                                                  BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
        return Convert.ToString(method?.Invoke(variant, null), CultureInfo.InvariantCulture) ?? string.Empty;
    }

    private static bool ReadExperimental(object descriptor)
    {
        var info = ReadProperty(descriptor, "infoAttribute");
        var value = ReadProperty(info, "experimental");
        return value is bool experimental && experimental;
    }

    private static string SourcePath(object model)
    {
        if (!(model is ScriptableObject scriptableObject))
            return string.Empty;

        var script = MonoScript.FromScriptableObject(scriptableObject);
        return script == null ? string.Empty : AssetDatabase.GetAssetPath(script);
    }

    private static string ReadTooltip(FieldInfo field)
    {
        var tooltip = field.GetCustomAttributes(typeof(TooltipAttribute), true).FirstOrDefault() as TooltipAttribute;
        return tooltip?.tooltip ?? string.Empty;
    }

    private static string FriendlyTypeName(Type type)
    {
        if (type == null)
            return string.Empty;
        if (!type.IsGenericType)
            return type.Name;
        var genericName = type.Name.Split('`')[0];
        return genericName + "<" + string.Join(",", type.GetGenericArguments().Select(FriendlyTypeName)) + ">";
    }

    private static string StableValue(object value)
    {
        if (value == null)
            return "null";
        if (value is bool boolean)
            return boolean ? "true" : "false";
        if (value is IFormattable formattable)
            return formattable.ToString(null, CultureInfo.InvariantCulture);
        if (value.GetType().IsEnum)
            return value.ToString();
        if (value is UnityEngine.Object unityObject)
            return AssetDatabase.GetAssetPath(unityObject);
        return value.ToString();
    }

    private static List<string> ReadStrings(object value)
    {
        if (!(value is IEnumerable enumerable))
            return new List<string>();
        return enumerable.Cast<object>()
            .Select(item => Convert.ToString(item, CultureInfo.InvariantCulture))
            .Where(item => !string.IsNullOrEmpty(item))
            .Distinct(StringComparer.Ordinal)
            .OrderBy(item => item, StringComparer.Ordinal)
            .ToList();
    }

    private static object ReadProperty(object instance, string name)
    {
        if (instance == null)
            return null;
        var property =
            instance.GetType().GetProperty(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
        return property?.GetValue(instance);
    }

    private static object ReadField(object instance, string name)
    {
        if (instance == null)
            return null;
        var field =
            instance.GetType().GetField(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
        return field?.GetValue(instance);
    }

    private static Type FindType(string fullName)
    {
        return AppDomain.CurrentDomain.GetAssemblies()
            .Select(assembly => assembly.GetType(fullName, false))
            .FirstOrDefault(type => type != null);
    }
}
