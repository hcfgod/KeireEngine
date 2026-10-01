using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f009")]
public sealed class PreparedPositions : Behaviour
{
    private Job? _job;

    protected override void OnEnable() => Begin();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Begin();
    }

    private void Begin()
    {
        Release();
        Vector3 origin = Transform.Position;
        var points = new Vector3[64];
        _job = Jobs.Submit(context =>
        {
            for (int index = 0; index < points.Length; ++index)
            {
                context.CancellationToken.ThrowIfCancellationRequested();
                points[index] = origin + new Vector3(index * 0.5f, 0.0f, 0.0f);
            }
        }, new JobDescription { Name = "Prepare debug positions" });
        StartCoroutine(Publish(_job, points));
    }

    private IEnumerator Publish(Job job, Vector3[] points)
    {
        while (!job.Completion.IsCompleted)
            yield return null;
        if (job.Status == JobStatus.Succeeded && Entity.IsValid)
        {
            using (Profiler.Sample("PreparedPositions.Publish"))
            {
                for (int index = 1; index < points.Length; ++index)
                    Debug.DrawLine(points[index - 1], points[index], Color.RedColor, 2.0f);
            }
            Profiler.Counter("PreparedPositions.Count", points.Length);
        }
        else if (job.Completion.Exception is { } exception)
            Debug.LogException(exception);
        _job = null;
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();

    private void Release()
    {
        StopAllCoroutines();
        _job?.Cancel();
        _job = null;
    }
}
