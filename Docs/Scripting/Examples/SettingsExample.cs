using Keire;

namespace ScriptingExamples;

public static class SettingsExample
{
    public static void Apply()
    {
        float volume = PlayerPreferences.GetFloat("audio.master", 0.8f);
        PlayerPreferences.SetFloat("audio.master", Math.Clamp(volume, 0.0f, 1.0f));
        PlayerPreferences.SetBool("accessibility.subtitles", true);
        PlayerPreferences.Save();
        if (Screen.IsPresentModeSupported(PresentMode.VSync))
            Screen.TrySetPresentMode(PresentMode.VSync);
        if (!Screen.TrySetResolution(1920, 1080, FullscreenMode.Windowed))
            Debug.Warn("Requested resolution is unavailable.");
    }
}
