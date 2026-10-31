#pragma once

// Distances are kilometres. Test opaque geometry independently of optical-medium enablement.
// Closest approach avoids subtracting two astronomical squared distances.
bool AtmosphereSunOccluded(float originRadius, float along, float closestSquared, float radius)
{
    if (originRadius < radius)
    {
        return true;
    }
    if (along >= 0.0F)
    {
        return false;
    }
    const float radiusSquared = radius * radius;
    const float discriminant = mad(radius, radius, -closestSquared);
    const float roundoff = 4.0F * 1.192092896e-7F * max(radiusSquared, closestSquared);
    return discriminant >= -roundoff;
}
