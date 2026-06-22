#pragma once

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Thermostat ESP32</title>
<style>
  body { font-family: Arial, sans-serif; margin: 20px; }
  h2 { color: #2c3e50; }
  .status { margin: 10px 0; padding: 10px; border: 1px solid #ccc; border-radius: 5px; display: inline-block; }
  .led { width: 20px; height: 20px; border-radius: 50%; display: inline-block; margin-left: 10px; }
  .led.off { background: #ccc; }
  .led.on { background: red; }
  input { width: 80px; }
  button { padding: 5px 10px; margin-top: 10px; }
  #thermostatStatus.actif { color: #27ae60; font-weight: bold; }
  #thermostatStatus.arret { color: #c0392b; font-weight: bold; }
  #btnToggleThermostat.arret { background: #c0392b; color: white; }
  #editingNotice { display: none; color: #e67e22; font-weight: bold; margin-bottom: 10px; }
  .stuckWarning { color: #e67e22; font-weight: bold; }
  #connStatus { font-weight: bold; margin-bottom: 10px; }
  #connStatus.connected { color: #27ae60; }
  #connStatus.disconnected { color: #c0392b; }
  #submitError { display: none; color: #c0392b; font-weight: bold; margin-top: 8px; }
  #saveAck { display: none; font-weight: bold; margin-top: 8px; }
  #saveAck.ok { color: #27ae60; }
  #saveAck.fail { color: #c0392b; }
  #alarmBanner { display: none; background: #c0392b; color: white; padding: 8px; border-radius: 5px; font-weight: bold; margin-bottom: 10px; }
  #buzzerStatus.actif { color: #27ae60; font-weight: bold; }
  #buzzerStatus.arret { color: #c0392b; font-weight: bold; }
  #btnToggleBuzzer.arret { background: #c0392b; color: white; }
  #boosterStatus.actif { color: #2980b9; font-weight: bold; }
  #boosterStatus.arret { color: inherit; }
  #btnToggleBooster.actif { background: #2980b9; color: white; }
</style>
</head>
<body>

<h2>Thermostat ESP32</h2>

<div id="connStatus" class="disconnected">⏳ Connexion en cours...</div>
<div id="alarmBanner">⚠️ Alarme : température au-delà de la marge autorisée depuis un moment prolongé</div>

<div class="status">
  Température congélateur : <span id="tempCong">--</span> °C <span id="tempCongStuckWarning" class="stuckWarning" style="display:none;">⚠️ figé</span><br>
  Température extérieure : <span id="tempExt">--</span> °C <span id="tempExtStuckWarning" class="stuckWarning" style="display:none;">⚠️ figé</span><br>
  Consigne : <span id="consigneDisplay">--</span> °C<br>
  Hystérésis (largeur totale) : <span id="hysteresisDisplay">--</span> °C<br>
  Seuil ext : <span id="setExtTempMaxDisplay">--</span> °C<br>
  Offset calibration congélateur : <span id="tempCongOffsetDisplay">--</span> °C<br>
  Gain calibration congélateur : <span id="tempCongGainDisplay">--</span><br>
  Compresseur : <span class="led off" id="led"></span><br>
  Ventilateur : <span id="fanStateDisplay">--</span><br>
  Thermostat : <span id="thermostatStatus">--</span><br>
  Buzzer : <span id="buzzerStatus">--</span><br>
  Booster : <span id="boosterStatus">--</span><br>
  Compteur M/A (raz 0h00) : <span id="counter">--</span>
</div>

<h3>Commande</h3>
<button id="btnToggleThermostat" type="button">--</button>
<button id="btnToggleBuzzer" type="button">--</button>
<button id="btnToggleBooster" type="button">--</button>


<h3>Réglages</h3>
<div id="editingNotice">✏️ Édition en cours — valeurs figées (la page ne se mettra pas à jour tant que tu n'as pas validé ou annulé)</div>
<form id="settingsForm">
  Consigne : <input type="number" step="0.1" id="consigneInput">
  <small>(min −20 °C / max +7 °C)</small><br><br>
  Hystérésis (largeur totale) : <input type="number" step="0.1" id="hysteresisInput">
  <small>(min 1 °C / max 5 °C)</small><br><br>
  Seuil ext (°C) : <input type="number" step="0.5" id="setExtTempMaxInput">
  <small>(min 20 °C / max 50 °C)</small><br><br>
  Gain calibration congélateur : <input type="number" step="0.001" id="tempCongGainInput">
  <small>(min 0.7 / max 1.3 — corrige une erreur de pente, pas juste un décalage constant)</small><br><br>
  Offset calibration congélateur (°C) : <input type="number" step="0.1" id="tempCongOffsetInput">
  <small>(min −10 °C / max +10 °C — voir calcul 2 points ci-dessous)</small><br><br>
  Offset calibration extérieur (°C) : <input type="number" step="0.1" id="tempExtOffsetInput">
  <small>(min −10 °C / max +10 °C)</small><br><br>
  Marge alarme (°C au-dessus de la consigne) : <input type="number" step="0.5" id="alarmMarginInput">
  <small>(min 1 °C / max 15 °C)</small><br><br>
  Durée avant déclenchement alarme (min) : <input type="number" step="1" id="alarmDurationInput">
  <small>(min 1 / max 120)</small><br><br>
  Durée du booster (min) : <input type="number" step="5" id="boosterDurationInput">
  <small>(min 10 / max 480)</small><br><br>
  <button type="submit">Appliquer</button>
  <button type="button" id="btnCancelEdit">Annuler</button>
  <div id="submitError">⚠️ Pas encore connecté au module — réessaie dans un instant.</div>
  <div id="saveAck"></div>
</form>

<script>
let ws;
let thermostatEnabledState = true;  // mis à jour à chaque message reçu du serveur
let buzzerEnabledState = true;
let boosterEnabledState = false;
let editing = false;                // true dès qu'un champ du formulaire est touché
let editingTimeout = null;
let lastKnownState = {};            // dernière trame reçue du serveur, pour "Annuler"
let awaitingSaveConfirm = false;    // true juste après un envoi, pour afficher l'accusé de réception

function startEditing() {
  editing = true;
  document.getElementById('editingNotice').style.display = 'block';
  clearTimeout(editingTimeout);
  // sécurité : si on oublie le champ ouvert, on redébloque la synchro après 2 min
  editingTimeout = setTimeout(stopEditing, 120000);
}

function stopEditing() {
  editing = false;
  document.getElementById('editingNotice').style.display = 'none';
  clearTimeout(editingTimeout);
}

function showSubmitError() {
  const el = document.getElementById('submitError');
  el.style.display = 'block';
  clearTimeout(showSubmitError._t);
  showSubmitError._t = setTimeout(() => { el.style.display = 'none'; }, 4000);
}

function wsReady() {
  return ws && ws.readyState === WebSocket.OPEN;
}
function applyInputsFromState(state) {
  if (state.consigne !== undefined) document.getElementById('consigneInput').value = state.consigne.toFixed(2);
  if (state.hysteresis !== undefined) document.getElementById('hysteresisInput').value = state.hysteresis.toFixed(2);
  if (state.setExtTempMax !== undefined) document.getElementById('setExtTempMaxInput').value = state.setExtTempMax.toFixed(1);
  if (state.tempCongGain !== undefined) document.getElementById('tempCongGainInput').value = state.tempCongGain.toFixed(3);
  if (state.tempCongOffset !== undefined) document.getElementById('tempCongOffsetInput').value = state.tempCongOffset.toFixed(2);
  if (state.tempExtOffset !== undefined) document.getElementById('tempExtOffsetInput').value = state.tempExtOffset.toFixed(2);
  if (state.alarmMarginC !== undefined) document.getElementById('alarmMarginInput').value = state.alarmMarginC.toFixed(1);
  if (state.alarmDurationMin !== undefined) document.getElementById('alarmDurationInput').value = state.alarmDurationMin.toFixed(0);
  if (state.boosterDurationMin !== undefined) document.getElementById('boosterDurationInput').value = state.boosterDurationMin.toFixed(0);
}

function initWebSocket() {
  ws = new WebSocket('ws://' + location.hostname + ':81/');

  ws.onopen = () => {
    console.log('WebSocket connecté');
    const el = document.getElementById('connStatus');
    el.textContent = '✅ Connecté';
    el.className = 'connected';
  };

  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    Object.assign(lastKnownState, data);  // garde toujours la dernière vérité serveur, même en édition

    if (data.tempCong !== undefined)
      document.getElementById('tempCong').textContent = data.tempCong.toFixed(2);

    if (data.tempCongStuck !== undefined)
      document.getElementById('tempCongStuckWarning').style.display = data.tempCongStuck ? 'inline' : 'none';

    if (data.tempExt !== undefined)
      document.getElementById('tempExt').textContent = data.tempExt.toFixed(2);

    if (data.tempExtStuck !== undefined)
      document.getElementById('tempExtStuckWarning').style.display = data.tempExtStuck ? 'inline' : 'none';

    if (data.consigne !== undefined)
      document.getElementById('consigneDisplay').textContent = data.consigne.toFixed(2);

    if (data.hysteresis !== undefined)
      document.getElementById('hysteresisDisplay').textContent = data.hysteresis.toFixed(2);

    if (data.setExtTempMax !== undefined)
      document.getElementById('setExtTempMaxDisplay').textContent = data.setExtTempMax.toFixed(1);

    if (data.tempCongGain !== undefined)
      document.getElementById('tempCongGainDisplay').textContent = data.tempCongGain.toFixed(3);

    if (data.tempCongOffset !== undefined)
      document.getElementById('tempCongOffsetDisplay').textContent = data.tempCongOffset.toFixed(2);

    if (data.alarmActive !== undefined)
      document.getElementById('alarmBanner').style.display = data.alarmActive ? 'block' : 'none';

    if (data.buzzerEnabled !== undefined) {
      const statusEl = document.getElementById('buzzerStatus');
      const btnEl = document.getElementById('btnToggleBuzzer');
      buzzerEnabledState = data.buzzerEnabled;
      if (buzzerEnabledState) {
        statusEl.textContent = 'ACTIF';
        statusEl.className = 'actif';
        btnEl.textContent = 'Couper le buzzer';
        btnEl.className = '';
      } else {
        statusEl.textContent = 'COUPÉ';
        statusEl.className = 'arret';
        btnEl.textContent = 'Réactiver le buzzer';
        btnEl.className = 'arret';
      }
    }

    if (data.boosterEnabled !== undefined) {
      const statusEl = document.getElementById('boosterStatus');
      const btnEl = document.getElementById('btnToggleBooster');
      boosterEnabledState = data.boosterEnabled;
      if (boosterEnabledState) {
        const remain = data.boosterRemainingMin !== undefined ? Math.round(data.boosterRemainingMin) : '?';
        statusEl.textContent = 'ACTIF (' + remain + ' min restantes)';
        statusEl.className = 'actif';
        btnEl.textContent = 'Arrêter le booster';
        btnEl.className = 'actif';
      } else {
        statusEl.textContent = 'inactif';
        statusEl.className = 'arret';
        btnEl.textContent = 'Démarrer le booster';
        btnEl.className = '';
      }
    }

    // Les champs de SAISIE ne sont resynchronisés que si l'utilisateur n'est pas en train d'éditer,
    // sinon une trame arrivant pendant la frappe écraserait la valeur en cours de saisie.
    if (!editing) applyInputsFromState(data);

    if (data.thermostatEnabled !== undefined) {
      thermostatEnabledState = data.thermostatEnabled;
      const statusEl = document.getElementById('thermostatStatus');
      const btnEl = document.getElementById('btnToggleThermostat');

      if (thermostatEnabledState) {
        statusEl.textContent = 'ACTIF';
        statusEl.className = 'actif';
        btnEl.textContent = 'Couper le thermostat';
        btnEl.className = '';
      } else {
        statusEl.textContent = 'ARRÊTÉ (manuel)';
        statusEl.className = 'arret';
        btnEl.textContent = 'Réactiver le thermostat';
        btnEl.className = 'arret';
      }
    }

    const led = document.getElementById('led');
    if (data.state === "ON") {
      led.classList.add('on');
      led.classList.remove('off');
    } else {
      led.classList.add('off');
      led.classList.remove('on');
    }

    if (data.counter !== undefined)
      document.getElementById('counter').textContent = data.counter;

    if (data.fanState !== undefined)
      document.getElementById('fanStateDisplay').textContent = data.fanState;

    if (awaitingSaveConfirm && data.saveOk !== undefined) {
      const ack = document.getElementById('saveAck');
      ack.style.display = 'block';
      if (data.saveOk) {
        ack.textContent = '✅ Réglages sauvegardés (LittleFS)';
        ack.className = 'ok';
      } else {
        ack.textContent = '⚠️ Échec de sauvegarde sur LittleFS — réglages actifs mais non persistés';
        ack.className = 'fail';
      }
      clearTimeout(window._saveAckTimeout);
      window._saveAckTimeout = setTimeout(() => { ack.style.display = 'none'; }, 5000);
      awaitingSaveConfirm = false;
    }
  };

  ws.onclose = () => {
    console.log('WebSocket fermé, reconnexion...');
    const el = document.getElementById('connStatus');
    el.textContent = '❌ Déconnecté — reconnexion...';
    el.className = 'disconnected';
    setTimeout(initWebSocket, 3000);
  };
}

document.getElementById('btnToggleThermostat').addEventListener('click', () => {
  if (!wsReady()) { showSubmitError(); return; }

  const newState = !thermostatEnabledState;
  console.log("📤 WS SEND:", { thermostatEnabled: newState });
  ws.send(JSON.stringify({ thermostatEnabled: newState }));
  awaitingSaveConfirm = true;
});

document.getElementById('btnToggleBuzzer').addEventListener('click', () => {
  if (!wsReady()) { showSubmitError(); return; }

  const newState = !buzzerEnabledState;
  console.log("📤 WS SEND:", { buzzerEnabled: newState });
  ws.send(JSON.stringify({ buzzerEnabled: newState }));
  awaitingSaveConfirm = true;
});

document.getElementById('btnToggleBooster').addEventListener('click', () => {
  if (!wsReady()) { showSubmitError(); return; }

  const newState = !boosterEnabledState;
  console.log("📤 WS SEND:", { boosterEnabled: newState });
  ws.send(JSON.stringify({ boosterEnabled: newState }));
  awaitingSaveConfirm = true;
});

// Dès qu'on touche un champ de réglage, on fige la synchro jusqu'à validation/annulation
// (focus = clic dans le champ ; input = changement réel de valeur, couvre aussi les flèches du spinner
// qui ne déclenchent pas toujours focus de façon fiable selon le navigateur)
['consigneInput', 'hysteresisInput', 'setExtTempMaxInput', 'tempCongGainInput', 'tempCongOffsetInput',
 'tempExtOffsetInput', 'alarmMarginInput', 'alarmDurationInput', 'boosterDurationInput']
  .forEach(id => {
    const el = document.getElementById(id);
    el.addEventListener('focus', startEditing);
    el.addEventListener('input', startEditing);
  });

document.getElementById('btnCancelEdit').addEventListener('click', () => {
  applyInputsFromState(lastKnownState);  // on revient à la dernière valeur connue du serveur
  stopEditing();
});

document.getElementById('settingsForm').addEventListener('submit', (e) => {
  e.preventDefault();

  if (!wsReady()) { showSubmitError(); return; }  // on ne quitte plus le mode édition si l'envoi échoue

  const msg = {
    consigne: parseFloat(document.getElementById('consigneInput').value),
    hysteresis: parseFloat(document.getElementById('hysteresisInput').value),
    setExtTempMax: parseFloat(document.getElementById('setExtTempMaxInput').value),
    tempCongGain: parseFloat(document.getElementById('tempCongGainInput').value),
    tempCongOffset: parseFloat(document.getElementById('tempCongOffsetInput').value),
    tempExtOffset: parseFloat(document.getElementById('tempExtOffsetInput').value),
    alarmMarginC: parseFloat(document.getElementById('alarmMarginInput').value),
    alarmDurationMin: parseFloat(document.getElementById('alarmDurationInput').value),
    boosterDurationMin: parseFloat(document.getElementById('boosterDurationInput').value)
  };

  console.log("📤 WS SEND:", msg);
  ws.send(JSON.stringify(msg));
  awaitingSaveConfirm = true;
  stopEditing();  // la valeur envoyée fera autorité dans la prochaine trame reçue
});

initWebSocket();
</script>

</body>
</html>
)rawliteral";
