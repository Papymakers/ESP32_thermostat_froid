# Thermostat pour congélateur / réfrigérateur V2

Contrôleur de thermostat ESP32-C6 pour congélateur/réfrigérateur, avec régulation par hystérésis, calibration de capteur, sécurités intégrées, alarme et mode booster, et pilotage local (boutons + affichage déporté) ou à distance (page web + MQTT). Conçu par [Papymakers](https://papymakers.com).

V2 du projet — nouveau PCB (ESP32-C6-DEVKITC-1-N4), nouvelles options matérielles. 

## Nouveautés par rapport à la V1 (non mise en ligne)

- Nouveau microcontrôleur : ESP32-C6-DEVKITC-1-N4 (remplace l'ESP32-C3-DevKitM-1, discontinué).
- 2 boutons physiques (SW1 −, SW2 +) avec machine à états gérant appui court, appui long individuel, et combinaison des deux.
- 3 options d'affichage déporté exclusives, sélectionnées à la compilation : OLED I2C, TM1637 en bargraphe, TM1637 en double 7 segments.
- Buzzer d'alarme (AC-1405G-LF), activable/désactivable depuis la page web ou par appui long sur SW2.
- Alarme de dépassement de consigne (marge + durée soutenue configurables), pensée pour détecter un réchauffement anormal (ex: ajout de nouveaux produits, panne, porte mal fermée).
- Mode booster : force le compresseur en continu pendant une durée définie, pour accélérer la congélation de nouveaux produits ; suspend l'alarme pendant son fonctionnement.
- Option de ventilation forcée du condenseur (2e relais), activable à la compilation selon le PCB assemblé.

## Fonctionnalités héritées de la V1

- Calibration 2 points (gain + offset) par capteur, pour corriger la non-linéarité des capteurs DS18B20 clonés.
- Détection automatique de capteur DS18B20 contrefait au démarrage.
- Détection de capteur déconnecté ou de lecture figée (conversion bloquée).
- Sécurité absolue basse température, indépendante de la consigne.
- Marche/arrêt à distance (page web, MQTT, ou combinaison SW1+SW2 maintenue 1.5s).
- Réglages persistés sur LittleFS — survivent aux coupures de courant.
- Page web temps réel (WebSocket), avec verrou d'édition pour ne pas perdre une saisie en cours.

## Matériel

| Élément | Référence | Remarque |
|---|---|---|
| Microcontrôleur | ESP32-C6-DEVKITC-1-N4 | |
| Capteurs température | 2x DS18B20 | **Utiliser des capteurs Maxim/Analog Devices authentifiés.** Les clones bon marché ont des erreurs de calibration non constantes, mal corrigées par un simple offset. Détection de clone au démarrage incluse (voir moniteur série). |
| Relais compresseur | GPIO3 | |
| Relais ventilateur (optionnel) | GPIO14 | Population et compilation conditionnelles (`FAN_PRESENT`) |
| LED témoin relais | GPIO23 | |
| Buzzer | AC-1405G-LF via SE2N7002E | GPIO22, signal carré ~2400 Hz (`tone()`/`noTone()`) |
| I2C natif (OLED / futur capteur proche) | GPIO6 (SDA) / GPIO7 (SCL) | **Ne pas déporter d'équipement I2C par câble** — bus sensible aux perturbations EMI du relais compresseur à cette distance |
| TM1637 (CLK / DIO) | GPIO21 / GPIO20 | |
| SW1 (−, long = booster) | GPIO19 | |
| SW2 (+, long = buzzer) | GPIO18 | |
| Combo SW1+SW2 (1.5s) | — | Marche/arrêt thermostat |
| DS1820 congélateur | GPIO0 | |
| DS1820 extérieur | GPIO1 | |

## Câblage capteurs DS18B20

Câblage 3 fils recommandé (alimentation séparée, pas de mode parasite) :
- VDD → 3.3V
- GND → GND
- DATA → GPIO dédié + résistance de pull-up 4.7 kΩ vers 3.3V

## Options de compilation (`platformio.ini` → `build_flags`)

| Flag | Valeurs | Effet |
|---|---|---|
| `DISPLAY_TYPE` | `DISPLAY_NONE` (défaut), `DISPLAY_OLED`, `DISPLAY_TM1637_BAR`, `DISPLAY_TM1637_7SEG` | Sélectionne le pilote d'affichage déporté |
| `LEDS_PER_ARM` | entier, défaut 4 | Nombre de LED de chaque côté du centre, mode bargraphe |
| `FAN_PRESENT` | `0` (défaut) / `1` | Active le 2e relais de ventilation |

Une seule option d'affichage doit être active à la fois — elles sont mutuellement exclusives au niveau matériel (un seul module assemblé par carte).

### Mode bargraphe (TM1637)

Adressage des grilles :

| Grid | Fonction |
|---|---|
| 1 | Bras "−" (en dessous de la consigne) |
| 2 | Bras "+" (au-dessus de la consigne) |
| 3 | LED centrale (à la consigne) |
| 4 | WiFi (bit 0) + sauvegarde (bit 1), combinés |
| 5, 6 | Libres / réserve |

⚠️ Le mapping bit → LED dans `displayUpdate()` suppose un ordre séquentiel simple. À vérifier/ajuster selon le câblage réel du module une fois en main.

### Mode 7 segments (TM1637)

Affiche la température congélateur au repos, bascule sur la consigne pendant un ajustement aux boutons, sauvegarde automatique après 3s d'inactivité. Les 2 grilles inutilisées par le double afficheur pilotent les LED WiFi/sauvegarde.

⚠️ Limite physique connue : un nombre négatif à 2 chiffres (ex. -15) nécessite 3 caractères et ne peut pas s'afficher en entier sur 2 positions. Voir le commentaire `// TODO` dans `displayUpdate()`.

## Machine à états des boutons

- **Appui court SW1** : consigne −0.5°C
- **Appui court SW2** : consigne +0.5°C
- **Appui long SW1 seul (≥1.5s)** : booster ON/OFF
- **Appui long SW2 seul (≥1.5s)** : buzzer ON/OFF
- **SW1 + SW2 ensemble, maintenus 1.5s** : marche/arrêt thermostat

Une fenêtre de grâce de 200ms après le premier appui permet de détecter si le second bouton rejoint (combinaison) avant de s'engager dans un appui simple. Si l'un des deux boutons est relâché avant le seuil de 1.5s dans la branche combinée, l'action est annulée sans effet partiel.

## Configuration

1. Copier `include/config.h.example` vers `include/config.h`.
2. Renseigner SSID WiFi, mot de passe, et adresse du broker MQTT.
3. `include/config.h` est ignoré par git — ne jamais committer ce fichier avec de vrais identifiants.

```bash
cp include/config.h.example include/config.h
# éditer include/config.h, et platformio.ini selon le PCB assemblé
pio run --target upload
```

## Topics MQTT

| Topic | Direction | Contenu |
|---|---|---|
| `thermostat/state` | publication | État complet (JSON) |
| `congelateur/cmd` | abonnement | `ON` / `OFF` — marche/arrêt thermostat |
| `congelateur/booster` | abonnement | `ON` / `OFF` — mode booster |
| `congelateur/ip` | publication (retain) | Adresse IP actuelle |

## Réglages disponibles (page web / WebSocket)

| Paramètre | Plage | Description |
|---|---|---|
| `consigne` | -20.0 à 7.0 °C | Température cible |
| `hysteresis` | 1.0 à 5.0 °C | Largeur totale de la bande |
| `setExtTempMax` | 20.0 à 50.0 °C | Seuil de compensation selon température extérieure |
| `tempCongGain` | 0.7 à 1.3 | Pente de calibration capteur congélateur |
| `tempCongOffset` / `tempExtOffset` | -10.0 à +10.0 °C | Décalage de calibration |
| `alarmMarginC` | 1.0 à 15.0 °C | Marge au-dessus de la consigne avant alarme |
| `alarmDurationMin` | 1 à 120 min | Durée de dépassement soutenu avant déclenchement |
| `boosterDurationMin` | 10 à 480 min | Durée du mode booster |
| `thermostatEnabled` / `buzzerEnabled` / `boosterEnabled` | bool | Bascules |

## Méthode de calibration 2 points

1. Régler temporairement `tempCongGain = 1.0` et `tempCongOffset = 0.0`.
2. Noter un couple (brut, réel) à un point froid, en comparant à un thermomètre de référence.
3. Noter un second couple (brut, réel) à un point plus chaud.
4. Calculer : `gain = (réel₂ − réel₁) / (brut₂ − brut₁)` puis `offset = réel₁ − gain × brut₁`.
5. Saisir ces valeurs dans l'interface.

## Acheter les PCB

Disponibles sur [papymakers.com](https://papymakers.com).

| Élément | Contenu | Prix |
|---|---|---|
| Carte principale | Carte ESP32-C6, capteurs, relais compresseur, buzzer | 20 € |
| Kit affichage OLED | Face avant dédiée + carte connecteur OLED & 2 switches (2 PCB) | 10 € |
| Kit affichage TM1637 bargraphe | Face avant dédiée + carte LED/TM + carte de liaison switches (3 PCB) | 15 € |
| Kit affichage TM1637 7 segments | Face avant dédiée + carte LED/TM + carte de liaison switches (3 PCB) | 15 € |

La carte principale fonctionne seule (pilotage via la page web uniquement) ou avec l'un des trois kits d'affichage déporté, au choix — voir la section "Options de compilation" plus haut pour la sélection logicielle correspondante.

Boîtier DIN rail compatible : 6 modules (105 mm, format standard DIN 43880).



MIT — voir [LICENSE](LICENSE).
