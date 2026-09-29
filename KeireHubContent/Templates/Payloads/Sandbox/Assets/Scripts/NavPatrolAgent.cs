using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000020")]
[ExecutionOrder(20)]
public sealed class NavPatrolAgent : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000021")]
    private Vector3 _firstTarget = new(-6.0f, 0.0f, 4.0f);

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000022")]
    private Vector3 _secondTarget = new(6.0f, 0.0f, -4.0f);

    private CancellationTokenSource? _pathCancellation;

    protected override void Start()
    {
        _pathCancellation = new CancellationTokenSource();
        _ = PreviewPatrolPath(_pathCancellation.Token);
    }

    protected override void OnDisable()
    {
        _pathCancellation?.Cancel();
        _pathCancellation?.Dispose();
        _pathCancellation = null;
    }

    private async Task PreviewPatrolPath(CancellationToken cancellation)
    {
        var path = await Navigation.FindPathAsync(_firstTarget, _secondTarget, cancellation: cancellation);
        for (var index = 1; index < path.Points.Count; ++index)
            Debug.DrawLine(path.Points[index - 1], path.Points[index], new Color(0.1f, 0.9f, 0.4f), 5.0f);
    }
}
