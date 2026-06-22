# Changelog

## [2.0.0] - Nouveau PCB, ESP32-C6-DEVKITC-1-N4

### Matériel
- Remplacement de l'ESP32-C3-DevKitM-1 (discontinué) par l'ESP32-C6-DEVKITC-1-N4.
- Nouveau brochage complet, vérifié contre les contraintes connues du C6 (strapping, USB natif, pont UART du DevKitC-1, flash).
- Connecteur d'extension pour affichage déporté + 2 boutons, anticipant 3 configurations d'affichage mutuellement exclusives.
- Ajout d'un buzzer d'alarme (AC-1405G-LF) piloté via MOSFET SE2N7002E.
- Option de population pour un 2e relais de ventilation du condenseur (`FAN_PRESENT`).
- DHT20 envisagé puis écarté : le bénéfice (humidité, purement informative) ne justifiait pas le risque d'un bus I2C partagé avec l'OLED, sensible aux perturbations EMI du relais compresseur si le capteur avait été déporté.

### Commande locale
- 2 boutons physiques (SW1 −, SW2 +) avec machine à états : appui court (réglage consigne), appui long individuel (booster sur SW1, buzzer sur SW2), combinaison maintenue (marche/arrêt thermostat).
- Sauvegarde automatique des réglages ajustés au clavier physique après 3s d'inactivité, avec confirmation visuelle sur l'affichage déporté.

### Affichage déporté (sélection à la compilation)
- OLED I2C (Adafruit_SSD1306/Adafruit_GFX) : température, consigne, état thermostat/compresseur, statut WiFi, confirmation de sauvegarde.
- TM1637 bargraphe : indicateur centré sur la consigne, LED WiFi/sauvegarde sur la grille libre.
- TM1637 double 7 segments : température au repos, consigne pendant ajustement, LED WiFi/sauvegarde sur les grilles inutilisées.

### Nouvelles fonctions de régulation
- Alarme de dépassement de consigne (marge + durée soutenue configurables), remplace l'ancien seuil fixe (-15°C) devenu incompatible avec la plage élargie -20°C/+7°C.
- Mode booster : force le compresseur en continu pour une durée définie, suspend l'alarme pendant son fonctionnement, retour automatique en régulation normale en fin de durée.
- Ventilateur de circulation (si `FAN_PRESENT`) : actif pendant le fonctionnement du compresseur et le booster, avec post-ventilation après arrêt. Réécriture propre par rapport à la toute première version du projet, qui contenait un bloc dupliqué non fonctionnel.

### Conservé de la V1
- Calibration 2 points (gain + offset), détection de clone DS18B20, détection de lecture figée/capteur déconnecté, sécurité absolue basse température, persistance LittleFS, verrou d'édition web, buffer JSON corrigé.

## Connu / limitations de cette version
- Mapping bit→LED du mode bargraphe basé sur une hypothèse d'ordre séquentiel, à vérifier contre le câblage réel du module une fois assemblé.
- Affichage d'une consigne/température négative à 2 chiffres non résolu proprement en mode 7 segments (limite physique de 2 positions d'affichage).
- API de la bibliothèque TM1637 (Erriez) et de l'OLED (Adafruit) non testées en conditions réelles à ce stade — première mouture.
