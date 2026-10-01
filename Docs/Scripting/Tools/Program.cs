using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

if (args.Length is < 1 or > 2 || (args.Length == 2 && args[1] != "--check"))
    throw new ArgumentException("Usage: Keire.ApiReference <repository-root> [--check]");

string root = Path.GetFullPath(args[0]);
foreach (string assembly in new[] { "KeireManaged", "KeireEditorManaged" })
{
    var types = new SortedDictionary<string, List<MemberDeclarationSyntax>>(StringComparer.Ordinal);
    foreach (string path in Directory.GetFiles(Path.Combine(root, assembly), "*.cs").Order(StringComparer.Ordinal))
    {
        var tree = CSharpSyntaxTree.ParseText(File.ReadAllText(path), path: path);
        foreach (MemberDeclarationSyntax declaration in tree.GetRoot().DescendantNodes().OfType<MemberDeclarationSyntax>())
        {
            if (declaration is not BaseTypeDeclarationSyntax and not DelegateDeclarationSyntax || !Visible(declaration))
                continue;
            string name = string.Join(".", declaration.Ancestors().Reverse().OfType<BaseNamespaceDeclarationSyntax>()
                .Select(value => value.Name.ToString()).Concat(declaration.AncestorsAndSelf().Reverse()
                .Where(value => value is BaseTypeDeclarationSyntax or DelegateDeclarationSyntax).Select(TypeName)));
            if (!types.TryGetValue(name, out var declarations))
                types.Add(name, declarations = new());
            declarations.Add(declaration);
        }
    }

    bool editor = assembly == "KeireEditorManaged";
    string title = editor ? "Editor API Member Reference" : "Runtime API Member Reference";
    var output = new StringBuilder($"# {title}\n\n");
    output.Append("[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Cookbook](Cookbook.md) · " +
        "[API index](ApiIndex.md)\n\n");
    output.Append(editor
        ? "These declarations belong to `Keire.Editor.Managed.dll`. Use them in an Editor-classified assembly. " +
          "Read [Managed Extensibility](ManagedExtensibility.md) for registration, transactions, and current integration limits.\n\n"
        : "These declarations belong to `Keire.Managed.dll`. Read the system guides for timing, ownership, " +
          "validation, and examples; a public declaration alone does not guarantee a host supplies that service. " +
          "[Capability status](ManagedApiMatrix.md) distinguishes supported workflows from remaining work.\n\n");
    output.Append("## How To Read This Reference\n\n" +
        "Search for a type or member name, or select a type below. Each entry links to its defining source. " +
        "Signatures retain parameter names, defaults, generic constraints, attributes, and accessor visibility. " +
        "Method bodies and field/property initializers are omitted. Base types supply inherited members; " +
        "follow their entries when a method is not declared on a derived type. Partial declarations are combined " +
        "under one entry. Positional record parameters also declare record properties; compiler-generated " +
        "equality, deconstruction, and implicit constructors are not repeated.\n\n" +
        "Protected members are subclass extension points. `internal` and `private` members are excluded. " +
        "Public bridge interfaces, identity records, serialization codecs, and `Runtime*` methods/fields are " +
        "advanced host/integration surfaces: ordinary scripts use the object and component APIs instead of " +
        "installing bridges, invoking runtime lifecycle dispatch, or editing diagnostic transport fields.\n\n" +
        "This is a generated declaration catalog, not a replacement for the workflow guides. " +
        "Regenerate or check it using [the documented commands](ApiIndex.md#maintaining-the-member-reference).\n\n" +
        "## Type Directory\n\n");
    foreach (string name in types.Keys)
        output.AppendLine($"- [`{name}`](#{Anchor(Heading(name))})");

    foreach (var (name, declarations) in types)
    {
        output.Append($"\n## {Heading(name)}\n\nSources: ");
        output.AppendJoin(", ", declarations.Select(value => value.SyntaxTree.FilePath).Distinct()
            .Select(path => $"[{Path.GetFileName(path)}](../../{assembly}/{Path.GetFileName(path)})"));
        output.Append("\n\n```csharp\n");
        foreach (MemberDeclarationSyntax declaration in declarations)
        {
            if (declaration is EnumDeclarationSyntax or DelegateDeclarationSyntax)
                output.AppendLine(declaration.WithoutTrivia().NormalizeWhitespace("    ", "\n").ToFullString());
            else if (declaration is TypeDeclarationSyntax type)
            {
                string header = type.WithMembers(default).WithOpenBraceToken(default).WithCloseBraceToken(default)
                    .WithSemicolonToken(default).WithoutTrivia().NormalizeWhitespace("    ", "\n").ToFullString().Trim();
                output.AppendLine(header);
                output.AppendLine("{");
                foreach (MemberDeclarationSyntax member in type.Members.Where(value =>
                    value is not BaseTypeDeclarationSyntax and not DelegateDeclarationSyntax && Visible(value)))
                {
                    string signature = Signature(member).WithoutTrivia().NormalizeWhitespace("    ", "\n").ToFullString();
                    foreach (string line in signature.Split('\n'))
                        output.AppendLine("    " + line);
                }
                output.AppendLine("}");
            }
        }
        output.Append("```\n");
    }
    string destination = Path.Combine(root, "Docs", "Scripting", editor ? "EditorReference.md" : "RuntimeReference.md");
    string content = output.ToString().Replace("\r\n", "\n");
    if (args.Length == 2)
    {
        if (!File.Exists(destination) || File.ReadAllText(destination).Replace("\r\n", "\n") != content)
            throw new InvalidOperationException($"Stale API reference: {destination}");
    }
    else
        File.WriteAllText(destination, content, new UTF8Encoding(false));
    Console.WriteLine($"{title}: {types.Count} types ({(args.Length == 2 ? "current" : "written")}).");
}

