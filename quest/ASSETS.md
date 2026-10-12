# The city lab's assets

`quest/tools/fetch_assets.py` downloads these and checks each file against
the SHA-256 pinned in `quest/assets.lock.json`. They are not in git; the APK
carries them under `assets/city/`.

## Photo materials: ambientCG, CC0

[ambientCG](https://ambientcg.com) by Lennart Demes, released under
[CC0 1.0](https://docs.ambientcg.com/license/) (public domain). The 1K JPG set
of each; the colour, normal (OpenGL), roughness and metalness maps are kept.

| Material (city.hpp) | Asset | Used for |
|---|---|---|
| BrickWindows | [Facade018A](https://ambientcg.com/view?id=Facade018A) | Prewar brick apartments |
| StonePiers | [Facade019A](https://ambientcg.com/view?id=Facade019A) | Deco and office towers |
| BrickPiers | [Facade020A](https://ambientcg.com/view?id=Facade020A) | Prewar apartments |
| OfficeGrid | [Facade006](https://ambientcg.com/view?id=Facade006) | Office slabs |
| GlassCurtain | [Facade001](https://ambientcg.com/view?id=Facade001) | Glass towers |
| GreenGlass | [Facade005](https://ambientcg.com/view?id=Facade005) | Glass towers |
| ConcreteGrid | [Facade017](https://ambientcg.com/view?id=Facade017) | Office slabs |
| RedBrick | [Bricks085](https://ambientcg.com/view?id=Bricks085) | Row houses, warehouses (windows drawn in) |
| CleanBrick | [Bricks101](https://ambientcg.com/view?id=Bricks101) | Row houses, prewar |
| DarkBrick | [Bricks094](https://ambientcg.com/view?id=Bricks094) | Row houses, warehouses, deco |
| Limestone | [Bricks066](https://ambientcg.com/view?id=Bricks066) | Deco towers, trim, cornices |
| Concrete | [Concrete046](https://ambientcg.com/view?id=Concrete046) | Trim, curbs, sea wall, painted poles |
| Asphalt | [Asphalt020L](https://ambientcg.com/view?id=Asphalt020L) | Roads and their paint |
| Sidewalk | [PavingStones136](https://ambientcg.com/view?id=PavingStones136) | Sidewalks, esplanade, plazas |
| RoofGravel | [Gravel028](https://ambientcg.com/view?id=Gravel028) | Flat roofs |
| Metal | [CorrugatedSteel009](https://ambientcg.com/view?id=CorrugatedSteel009) | Rooftop machinery, water tower steel |
| Wood | [WoodSiding008](https://ambientcg.com/view?id=WoodSiding008) | Water tanks |
| OrangeBrick | [Bricks086](https://ambientcg.com/view?id=Bricks086) | Row houses, warehouses, prewar |
| Stucco | [Concrete040](https://ambientcg.com/view?id=Concrete040) | Rendered row houses and prewar |
| TarRoof | [Asphalt033](https://ambientcg.com/view?id=Asphalt033) | Flat roofs in tar |

## Skies: Poly Haven, CC0

Pure-sky 2K HDRIs from [Poly Haven](https://polyhaven.com), [CC0](https://polyhaven.com/license).
The renderer finds the sun (or moon) in each and lights the city by it.

| Time | Sky | By |
|---|---|---|
| Day | [Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky) | Greg Zaal (sky edits: Jarod Guest) |
| Sunset | [Kloppenheim 06 (Pure Sky)](https://polyhaven.com/a/kloppenheim_06_puresky) | Greg Zaal (sky edits: Jarod Guest) |
| Night | [Kloppenheim 02 (Pure Sky)](https://polyhaven.com/a/kloppenheim_02_puresky) | Greg Zaal (sky edits: Jarod Guest) |

## Image decoding: stb_image

[stb_image.h](https://github.com/nothings/stb) by Sean Barrett, revision
`2c980bb59875b0d32144a71867fbdebb2f77cd20`, public domain (or MIT, at your
choice), fetched to `third_party/stb/`.
