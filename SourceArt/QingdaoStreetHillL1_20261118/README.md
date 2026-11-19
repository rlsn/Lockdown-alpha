# Qingdao street and hillside park art

This source library supplies eight reusable environment props and five ground meshes for the comparison area in `Content/Lockdown/Generated/Maps/L_Demo.umap`.

The original gameplay area is separate. Comparison-area ground and props have no collision, navigation contribution, overlap events, or gameplay identity. They are ordinary independently editable actors, visible in a cooked scene. They do not generate themselves on Play.

## Editable source

- `props/QD_Street_And_Hill_Park_8_Props.blend`: bench, heritage lamp, stone bollard, planter, cafe set, storefront canopy, shoulder wheel, and parallel bars. Each is below 2,000 triangles; all share one 512 x 512 PBR atlas.
- `ground/QD_City_Ground_And_Street_Regions.blend`: the retained terrain geometry for the three mountain regions and the Feicheng slope, plus a surface-only Parkson street overlay.
- `ground/asset-manifest.json` and `props/asset-manifest.json`: exported triangle counts, dimensions, material assignments, and texture sizes.

Ground geometry uses meters relative to the comparison origin at Unreal world `(0, 24000, 0)` centimeters. Props use a local origin at ground level. FBX import scale is 100 with legacy FBX import. FBX geometry includes the project's authored Y reflection; do not mirror it again.

## Materials and placement

Feicheng paving uses one 1024 x 1024 street-section texture set, with rough grey setts and stone edge drainage. Parkson streets retain the authored road footprints and distinguish asphalt, pale markings, and stone pedestrian margins.

The mountain ground paint follows the authored terraces. It contains no generated route graph. The large northern park terrace has continuous quiet paving; its center has no decorative subdivision or furniture. Furniture stays at the perimeter. Mountain vegetation stays on rocks or in small pockets beside them; reseated vegetation remains within the original obstacle envelopes.

`MountainGroundPaint` UVs map local XY bounds `(-20, -65)` to `(65, 80)` meters. All mountain terrain uses the same paint field, so a texture repaint is needed if its geometry is substantially rearranged. This is an art reference, with no gameplay dependency. Retaining side faces reuse `QingdaoGroundVegetationL1/Ground/Materials/M_QD_RetainingStone_1024`; that material's source is in the corresponding ground and vegetation source library.

Unreal assets are under `/Game/Lockdown/Art/QingdaoStreetHillL1`. Existing vegetation remains under `/Game/Lockdown/Art/QingdaoGroundVegetationL1/Vegetation`.

## Real-scene references

- Feicheng Road's surviving stone slope and cathedral approach: https://news.qingdaonews.com/content/2014-09/03/content_10656791_all.htm
- Feicheng Road historic shopfront context: https://news.qingdaonews.com/qingdao/2016-06/07/content_11632633_all.htm
- Parkson intersection photograph: https://img.guanhai.com.cn/a/10001/202209/9934c6638654ece74e329604d7199e7e.jpeg
- Qingdao community park exercise equipment: https://news.bandao.cn/a/1701168894701706.html
- Yandao urban hillside park reference: https://minsheng.qingdaonews.com/content/2021-07/02/content_22779119.htm

Reference photos are research material, not redistributed textures. The atlas and ground textures are newly authored stylized assets.