string cookbook = Path.Combine(root, "Docs", "Scripting", "Cookbook.md");
if (File.Exists(cookbook))
{
    string original = File.ReadAllText(cookbook).Replace("\r\n", "\n");
    string updated = System.Text.RegularExpressions.Regex.Replace(original,
        @"<!-- example:([A-Za-z0-9]+\.cs) -->[\s\S]*?<!-- /example -->", match =>
        {
            string file = match.Groups[1].Value;
            string source = File.ReadAllText(Path.Combine(root, "Docs", "Scripting", "Examples", file))
                .Replace("\r\n", "\n").TrimEnd();
            return $"<!-- example:{file} -->\n\n```csharp\n{source}\n```\n\n<!-- /example -->";
        });
    if (args.Length == 2 && original != updated)
        throw new InvalidOperationException("Cookbook examples differ from their compile-checked sources.");
    if (args.Length == 1)
        File.WriteAllText(cookbook, updated, new UTF8Encoding(false));
    Console.WriteLine("Cookbook source excerpts synchronized.");
}

static string TypeName(SyntaxNode node) => node switch
{
    TypeDeclarationSyntax type => type.Identifier.Text + type.TypeParameterList,
    EnumDeclarationSyntax value => value.Identifier.Text,
    DelegateDeclarationSyntax value => value.Identifier.Text + value.TypeParameterList,
    _ => throw new ArgumentException("Expected a type declaration.")
};

static string Heading(string name) => System.Text.RegularExpressions.Regex.Replace(
    name.Replace("<", " of ").Replace(">", "").Replace(",", " and "), @"\s+", " ");
static string Anchor(string name) => string.Concat(name.ToLowerInvariant().Where(value =>
    char.IsLetterOrDigit(value) || value == ' ')).Replace(" ", "-");

static bool Visible(MemberDeclarationSyntax member)
{
    bool own = member.Modifiers.Any(SyntaxKind.PublicKeyword) ||
        (member.Modifiers.Any(SyntaxKind.ProtectedKeyword) && !member.Modifiers.Any(SyntaxKind.PrivateKeyword)) ||
        (member.Parent is InterfaceDeclarationSyntax && !member.Modifiers.Any(SyntaxKind.PrivateKeyword) &&
         !member.Modifiers.Any(SyntaxKind.InternalKeyword));
    return own && member.Ancestors().OfType<BaseTypeDeclarationSyntax>().All(parent => Visible(parent));
}

static AccessorListSyntax? Accessors(AccessorListSyntax? list) => list?.WithAccessors(SyntaxFactory.List(
    list.Accessors.Select(value => value.WithBody(null).WithExpressionBody(null)
        .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken)))));

static MemberDeclarationSyntax Signature(MemberDeclarationSyntax member) => member switch
{
    MethodDeclarationSyntax value => value.WithBody(null).WithExpressionBody(null)
        .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken)),
    ConstructorDeclarationSyntax value => value.WithBody(null).WithExpressionBody(null).WithInitializer(null)
        .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken)),
    OperatorDeclarationSyntax value => value.WithBody(null).WithExpressionBody(null)
        .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken)),
    ConversionOperatorDeclarationSyntax value => value.WithBody(null).WithExpressionBody(null)
        .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken)),
    PropertyDeclarationSyntax value => value.WithInitializer(null).WithExpressionBody(null).WithSemicolonToken(default)
        .WithAccessorList(Accessors(value.AccessorList) ?? SyntaxFactory.AccessorList(SyntaxFactory.SingletonList(
            SyntaxFactory.AccessorDeclaration(SyntaxKind.GetAccessorDeclaration)
                .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken))))),
    IndexerDeclarationSyntax value => value.WithExpressionBody(null).WithSemicolonToken(default)
        .WithAccessorList(Accessors(value.AccessorList) ?? SyntaxFactory.AccessorList(SyntaxFactory.SingletonList(
            SyntaxFactory.AccessorDeclaration(SyntaxKind.GetAccessorDeclaration)
                .WithSemicolonToken(SyntaxFactory.Token(SyntaxKind.SemicolonToken))))),
    EventDeclarationSyntax value => value.WithAccessorList(Accessors(value.AccessorList)),
    FieldDeclarationSyntax value => value.WithDeclaration(value.Declaration.WithVariables(SyntaxFactory.SeparatedList(
        value.Declaration.Variables.Select(variable => variable.WithInitializer(null))))),
    _ => member
};
