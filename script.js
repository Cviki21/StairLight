document.addEventListener("DOMContentLoaded", () => {
  console.log("[DOM] DOMContentLoaded - Skripta start");

  const efektiBtn = document.getElementById("efektiNavButton");
  const postavkeBtn = document.getElementById("postavkeNavButton");
  const dayNightBtn = document.getElementById("dayNightNavButton");
  const saveBtn = document.getElementById("saveConfigButton");

  const wipeAllChk = document.getElementById("wipeAllCheckbox");
  const effectSel = document.getElementById("effectSelect");
  const instSel = document.getElementById("installationType");
  const efSredina = document.getElementById("efektKreniOd_sredina");
  const efRuba = document.getElementById("efektKreniOd_ruba");

  const dayBr = document.getElementById("dayBr");
  const nightBr = document.getElementById("nightBr");

  const colPick = document.getElementById("effectColor");
  if (colPick) {
    colPick.addEventListener("input", () => {
      // Ako je effect = Solid (tj. value=0), osvježi preview
      const effVal = parseInt(effectSel.value) || 0;
      if (effVal === 0) {
        updateEffectPreview(effVal);
      }
    });
  }

  // Navigacija
  if (efektiBtn) {
    efektiBtn.addEventListener("click", () => {
      console.log("[UI] Klik na Efekti");
      showSection("efektiPage");
    });
  }
  if (postavkeBtn) {
    postavkeBtn.addEventListener("click", () => {
      console.log("[UI] Klik na Postavke");
      showSection("postavkePage");
    });
  }
  if (dayNightBtn) {
    dayNightBtn.addEventListener("click", () => {
      console.log("[UI] Klik na Day/Night");
      showSection("dayNightPage");
    });
  }
  if (saveBtn) {
    saveBtn.addEventListener("click", () => {
      console.log("[UI] Klik na Save Settings");
      saveConfig();
    });
  }

  // Eventi
  if (wipeAllChk) wipeAllChk.addEventListener("change", updateWipeUI);
  if (effectSel) effectSel.addEventListener("change", () => {
    onEffectChange();
    updateEffectPreview(parseInt(effectSel.value));
  });
  if (instSel) instSel.addEventListener("change", handleInstallTypeChange);
  if (efSredina) efSredina.addEventListener("change", onKreniOdChange);
  if (efRuba) efRuba.addEventListener("change", onKreniOdChange);

  // Slider prikaz vrijednosti
  if (dayBr) {
    dayBr.addEventListener("input", () => {
      const dayBrVal = document.getElementById("dayBrVal");
      if (dayBrVal) dayBrVal.innerText = dayBr.value;
    });
  }
  if (nightBr) {
    nightBr.addEventListener("input", () => {
      const nightBrVal = document.getElementById("nightBrVal");
      if (nightBrVal) nightBrVal.innerText = nightBr.value;
    });
  }

  // Modal close
  const popup = document.getElementById("popup");
  const closeBtn = document.querySelector(".close-button");
  if (closeBtn) {
    closeBtn.onclick = () => {
      if (popup) popup.style.display = "none";
    };
  }
  window.onclick = (ev) => {
    if (popup && ev.target === popup) popup.style.display = "none";
  };

  console.log("[DOM] Inicijalno fetchConfig()");
  fetchConfig();
});

function showSection(sectionId) {
  console.log("[showSection] -> ", sectionId);
  const sections = ["efektiPage", "postavkePage", "dayNightPage"];
  sections.forEach(id => {
    const sec = document.getElementById(id);
    if (sec) sec.classList.add("hidden");
  });
  const target = document.getElementById(sectionId);
  if (target) {
    target.classList.remove("hidden");
    console.log("[showSection] Prikazana sekcija:", sectionId);
  }
}

