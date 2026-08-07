using Keire.Distribution;

namespace Keire.Distribution.Publisher;

public sealed class SigningPolicy
{
    public long MinimumSequence { get; init; } = 1;

    public TimeSpan MinimumRemainingValidity { get; init; } = TimeSpan.FromHours(24);

    public DateTimeOffset Now { get; init; } = DateTimeOffset.UtcNow;

    public void Validate(SignedDocumentIdentity identity, string documentName)
    {
        ArgumentNullException.ThrowIfNull(identity);
        if (MinimumSequence < 1)
        {
            throw new InvalidDataException("The minimum signed-document sequence must be at least one.");
        }

        if (MinimumRemainingValidity < TimeSpan.Zero || MinimumRemainingValidity > TimeSpan.FromDays(3650))
        {
            throw new InvalidDataException("The minimum signature validity must be between zero and 3650 days.");
        }

        if (Now == default || Now.Offset != TimeSpan.Zero)
        {
            throw new InvalidDataException("The signing policy clock must be expressed in UTC.");
        }

        if (identity.Sequence < MinimumSequence)
        {
            throw new InvalidDataException(
                $"Distribution document sequence is below the required minimum: '{documentName}'.");
        }

        if (identity.ExpiresAt <= Now)
        {
            throw new InvalidDataException($"Distribution document has expired: '{documentName}'.");
        }

        if (identity.ExpiresAt - Now < MinimumRemainingValidity)
        {
            throw new InvalidDataException(
                $"Distribution document expires before the required validity window: '{documentName}'.");
        }
    }
}
