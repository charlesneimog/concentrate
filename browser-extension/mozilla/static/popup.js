function isValidPort(value) {
  const port = Number(value);
  return Number.isInteger(port) && port >= 1 && port <= 65535;
}

function setStatus(text) {
  const el = document.getElementById("status");
  el.textContent = text;
}

function initHeaderFromManifest() {
  try {
    const manifest = browser.runtime.getManifest();
    const nameEl = document.getElementById("appName");
    const iconEl = document.getElementById("appIcon");

    if (nameEl && manifest?.name) {
      nameEl.textContent = manifest.name;
    }

    if (iconEl && manifest?.icons) {
      const iconPath =
        manifest.icons["32"] ||
        manifest.icons["48"] ||
        manifest.icons["128"];
      if (iconPath) {
        iconEl.src = browser.runtime.getURL(iconPath);
        iconEl.alt = manifest?.name || "";
      }
    }
  } catch (_) {
    // ignore
  }
}

async function getCurrentPort() {
  try {
    const resp = await browser.runtime.sendMessage({ type: "getPort" });
    if (resp && isValidPort(resp.port)) return resp.port;
  } catch (_) {
    // ignore; fall back to storage
  }

  try {
    const { port } = await browser.storage.local.get("port");
    if (isValidPort(port)) return Number(port);
  } catch (_) {
    // ignore
  }

  return 7079;
}

async function savePort(port) {
  const resp = await browser.runtime.sendMessage({ type: "setPort", port });
  if (!resp || resp.ok !== true) {
    throw new Error(resp?.error || "Failed to set port");
  }
}

document.addEventListener("DOMContentLoaded", async () => {
  //initHeaderFromManifest();

  const input = document.getElementById("port");
  const saveBtn = document.getElementById("save");

  const port = await getCurrentPort();
  input.value = String(port);

  saveBtn.addEventListener("click", async () => {
    const nextPort = Number(input.value);
    if (!isValidPort(nextPort)) {
      setStatus("Enter a valid port (1000-9999).");
      return;
    }

    try {
      await savePort(nextPort);
      setStatus(`Saved. Using port ${nextPort}.`);
    } catch (e) {
      setStatus(e?.message || String(e));
    }
  });
});