function fetchConfig() {
  console.log("[fetchConfig] GET /api/getConfig");
  fetch("/api/getConfig")
    .then(res => {
      if (!res.ok) throw new Error("Network error: " + res.status);
      return res.json();
    })
    .then(data => {
      console.log("[fetchConfig] Primio config:", data);

      const wipeAll = document.getElementById("wipeAllCheckbox");
      if (wipeAll) wipeAll.checked = data.wipeAll || false;
      const ws = document.getElementById("wipeSpeed");
      if (ws) ws.value = data.speed || 15;
      const onT = document.getElementById("onTime");
      if (onT) onT.value = data.onTime || 60;
      updateWipeUI();

      const effSel = document.getElementById("effectSelect");
      if (effSel) {
        effSel.value = data.effect || 0;
        updateEffectPreview(parseInt(effSel.value));
      }
      const colPick = document.getElementById("effectColor");
      if (colPick && data.effect === 0) {
        const rr = (data.colorR ?? 255).toString(16).padStart(2, "0");
        const gg = (data.colorG ?? 255).toString(16).padStart(2, "0");
        const bb = (data.colorB ?? 255).toString(16).padStart(2, "0");
        colPick.value = "#" + rr + gg + bb;
      }

      const led1 = document.getElementById("enableLed1");
      if (led1) led1.checked = data.enableLed1 !== false;
      const led2 = document.getElementById("enableLed2");
      if (led2) led2.checked = data.enableLed2 !== false;

      const instType = document.getElementById("installationType");
      if (instType) instType.value = data.installationType || "linija";

      const up = document.getElementById("ledsUp");
      if (up) up.value = data.ledsUp || 300;
      const down = document.getElementById("ledsDown");
      if (down) down.value = data.ledsDown || 300;

      const brStp = document.getElementById("brojStepenica");
      if (brStp) brStp.value = data.brojStepenica || 12;
      const brLed = document.getElementById("brojLedicaPoStepenici");
      if (brLed) brLed.value = data.brojLedicaPoStepenici || 30;

      if (data.efektKreniOd === "ruba") {
        const rb = document.getElementById("efektKreniOd_ruba");
        if (rb) rb.checked = true;
      } else {
        const sm = document.getElementById("efektKreniOd_sredina");
        if (sm) sm.checked = true;
      }
      const rotChk = document.getElementById("rotacijaStepenica");
      if (rotChk) rotChk.checked = data.rotacijaStepenica || false;

      if (data.stepMode === "sequence") {
        const seq = document.getElementById("stepMode_sequence");
        if (seq) seq.checked = true;
      } else {
        const ao = document.getElementById("stepMode_allAtOnce");
        if (ao) ao.checked = true;
      }

      const mCur = document.getElementById("maxCurrent");
      if (mCur) mCur.value = data.maxCur || 2000;

      const ds = document.getElementById("dayStart");
      if (ds) ds.value = data.dayStart || "08:00";
      const de = document.getElementById("dayEnd");
      if (de) de.value = data.dayEnd || "20:00";
      const dayB = document.getElementById("dayBr");
      const dayBV = document.getElementById("dayBrVal");
      if (dayB && dayBV) {
        dayB.value = data.dayBr || 100;
        dayBV.innerText = data.dayBr || 100;
      }
      const nightB = document.getElementById("nightBr");
      const nightBV = document.getElementById("nightBrVal");
      if (nightB && nightBV) {
        nightB.value = data.nightBr || 30;
        nightBV.innerText = data.nightBr || 30;
      }

      onEffectChange();
      handleInstallTypeChange();
    })
    .catch(err => {
      console.error("fetchConfig error:", err);
      showPopup("Greška pri učitavanju postavki!");
    });
}

function saveConfig() {
  console.log("[saveConfig] Prikupljam vrijednosti...");
  const wAll = document.getElementById("wipeAllCheckbox");
  const spd = document.getElementById("wipeSpeed");
  const onT = document.getElementById("onTime");
  const effSel = document.getElementById("effectSelect");
  const colValEl = document.getElementById("effectColor");
  const led1 = document.getElementById("enableLed1");
  const led2 = document.getElementById("enableLed2");
  const instT = document.getElementById("installationType");
  const lUp = document.getElementById("ledsUp");
  const lDown = document.getElementById("ledsDown");
  const bStp = document.getElementById("brojStepenica");
  const bLed = document.getElementById("brojLedicaPoStepenici");
  const rbRuba = document.getElementById("efektKreniOd_ruba");
  const rot = document.getElementById("rotacijaStepenica");
  const stepSeq = document.getElementById("stepMode_sequence");
  const mC = document.getElementById("maxCurrent");
  const dS = document.getElementById("dayStart");
  const dE = document.getElementById("dayEnd");
  const dBr = document.getElementById("dayBr");
  const nBr = document.getElementById("nightBr");

  let colR = 255, colG = 255, colB = 255;
  const eff = effSel ? parseInt(effSel.value) || 0 : 0;
  if (eff === 0 && colValEl) {
    const cv = colValEl.value || "#ffffff";
    const r = parseInt(cv.substr(1, 2), 16);
    const g = parseInt(cv.substr(3, 2), 16);
    const b = parseInt(cv.substr(5, 2), 16);

    colR = isNaN(r) ? 255 : r;
    colG = isNaN(g) ? 255 : g;
    colB = isNaN(b) ? 255 : b;
  }

  const payload = {
    wipeAll: wAll ? wAll.checked : false,
    speed: spd ? parseInt(spd.value) || 15 : 15,
    onTime: onT ? parseInt(onT.value) || 60 : 60,
    effect: eff,
    colorR: colR,
    colorG: colG,
    colorB: colB,
    enableLed1: led1 ? led1.checked : true,
    enableLed2: led2 ? led2.checked : true,
    installationType: instT ? instT.value : "linija",
    ledsUp: lUp ? parseInt(lUp.value) || 300 : 300,
    ledsDown: lDown ? parseInt(lDown.value) || 300 : 300,
    brojStepenica: bStp ? parseInt(bStp.value) || 12 : 12,
    brojLedicaPoStepenici: bLed ? parseInt(bLed.value) || 30 : 30,
    efektKreniOd: rbRuba && rbRuba.checked ? "ruba" : "sredina",
    rotacijaStepenica: rot ? rot.checked : false,
    stepMode: (stepSeq && stepSeq.checked) ? "sequence" : "allAtOnce",
    maxCur: mC ? parseInt(mC.value) || 2000 : 2000,
    dayStart: dS ? dS.value || "08:00" : "08:00",
    dayEnd: dE ? dE.value || "20:00" : "20:00",
    dayBr: dBr ? parseInt(dBr.value) || 100 : 100,
    nightBr: nBr ? parseInt(nBr.value) || 30 : 30
  };

  console.log("[saveConfig] payload:", payload);

  fetch("/api/saveConfig", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  })
    .then(res => {
      if (!res.ok) throw new Error("Network error: " + res.status);
      return res.json();
    })
    .then(resp => {
      console.log("[saveConfig] odgovor servera:", resp);
      if (resp.status === "ok") {
        showPopup("Postavke su uspješno spremljene!");
      } else {
        showPopup("Greška pri spremanju postavki!");
      }
    })
    .catch(err => {
      console.error("saveConfig error:", err);
      showPopup("Greška pri spremanju postavki!");
    });
}

