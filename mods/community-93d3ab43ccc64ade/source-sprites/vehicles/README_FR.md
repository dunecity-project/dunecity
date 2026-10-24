# Références des véhicules Tornie

Ces PNG sont des gabarits de travail. Ils ne sont pas chargés par le jeu et ne
sont pas inclus dans `Tornie.PAK`.

La géométrie propre des châssis et des canons vanilla est extraite directement
de `UNITS2.SHP` avec `scripts/extract-unit-sprite.py`. Les bandes correspondantes
sont conservées dans `../base/`. Les anciennes bandes colorées compactes servent
uniquement de guides de couleur : elles ne sont pas des feuilles de cinq cases
alignées et ne doivent pas être importées directement dans le moteur.

## Format

- Taille habituelle d'une feuille complète : `128x16` pixels.
- Le Harvestank suit le Harvester original : `192x24`, soit huit images de
  `24x24`. Il ne faut pas le réduire en `16x16`.
- Les huit images sont placées horizontalement.
- Ordre identique à celui utilisé par le moteur DuneCity.
- Palette indexée Dune 2.
- L'indice `0` est transparent.

Dans GIMP, conserver le mode indexé, la taille exacte et l'ordre des huit
images. Ne pas déplacer une orientation dans la case voisine.

## Fichiers complets

- `RocketTrike_reference.png`
- `SonicTrike_reference.png`
- `Harvestank_reference.png`
- `Deviator_reference.png`
- `FlameTank_reference.png`
- `EliteLauncher_reference.png`
- `EliteSiegeTank_reference.png`
- `RebelSonicTank_reference.png`

Les fichiers complets montrent le châssis et le canon déjà superposés. Ils sont
faits pour servir de référence visuelle pendant la retouche.

`Harvestank_compact_color_guide.png` conserve l'ancien dessin Tornie en
`128x16`. Il sert uniquement de guide de couleurs et de formes. Ses cases ne
correspondent pas aux dimensions natives du Harvester et ce fichier ne doit
pas être importé directement dans le moteur.

## Canons séparés

- `Deviator_turret_reference.png`
- `FlameTank_turret_reference.png`
- `EliteLauncher_turret_reference.png`
- `EliteSiegeTank_turret_reference.png`
- `RebelSonicTank_turret_reference.png`

Le moteur dessine normalement ces canons par-dessus le châssis. Les bandes de
canons vanilla conservent leur taille native (`80x10` pour les canons Launcher
et Sonic); celle de l'Elite Siege Tank reste en cases de `16x16`. Le fichier
complet associé montre le placement exact du canon sur le châssis.

Une fois les retouches approuvées, les feuilles séparées seront donc les
fichiers à importer dans le jeu. Ne pas agrandir une bande native : conserver
ses huit cases et sa taille exacte.

## Régénération

Exécuter `tools/generate_tornie_vehicle_references.py` avec Python et Pillow.
Le script reconstruit les huit orientations à partir des bandes SHP extraites
et des feuilles indexées validées de `mods/Tornie/data`.
