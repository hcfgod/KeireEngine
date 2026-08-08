namespace Keire.Distribution.Publisher;

internal sealed class CommandLine
{
    private readonly Dictionary<string, string> m_Values;
    private readonly HashSet<string> m_Flags;

    private CommandLine(string command, Dictionary<string, string> values, HashSet<string> flags)
    {
        Command = command;
        m_Values = values;
        m_Flags = flags;
    }

    public string Command { get; }

    public static CommandLine Parse(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help")
        {
            return new CommandLine("help", new Dictionary<string, string>(), new HashSet<string>());
        }

        string command = args[0];
        Dictionary<string, string> values = new(StringComparer.Ordinal);
        HashSet<string> flags = new(StringComparer.Ordinal);
        for (int index = 1; index < args.Length; ++index)
        {
            string option = args[index];
            if (!option.StartsWith("--", StringComparison.Ordinal) || option.Length == 2)
            {
                throw new ArgumentException($"Unexpected argument '{option}'.");
            }

            if (option == "--activate")
            {
                if (!flags.Add(option))
                {
                    throw new ArgumentException($"Duplicate option '{option}'.");
                }

                continue;
            }

            if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Option '{option}' requires a value.");
            }

            if (!values.TryAdd(option, args[++index]))
            {
                throw new ArgumentException($"Duplicate option '{option}'.");
            }
        }

        return new CommandLine(command, values, flags);
    }

    public string Require(string name)
    {
        return m_Values.TryGetValue(name, out string? value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"Required option '{name}' was not provided.");
    }

    public string? Optional(string name)
    {
        return m_Values.TryGetValue(name, out string? value) && !string.IsNullOrWhiteSpace(value) ? value : null;
    }

    public bool HasFlag(string name)
    {
        return m_Flags.Contains(name);
    }

    public void RejectUnknown(params string[] allowed)
    {
        HashSet<string> accepted = allowed.ToHashSet(StringComparer.Ordinal);
        string? unknown = m_Values.Keys.Concat(m_Flags).FirstOrDefault(option => !accepted.Contains(option));
        if (unknown is not null)
        {
            throw new ArgumentException($"Unknown option '{unknown}'.");
        }
    }
}
