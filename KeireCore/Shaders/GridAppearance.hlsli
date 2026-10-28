// Fade every grid element, including axes, over the outer half of the chosen range.
float GridVisibility(const float opacity, const float fadeDistance, const float rayDistance)
{
    const float safeDistance = max(fadeDistance, 0.001F);
    const float distanceFade = 1.0F - smoothstep(safeDistance * 0.5F, safeDistance, rayDistance);
    return saturate(opacity) * distanceFade;
}
