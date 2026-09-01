internal static class ManagedSerializationV4Tests
{
    internal static void Run()
    {
        var source = new ManagedSerializationV4Probe
        {
            Temperature = new Temperature(294.5),
        };
        string state = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
        Assert(state.Contains("\"Version\":4", StringComparison.Ordinal) &&
                   state.Contains("\"$custom\":\"73616e64-626f-4078-8000-00000000f401\"",
                                  StringComparison.Ordinal) &&
                   state.Contains("\"version\":2", StringComparison.Ordinal),
               "Canonical v4 state must tag custom values with their stable codec ID and version.");
        Assert(source.CallbackValue == 0,
               "OnBeforeSerialize must run against a staged candidate without mutating live state.");

        var target = new ManagedSerializationV4Probe { Temperature = new Temperature(100.0) };
        _ = Keire.ManagedStateSerializer.Restore(target, state, false);
        Assert(target.Temperature == new Temperature(294.5) && target.CallbackValue == 11,
               "Custom converter round trips and serialization callbacks must commit one staged graph.");

        System.Text.Json.Nodes.JsonObject migrated =
            System.Text.Json.Nodes.JsonNode.Parse(state)!.AsObject();
        System.Text.Json.Nodes.JsonObject temperatureField = migrated["Fields"]!.AsArray()
            .Select(node => node!.AsObject())
            .Single(field => field["Name"]!.GetValue<string>() == nameof(ManagedSerializationV4Probe.Temperature));
        System.Text.Json.Nodes.JsonObject customValue = temperatureField["Value"]!.AsObject();
        customValue["version"] = 1;
        customValue["payload"] = 301.25;
        _ = Keire.ManagedStateSerializer.Restore(target, migrated.ToJsonString(), false);
        Assert(target.Temperature == new Temperature(301.25),
               "A unique contiguous migration chain must upgrade old custom payloads before construction.");

        System.Text.Json.Nodes.JsonObject failed =
            System.Text.Json.Nodes.JsonNode.Parse(state)!.AsObject();
        System.Text.Json.Nodes.JsonObject failField = failed["Fields"]!.AsArray()
            .Select(node => node!.AsObject())
            .Single(field => field["Name"]!.GetValue<string>() == nameof(ManagedSerializationV4Probe.FailAfterDeserialize));
        failField["Value"] = true;
        Temperature previousTemperature = target.Temperature;
        int previousCallbackValue = target.CallbackValue;
        AssertThrows<Keire.ManagedSerializationException>(
            () => Keire.ManagedStateSerializer.Restore(target, failed.ToJsonString(), false),
            "A failed OnAfterDeserialize callback must reject the staged candidate.");
        Assert(target.Temperature == previousTemperature && target.CallbackValue == previousCallbackValue &&
                   !target.FailAfterDeserialize,
               "Callback failure must leave every live field unchanged.");

        AssertThrows<ArgumentOutOfRangeException>(
            () => Keire.ManagedSerializedValue.From(double.NaN),
            "Custom payload numbers must reject non-finite values.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => Keire.ManagedSerializedValue.FromList(
                Enumerable.Range(0, 4_097).Select(index => Keire.ManagedSerializedValue.From((long)index))),
            "Custom payload collections must enforce their exact item bound.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void AssertThrows<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        throw new InvalidOperationException(message);
    }
}

internal readonly record struct Temperature(double Kelvin);

[Keire.CustomManagedValueConverter(typeof(Temperature), "73616e64-626f-4078-8000-00000000f401", 2)]
internal sealed class TemperatureConverter : Keire.ManagedValueConverter<Temperature>
{
    public override Keire.ManagedSerializedValue Write(Temperature value, Keire.ManagedSerializationContext context) =>
        Keire.ManagedSerializedValue.FromMap(
        [
            new KeyValuePair<string, Keire.ManagedSerializedValue>(
                "kelvin", Keire.ManagedSerializedValue.From(value.Kelvin)),
        ]);

    public override Temperature Read(Keire.ManagedSerializedValue value, Keire.ManagedSerializationContext context) =>
        new(value.AsMap()["kelvin"].AsNumber());
}

[Keire.ManagedValueMigration("73616e64-626f-4078-8000-00000000f401", 1, 2)]
internal sealed class TemperatureV1ToV2Migration : Keire.IManagedValueMigration
{
    public Keire.ManagedSerializedValue Migrate(Keire.ManagedSerializedValue value,
                                                Keire.ManagedMigrationContext context) =>
        Keire.ManagedSerializedValue.FromMap(
        [
            new KeyValuePair<string, Keire.ManagedSerializedValue>("kelvin", value),
        ]);
}

[Keire.StableComponentId("73616e64-626f-4078-8000-00000000f402")]
internal sealed class ManagedSerializationV4Probe : Keire.Behaviour, Keire.ISerializationCallbackReceiver
{
    public Temperature Temperature;
    public int CallbackValue;
    public bool FailAfterDeserialize = false;

    public void OnBeforeSerialize() => ++CallbackValue;

    public void OnAfterDeserialize()
    {
        CallbackValue += 10;
        if (FailAfterDeserialize)
            throw new InvalidOperationException("Expected callback failure.");
    }
}
