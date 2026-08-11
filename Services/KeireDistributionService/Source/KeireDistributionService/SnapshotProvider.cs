using Keire.Distribution;

namespace Keire.Distribution.Service;

public sealed record SnapshotProviderState(
    SnapshotIndex? Current,
    string? LastRefreshError,
    DateTimeOffset LastRefreshAttempt);

public sealed class SnapshotProvider : BackgroundService
{
    private readonly DistributionOptions m_Options;
    private readonly ILogger<SnapshotProvider> m_Logger;
    private SnapshotProviderState m_State = new(null, "No validated snapshot has been loaded.", DateTimeOffset.MinValue);
    private string? m_LastRejectedSnapshotId;
    private DateTimeOffset m_NextIntegrityValidation = DateTimeOffset.MinValue;

    public SnapshotProvider(DistributionOptions options, ILogger<SnapshotProvider> logger)
    {
        m_Options = options;
        m_Logger = logger;
    }

    public SnapshotProviderState State => Volatile.Read(ref m_State);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            await RefreshAsync(stoppingToken);
            try
            {
                await Task.Delay(m_Options.SnapshotPollInterval, stoppingToken);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
        }
    }

    private async Task RefreshAsync(CancellationToken cancellationToken)
    {
        DateTimeOffset attempt = DateTimeOffset.UtcNow;
        SnapshotProviderState previous = State;
        string? snapshotId = null;
        bool revalidatingActiveSnapshot = false;
        try
        {
            snapshotId = SnapshotValidator.ReadCurrentSnapshotId(m_Options.StorageRoot);
            revalidatingActiveSnapshot = string.Equals(previous.Current?.SnapshotId, snapshotId, StringComparison.Ordinal);
            if (revalidatingActiveSnapshot && attempt < m_NextIntegrityValidation)
            {
                if (previous.LastRefreshError is not null)
                {
                    Volatile.Write(ref m_State, previous with { LastRefreshError = null, LastRefreshAttempt = attempt });
                }

                m_LastRejectedSnapshotId = null;
                return;
            }

            if (string.Equals(m_LastRejectedSnapshotId, snapshotId, StringComparison.Ordinal))
            {
                return;
            }

            SnapshotIndex candidate = await Task.Run(
                () => SnapshotValidator.ValidateSnapshotDirectory(
                    Path.Combine(m_Options.StorageRoot, "snapshots", snapshotId),
                    snapshotId,
                    m_Options.Validation,
                    cancellationToken),
                cancellationToken);
            string currentAfterValidation = SnapshotValidator.ReadCurrentSnapshotId(m_Options.StorageRoot);
            if (!string.Equals(snapshotId, currentAfterValidation, StringComparison.Ordinal))
            {
                return;
            }

            Volatile.Write(ref m_State, new SnapshotProviderState(candidate, null, attempt));
            m_LastRejectedSnapshotId = null;
            m_NextIntegrityValidation = attempt + m_Options.SnapshotIntegrityPollInterval;
            if (revalidatingActiveSnapshot)
            {
                m_Logger.LogInformation("Revalidated active distribution snapshot {SnapshotId}", candidate.SnapshotId);
            }
            else
            {
                m_Logger.LogInformation(
                    "Activated validated distribution snapshot {SnapshotId} created at {CreatedAt}",
                    candidate.SnapshotId,
                    candidate.CreatedAt);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            string message = exception.Message;
            SnapshotIndex? retained = revalidatingActiveSnapshot ? null : previous.Current;
            Volatile.Write(ref m_State, new SnapshotProviderState(retained, message, attempt));
            try
            {
                m_LastRejectedSnapshotId = SnapshotValidator.ReadCurrentSnapshotId(m_Options.StorageRoot);
            }
            catch (Exception)
            {
                m_LastRejectedSnapshotId = null;
            }

            if (!string.Equals(previous.LastRefreshError, message, StringComparison.Ordinal))
            {
                m_Logger.LogError(
                    exception,
                    revalidatingActiveSnapshot
                        ? "Active distribution snapshot {SnapshotId} failed integrity revalidation and was withdrawn"
                        : "Rejected distribution snapshot refresh; continuing with snapshot {SnapshotId}",
                    revalidatingActiveSnapshot ? snapshotId : previous.Current?.SnapshotId ?? "none");
            }
        }
    }
}
