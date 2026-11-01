# Earth polar source correction

The January NASA image used by this sample contains a bright border defect at its northeast corner. At its first row, 96 pixels near the right edge exceed RGB mean 100; the same-latitude ocean is approximately RGB `(2, 5, 20)`. The bright edge narrows over subsequent rows. Equirectangular mapping projects this border into a radial wedge at the north-pole launch site. The imported cap's 65,280 triangles were inspected: all UVs remain within their expected domain and no triangle crosses an unintended longitude discontinuity.

`Author-EarthPoleCorrection.ps1` creates a separate lossless PNG derivative and changes only the Earth material's texture reference. It preserves the original NASA JPEG, its attribution, and a backup of the previous material and metadata. The correction is locked to SHA-256 `99F5FAAD74EFE985FBF1714C8BE7296CA9999759A1215B65F99B7F1DF278DDE5`; other images are rejected. Only the northeast 108-by-90-pixel ocean patch is replaced from an unaffected pixel at the same latitude. This is a sample source repair, not a global rendering change or geographically accurate terrain generation.

Close the editor before authoring, then run from the repository root:

```powershell
./Samples/PlanetaryExplorer/Test-EarthPoleCorrection.ps1
./Samples/PlanetaryExplorer/Author-EarthPoleCorrection.ps1 -Project D:/Projects/KeireValidation/PlanetaryExplorer
```

Reimport the project after authoring. The script refuses to overwrite an existing correction or backup. The isolated test checks every corrected pixel, sampled pixels throughout the unaffected image, unchanged original file hash, the new material reference, and repeated-authoring rejection. Visual acceptance still needs the same camera angle after the new texture imports. This repair does not replace the surface-spaceport and terrain work.
