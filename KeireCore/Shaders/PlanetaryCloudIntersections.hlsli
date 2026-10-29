#pragma once

// Scalar kernel is compiled directly by CPU regression tests. Altitude is supplied separately
// from radius so metre-scale layers never subtract two planet-sized squared radii.
struct PlanetaryCloudRoots
{
    float Near;
    float Far;
    bool Hit;
};

PlanetaryCloudRoots PlanetaryCloudSphereRoots(float radius, float altitude, float radialCosine, float shellHeight)
{
    PlanetaryCloudRoots roots;
    roots.Near = 0.0F;
    roots.Far = 0.0F;
    roots.Hit = false;
    if (!isfinite(radius) || !isfinite(altitude) || !isfinite(radialCosine) || !isfinite(shellHeight) ||
        radius <= 0.0F || radius + altitude <= 0.0F || radius + shellHeight <= 0.0F)
    {
        return roots;
    }
    const float b = (radius + altitude) * max(-1.0F, min(1.0F, radialCosine));
    const float c = (altitude - shellHeight) * (2.0F * radius + altitude + shellHeight);
    const float cosine = max(-1.0F, min(1.0F, radialCosine));
    const float observerRadius = radius + altitude;
    const float shellRadius = radius + shellHeight;
    const float perpendicular = observerRadius * sqrt(max(0.0F, 1.0F - cosine * cosine));
    const float discriminant = observerRadius > 2.0F * shellRadius ?
        (shellRadius - perpendicular) * (shellRadius + perpendicular) : b * b - c;
    if (discriminant < 0.0F || !isfinite(discriminant))
    {
        return roots;
    }
    const float root = sqrt(discriminant);
    const float q = -b - (b >= 0.0F ? root : -root);
    const float other = observerRadius > 2.0F * shellRadius ? -b + (b >= 0.0F ? root : -root) :
        (q != 0.0F ? c / q : -b);
    roots.Near = min(q, other);
    roots.Far = max(q, other);
    roots.Hit = true;
    return roots;
}

struct PlanetaryCloudIntervals
{
    float FirstEntry;
    float FirstExit;
    float SecondEntry;
    float SecondExit;
};

PlanetaryCloudIntervals IntersectPlanetaryCloudShell(float radius, float altitude, float radialCosine,
    float baseHeight, float thickness, float maximumDistance)
{
    PlanetaryCloudIntervals result;
    result.FirstEntry = 0.0F;
    result.FirstExit = 0.0F;
    result.SecondEntry = 0.0F;
    result.SecondExit = 0.0F;
    if (!isfinite(maximumDistance) || maximumDistance <= 0.0F || !isfinite(thickness) || thickness <= 0.0F)
    {
        return result;
    }
    const PlanetaryCloudRoots outer = PlanetaryCloudSphereRoots(radius, altitude, radialCosine, baseHeight + thickness);
    if (!outer.Hit)
    {
        return result;
    }
    float entry = max(0.0F, outer.Near);
    float exitDistance = min(maximumDistance, outer.Far);
    const PlanetaryCloudRoots ground = PlanetaryCloudSphereRoots(radius, altitude, radialCosine, 0.0F);
    if (altitude < 0.0F)
    {
        return result;
    }
    if (ground.Hit && ground.Far > 0.0F && ground.Near >= 0.0F)
    {
        exitDistance = min(exitDistance, ground.Near);
    }
    if (exitDistance <= entry)
    {
        return result;
    }
    const PlanetaryCloudRoots inner = PlanetaryCloudSphereRoots(radius, altitude, radialCosine, baseHeight);
    if (!inner.Hit || inner.Far <= entry || inner.Near >= exitDistance)
    {
        result.FirstEntry = entry;
        result.FirstExit = exitDistance;
        return result;
    }
    result.FirstEntry = entry;
    result.FirstExit = max(entry, min(exitDistance, inner.Near));
    result.SecondEntry = max(entry, inner.Far);
    result.SecondExit = max(result.SecondEntry, exitDistance);
    if (result.FirstExit <= result.FirstEntry)
    {
        result.FirstEntry = result.SecondEntry;
        result.FirstExit = result.SecondExit;
        result.SecondEntry = 0.0F;
        result.SecondExit = 0.0F;
    }
    return result;
}

PlanetaryCloudIntervals LimitPlanetaryCloudTravel(PlanetaryCloudIntervals intervals, float travelBudget)
{
    const float firstLength = min(max(intervals.FirstExit - intervals.FirstEntry, 0.0F), max(travelBudget, 0.0F));
    intervals.FirstExit = intervals.FirstEntry + firstLength;
    const float secondLength = min(max(intervals.SecondExit - intervals.SecondEntry, 0.0F),
        max(travelBudget - firstLength, 0.0F));
    intervals.SecondExit = intervals.SecondEntry + secondLength;
    return intervals;
}

struct PlanetaryCloudSampleInterval
{
    float Entry;
    float Exit;
};

// Allocate a shared step budget to occupied intervals; the empty gap consumes no density samples.
// The caller supplies at least two steps when both intervals are nonempty.
PlanetaryCloudSampleInterval PlanetaryCloudSample(PlanetaryCloudIntervals intervals, uint steps, uint index)
{
    const float firstLength = max(intervals.FirstExit - intervals.FirstEntry, 0.0F);
    const float secondLength = max(intervals.SecondExit - intervals.SecondEntry, 0.0F);
    uint firstSteps = steps;
    if (secondLength > 0.0F && steps > 1U)
    {
        firstSteps = max(1U, min(steps - 1U, uint(float(steps) * firstLength / max(firstLength + secondLength, 0.0001F))));
    }
    const bool first = index < firstSteps;
    const uint count = first ? firstSteps : steps - firstSteps;
    const uint localIndex = first ? index : index - firstSteps;
    const float start = first ? intervals.FirstEntry : intervals.SecondEntry;
    const float length = first ? firstLength : secondLength;
    PlanetaryCloudSampleInterval result;
    result.Entry = start + length * float(localIndex) / float(max(count, 1U));
    result.Exit = start + length * float(localIndex + 1U) / float(max(count, 1U));
    return result;
}