function updateWipeUI() {
  const wAll = document.getElementById("wipeAllCheckbox");
  const wsLabel = document.getElementById("wipeSpeedLabel");
  const wsInput = document.getElementById("wipeSpeed");
  const onTLabel = document.getElementById("onTimeLabel");
  const onTInput = document.getElementById("onTime");
  if (!wAll || !wsLabel || !wsInput || !onTLabel || !onTInput) return;

  if (wAll.checked) {
    wsLabel.style.display = "block";
    wsInput.style.display = "block";
    onTLabel.style.display = "block";
    onTInput.style.display = "block";
  } else {
    wsLabel.style.display = "none";
    wsInput.style.display = "none";
    onTLabel.style.display = "none";
    onTInput.style.display = "none";
  }
}

function onEffectChange() {
  const effSel = document.getElementById("effectSelect");
  const colDiv = document.getElementById("colorDiv");
  if (!effSel || !colDiv) return;
  const val = parseInt(effSel.value);
  // Ako je 'Solid' -> prikaži biranje boje
  if (val === 0) {
    colDiv.style.display = "block";
  } else {
    colDiv.style.display = "none";
  }
}

function handleInstallTypeChange() {
  const val = document.getElementById("installationType");
  const lin = document.getElementById("linijaSettings");
  const stp = document.getElementById("stepenicaSettings");
  if (!val || !lin || !stp) return;
  if (val.value === "stepenica") {
    lin.classList.add("hidden");
    stp.classList.remove("hidden");
  } else {
    lin.classList.remove("hidden");
    stp.classList.add("hidden");
  }
}

function onKreniOdChange() {
  const ruba = document.getElementById("efektKreniOd_ruba");
  const rotDiv = document.getElementById("rotDiv");
  if (!ruba || !rotDiv) return;
  // Ako je odabrano "ruba", prikazujemo rotaciju
  if (ruba.checked) {
    rotDiv.style.display = "block";
  } else {
    rotDiv.style.display = "none";
  }
}

function showPopup(msg) {
  const popup = document.getElementById("popup");
  const popupMsg = document.getElementById("popupMsg");
  if (!popup || !popupMsg) return;
  popupMsg.innerText = msg;
  popup.style.display = "block";
  console.log("[showPopup]", msg);
}

// Ažuriraj preview ovisno o efektu
function updateEffectPreview(val) {
  const preview = document.getElementById("effectPreview");
  if (!preview) return;
  preview.className = "effectPreview";
  preview.style.backgroundColor = "";

  switch (val) {
    case 0: {
      preview.classList.add("solidPreview");
      const c = document.getElementById("effectColor");
      if (c) preview.style.backgroundColor = c.value;
    } break;
    case 1: preview.classList.add("confettiPreview"); break;
    case 2: preview.classList.add("rainbowPreview"); break;
    case 3: preview.classList.add("meteorPreview"); break;
    case 4: preview.classList.add("fadeInOutPreview"); break;
    case 5: preview.classList.add("firePreview"); break;
    case 6: preview.classList.add("lightningPreview"); break;
    default:
      preview.classList.add("solidPreview");
      break;
  }
}
