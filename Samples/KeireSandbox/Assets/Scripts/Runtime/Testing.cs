using Keire;

namespace KeireSandbox;

[StableComponentId("f2354706-543d-4ac6-87e1-6d0c37bee1c8")]
public sealed class Testing : Behaviour
{
    [SerializeField] private bool canFire;
    [SerializeField] private bool canReload;

    [SerializeField] private int ammoInClip;
    [SerializeField] private int clipSize;
    [SerializeField] private int amountOfClips;
    [SerializeField] private int reserveAmmo;

    private const int startingClipSize = 17;
    private const int startingClips = 2;
    private const int reserveAmmoMultiplier = 5;

    protected override void Start()
    {
        clipSize = startingClipSize;
        amountOfClips = startingClips;
        reserveAmmo = clipSize * reserveAmmoMultiplier;

        IncreaseAmmo(clipSize);

        if (!IsEmpty)
        {
            canFire = true;
        }
    }

    protected override void Update()
    {
        if (IsEmpty)
        {
            canFire = false;
            canReload = true;
        }
        else
        {
            canFire = true;
            canReload = false;
        }

        if (canFire && Input.Button("Fire"))
        {
            Debug.Log("Firing");

            DecreaseAmmo(1);
        }

        if(canReload && Input.Button("Reload"))
        {
            Reload();
        }
    }

    public void DecreaseAmmo(int amount)
    {
        ammoInClip -= amount;
    }

    public void IncreaseAmmo(int amount)
    {
        ammoInClip += amount;
    }

    public void SetAmmo(int amount)
    {
        ammoInClip = amount;
    }

    public void Reload()
    {
        amountOfClips -= 1;
        ammoInClip = clipSize;
        reserveAmmo -= clipSize;
    }

    public bool IsEmpty => ammoInClip > 0 && amountOfClips > 0;
}
